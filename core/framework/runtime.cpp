#include "daemon/framework/runtime.hpp"
#include <limits>
#include <cstdio>

namespace daemonos {
Runtime::Runtime(hal::Clock& clock, hal::Random& random, StateStore& store, CheckpointStore& checkpoint, InferenceProvider& provider,VoiceDirectory* identities,ResourceManager* resources)
    : identities_(identities), clock_(clock), random_(random), store_(store), checkpoint_(checkpoint), provider_(provider), assistant_(clock,store,identities), agents_(clock,store,identities),resources_(resources ? *resources : local_resources_) {
    services_.add<hal::Clock>(clock_); services_.add<StateStore>(store_);
    services_.add<EventBus>(events_); services_.add<PowerManager>(power_);
    services_.add<ResourceManager>(resources_);
    services_.add<Scheduler>(scheduler_);
    applications_.add(launcher_); applications_.add(clock_app_);
    applications_.add(settings_app_); applications_.add(creature_app_);
    subscription_ = events_.subscribe(on_event, this);
    power_.bind(ServiceId::applications,+[](void* context,bool enabled) {
        static_cast<Runtime*>(context)->navigation_.suspend(!enabled); return Status{};
    },this);
}
Runtime::~Runtime() { if (booted_) { provider_.cancel(); } }
Status Runtime::boot() {
    if (booted_) return Status::fail(Error::busy, "runtime already booted");
    // Preserve checkpoint recovery while the unhealthy directory keeps every privileged path closed.
    if(identities_) (void)identities_->load();
    runtime_memory_=resources_.reserve(MemoryCategory::runtime,MemoryPool::external,sizeof(Runtime));
    if (!runtime_memory_) return Status::fail(Error::full,"runtime memory budget exhausted");
    auto status = store_.load(creature_, settings_);
    if (!status) {
        // Never overwrite a schema from a newer release or a locked database.
        if (status.code == Error::incompatible || status.code == Error::busy) return status;
        auto checkpoint_status = checkpoint_.load(creature_);
        if (!checkpoint_status) {
            if (status.code != Error::unavailable || checkpoint_status.code != Error::unavailable)
                return Status::fail(Error::corrupt, "state inaccessible and no valid identity checkpoint; refusing to replace creature");
            creature_ = CreatureEngine::create(clock_, random_);
        }
        storage_status_.assign(checkpoint_status ? "Recovered minimal checkpoint" : "New local identity");
    } else storage_status_.assign(store_.backend_name());
    char greeting[96]{}; std::snprintf(greeting,sizeof(greeting),"Hello, keeper. I am %s.",creature_.name.c_str()); message_.assign(greeting);
    const auto departure_health=creature_.needs.health;
    CreatureEngine::advance(creature_, CreatureEngine::offline_elapsed(clock_.wall_seconds(), creature_.last_wall) * 1000);
    // An unclassified reboot may be a depleted battery or storage failure.
    // Preserve health across that technical downtime; other needs and age progress.
    creature_.needs.health=std::max(departure_health,creature_.needs.health);
    creature_.last_wall = std::max<std::int64_t>(0, clock_.wall_seconds());
    last_mono_ = last_input_ = last_save_ = last_checkpoint_ = clock_.monotonic_ms();
    navigation_.push(AppId::launcher); booted_ = true;
    // Initial identity must survive a first-run storage fault.
    const auto checkpoint_status = checkpoint_.save(creature_);
    const auto save_status = flush();
    if (!checkpoint_status && !save_status) return Status::fail(Error::io, "cannot persist initial identity");
    const auto recovered=agents_.recover();
    tasks_recovered_=static_cast<bool>(recovered) || recovered.code==Error::unavailable || recovered.code==Error::unsupported;
    coordinate_power();
    return {};
}
void Runtime::tick() {
    if (!booted_) return;
    const auto now = clock_.monotonic_ms();
    if(secured() && private_message_) {
        SpeakerContext audience;
        if(!identities_->context(message_session_,audience)) { message_.assign("Profilo bloccato. I messaggi privati aspettano lo sblocco."); private_message_=false; speech_.cancel(); audio_lock_.reset(); ++view_revision_; }
    }
    if(!speaking() && !listening()) audio_lock_.reset();
    if (events_.pending()==0 && next_delay_ms()>0) return;
    if (events_.pending() || now-last_mono_>=60'000-creature_.pending_ms) advance(now);
    if (face_cue_ && now >= face_cue_started_+2500) face_cue_.reset();
    events_.dispatch();
    if (scheduler_.next_deadline()<=now) {
        auto scheduled=power_.acquire(ServiceId::scheduler,mask(Resource::scheduled),now,1000);
        if (scheduled) {
            power_.update(environment_,std::max<std::int64_t>(0,now-last_input_),cognition_.active(),now);
            scheduler_.run(now);
        }
    }
    if (cognition_.active() && (now>=next_cognitive_poll_ || !environment_.connected || !settings_.cognition || resources_.pressured())) {
        poll_cognition(); next_cognitive_poll_=now+20;
    }
    if(now>=assistant_.next_deadline_ms() || now>=next_task_scan_ms_ || clock_.wall_seconds()>=next_task_scan_wall_) {
        auto work=power_.acquire(ServiceId::scheduler,mask(Resource::scheduled),now,1000);
        auto storage=power_.acquire(ServiceId::storage,mask(Resource::storage),now,30'000);
        if(work && storage) {
            power_.update(environment_,std::max<std::int64_t>(0,now-last_input_),cognition_.active(),now);
            assistant_.tick();
            if(assistant_revision_!=assistant_.revision()) { message_.assign(assistant_.reply()); assistant_revision_=assistant_.revision(); next_task_scan_ms_=0; if(assistant_.alerting()) last_input_=now; }
            poll_agent_tasks();
        }
    }
    coordinate_power();
    if (!suspended_ && dirty_ && now-last_save_>=budget::save_interval_ms) { (void)flush(); last_save_=now; }
    if (!suspended_ && now-last_checkpoint_>=budget::checkpoint_interval_ms) {
        if (checkpoint_.save(creature_)) last_checkpoint_ = now;
    }
    ++view_revision_;
}
void Runtime::show_face(CreatureFace face) { face_cue_=face; face_cue_started_=clock_.monotonic_ms(); }
void Runtime::inference_error(const Status& status) {
    if(status.code==Error::io || status.code==Error::unavailable) remote_unreachable_=true;
    else if(status.code==Error::invalid || status.code==Error::corrupt || status.code==Error::full) show_face(CreatureFace::internal_error);
}
CreatureFace Runtime::face() const {
    const auto now=clock_.monotonic_ms();
    const bool cue=face_cue_ && now-face_cue_started_<2500;
    if(internal_fault_ || storage_fault_ || !environment_.storage || (cue && *face_cue_==CreatureFace::internal_error)) return CreatureFace::internal_error;
    if(services_installing_) return CreatureFace::installing_services;
    if(listening()) return CreatureFace::listening;
    const auto phone=phone_link_override_.value_or(assistant_.phone_state());
    if(phone==LinkState::connecting && (phone_link_override_ || assistant_.phone_configured())) return CreatureFace::connecting;
    if(phone==LinkState::offline && (phone_link_override_ || assistant_.phone_configured())) return CreatureFace::phone_offline;
    if(inference_link_override_==LinkState::connecting || (thinking() && provider_.phase()==InferencePhase::connecting)) return CreatureFace::connecting;
    if(remote_unreachable_ || inference_link_override_==LinkState::offline) return CreatureFace::inference_unreachable;
    if(thinking()) return !receiving_response_ && provider_.phase()!=InferencePhase::generating ? CreatureFace::waiting_response : CreatureFace::thinking;
    if(assistant_.waiting_for_service()) return CreatureFace::waiting_response;
    if(creature_.sleeping) return CreatureFace::resting;
    const auto& needs=creature_.needs;
    if(needs.energy<=20) return CreatureFace::tired;
    if(needs.hunger>=75) return CreatureFace::hungry;
    if(needs.health<=35 || needs.stress>=70) return CreatureFace::sad;
    if(cue) return *face_cue_;
    if(environment_.charging) return CreatureFace::charging;
    if(needs.happiness<=35 || needs.affection<=20 || needs.boredom>=75) return CreatureFace::sad;
    if(needs.happiness>=80) return CreatureFace::happy;
    if(needs.curiosity>=75) return CreatureFace::curious;
    return CreatureFace::calm;
}
void Runtime::handle(const Event& event) {
    const auto now = clock_.monotonic_ms();
    if (event.type == EventType::feed || event.type == EventType::rest || event.type == EventType::pet ||
        event.type == EventType::open_app || event.type == EventType::home || event.type == EventType::back ||
        event.type == EventType::think || event.type == EventType::wake || event.type == EventType::brightness)
        last_input_ = now;
    if (event.type==EventType::wake) wake_reason_.assign("button");
    else if (event.type==EventType::charging && event.value!=0) { last_input_=now; wake_reason_.assign("charging"); }
    else if (last_input_==now) wake_reason_.assign("interaction");
    if (CreatureEngine::interact(creature_, event.type)) {
        if(event.type==EventType::feed || event.type==EventType::pet) show_face(CreatureFace::happy);
        else if(event.type==EventType::move) show_face(CreatureFace::curious);
        else if(event.type==EventType::rest) face_cue_.reset();
        switch (event.type) {
        case EventType::feed: message_.assign("Rifornimento accettato. Io almeno non sporco i piatti."); remember("My keeper fed me warm sparks."); break;
        case EventType::rest: message_.assign(creature_.sleeping ? "Mi ricarico. Tu per la memoria hai provato?" : "Operativo. Vediamo cosa ti sei dimenticato."); remember(creature_.sleeping ? "I settled down to rest." : "I woke up with my keeper."); break;
        case EventType::pet: message_.assign("Va bene, ti voglio bene anch'io. Non farne un caso."); remember("My keeper showed me affection."); break;
        case EventType::move:
            show_face(CreatureFace::curious);
            environment_.steps += static_cast<std::uint32_t>(std::clamp<std::int32_t>(event.value, 0, 10000));
            message_.assign("Passi registrati. Il divano sentira' la tua mancanza."); remember("We moved and explored together."); break;
        default: break;
        }
        // Debounced on the persistence cadence, keeping touch bursts off storage.
    }
    switch (event.type) {
    case EventType::battery: environment_.battery = std::clamp<std::int32_t>(event.value, 0, 100); next_task_scan_ms_=0; break;
    case EventType::charging: environment_.charging = event.value != 0; next_task_scan_ms_=0; break;
    case EventType::connectivity:
        environment_.connected=event.value!=0; next_task_scan_ms_=0;
        if(environment_.connected) remote_unreachable_=false;
        break;
    case EventType::storage:
        environment_.storage = event.value != 0; store_.available(environment_.storage);
        assistant_.storage_changed();
        next_task_scan_ms_=0;
        if (environment_.storage) (void)flush();
        else storage_status_.assign("Storage fault / RAM + checkpoint");
        break;
    case EventType::open_app:
        if (event.value >= 0 && event.value <= 3) navigation_.push(static_cast<AppId>(event.value));
        break;
    case EventType::back: navigation_.back(); break;
    case EventType::home: navigation_.home(); break;
    case EventType::think: (void)think(); break;
    case EventType::toggle_cognition: settings_.cognition = event.value != 0; next_task_scan_ms_=0; break;
    case EventType::brightness: settings_.brightness = std::clamp<std::int32_t>(event.value, 10, 100); break;
    case EventType::suspend: last_input_ = now - 60'000; break;
    case EventType::services_install:
        services_installing_=event.value!=0;
        if(!services_installing_) show_face(CreatureFace::happy);
        last_input_=now; break;
    case EventType::phone_link:
        if(event.value<0) phone_link_override_.reset();
        else if(event.value<=2) phone_link_override_=static_cast<LinkState>(event.value);
        last_input_=now; break;
    case EventType::inference_link:
        if(event.value<0) { inference_link_override_.reset(); remote_unreachable_=false; }
        else if(event.value<=2) { inference_link_override_=static_cast<LinkState>(event.value); remote_unreachable_=event.value==0; }
        last_input_=now; break;
    case EventType::internal_fault: internal_fault_=event.value!=0; last_input_=now; break;
    case EventType::listen_start:
        listening_token_=event.token; listening_until_ms_=now+(event.value>0 ? std::clamp<std::int32_t>(event.value,1,120'000) : 30'000);
        speech_.cancel(); audio_lock_.reset();
        audio_lock_=power_.acquire(ServiceId::audio,Resource::cpu|Resource::audio,now,std::min<std::int64_t>(120'000,listening_until_ms_-now),true,PowerState::listening);
        if(!audio_lock_) listening_until_ms_=0;
        last_input_=now;
        if(creature_.sleeping) (void)CreatureEngine::interact(creature_,EventType::rest);
        break;
    case EventType::listen_stop: if(event.token==listening_token_) { listening_until_ms_=0; audio_lock_.reset(); } break;
    case EventType::speech_start:
        audio_lock_.reset(); audio_lock_=power_.acquire(ServiceId::audio,Resource::cpu|Resource::audio,now,event.value>0 ? std::clamp<std::int64_t>(event.value,1,300'000) : 30'000,true,PowerState::speaking);
        if(!audio_lock_) break;
        speech_.start(event.token,now,event.value); listening_until_ms_=0; last_input_=now;
        if(creature_.sleeping) (void)CreatureEngine::interact(creature_,EventType::rest);
        break;
    case EventType::speech_level: speech_.level(event.token,event.value,now); break;
    case EventType::speech_stop: speech_.stop(event.token); if(!speaking()) audio_lock_.reset(); break;
    case EventType::memory_pressure:
        resources_.pressure(event.value!=0);
        if (resources_.pressured()) (void)store_.trim();
        break;
    case EventType::peripheral: {
        const auto value=event.value;
        if (value==2 || value==-2 || value==4 || value==-4 || value==8 || value==-8 || value==16 || value==-16 || value==64 || value==-64)
            power_.allow(static_cast<Resource>(value<0 ? -value : value),value>0);
        break;
    }
    case EventType::inspect_contract: break; // Developer inspection lives in the lab, outside watch UI.
    default: break;
    }
    dirty_=true;
    if (auto* foreground = navigation_.foreground(); foreground && !navigation_.suspended()) foreground->event(event);
}
void Runtime::remember(std::string_view content, std::string_view provenance,std::string_view actor,double importance,double valence) {
    if (pending_count_ == pending_.size()) { ++lost_memories_; return; }
    auto& memory = pending_[pending_count_++];
    memory = {};
    if(secured() && (actor.empty() || !memory.actor_id.assign(actor))) { --pending_count_; return; }
    memory.timestamp = creature_.last_wall;
    memory.content.assign(content); memory.provenance.assign(provenance);
    memory.importance=importance; memory.valence=valence;
    memory.tags.assign("keeper,interaction"); memory.entities.assign(creature_.name.view());
}
Status Runtime::flush(bool force_checkpoint) {
    if (!booted_) return Status::fail(Error::unavailable, "runtime not booted");
    auto lock=power_.acquire(ServiceId::storage,mask(Resource::storage),clock_.monotonic_ms(),30'000);
    if (!lock) return Status::fail(Error::busy,"storage wake lock unavailable");
    power_.update(environment_,std::max<std::int64_t>(0,clock_.monotonic_ms()-last_input_),cognition_.active(),clock_.monotonic_ms());
    Status result{};
    // State and each associated experience commit together. Failed memories stay queued.
    if (pending_count_ == 0) { ++database_operations_; result = store_.save(creature_, settings_); }
    while (pending_count_ && result) {
        ++database_operations_; result = store_.save(creature_, settings_, &pending_[0]);
        if (result) {
            for (std::size_t i = 1; i < pending_count_; ++i) pending_[i - 1] = pending_[i];
            --pending_count_;
        }
    }
    storage_status_.assign(result ? store_.backend_name() : std::string_view{"Storage unavailable / RAM retained"});
    storage_fault_=!result;
    if (force_checkpoint) {
        const auto status = checkpoint_.save(creature_);
        if (!status) return status;
        last_checkpoint_ = clock_.monotonic_ms();
    }
    last_save_ = clock_.monotonic_ms();
    if (result) dirty_=false;
    lock.reset();
    power_.update(environment_,std::max<std::int64_t>(0,clock_.monotonic_ms()-last_input_),cognition_.active(),clock_.monotonic_ms());
    return result;
}
Status Runtime::request(std::string_view input) {
    if(secured()) return Status::fail(Error::denied,"identity session required");
    identified_request_=false;
    if(!booted_) return Status::fail(Error::unavailable,"runtime not booted");
    if(cognition_.active()) return Status::fail(Error::busy,"inference already active");
    if(input.empty() || !user_input_.assign(input)) return Status::fail(Error::invalid,"empty or oversized user input");
    last_input_=clock_.monotonic_ms();
    return think();
}
Status Runtime::request(const UserSession& session,std::string_view input) {
    if(!booted_) return Status::fail(Error::unavailable,"runtime not booted");
    if(cognition_.active()) return Status::fail(Error::busy,"inference already active");
    SpeakerContext c;
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto status=identities_->context(session,c); !status) return status;
    if(input.empty() || !user_input_.assign(input)) return Status::fail(Error::invalid,"empty or oversized user input");
    // A different speaker immediately replaces any previous private presentation.
    message_.assign("Ti ascolto."); private_message_=false; speech_.cancel(); audio_lock_.reset();
    request_session_=session; identified_request_=true; last_input_=clock_.monotonic_ms();
    return think();
}
Status Runtime::request_voice(const VoiceObservation& observation,std::string_view transcript,VoiceMatch& match) {
    match={};
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(cognition_.active()) return Status::fail(Error::busy,"inference already active");
    if(transcript.empty() || transcript.size()>=384) return Status::fail(Error::invalid,"invalid transcript");
    message_.assign("Verifico la voce."); private_message_=false; speech_.cancel(); audio_lock_.reset();
    if(auto status=identities_->observe(observation,match); !status) return status;
    if(match.state!=VoiceMatchState::matched && match.state!=VoiceMatchState::new_guest)
        return Status::fail(Error::denied,"voice identity unknown, ambiguous or blocked");
    return request(match.session,transcript);
}
Status Runtime::think() {
    if(resources_.pressured()) return Status::fail(Error::busy,"cognition deferred under memory pressure");
    SpeakerContext speaker;
    if(secured()) {
        if(!identified_request_) return Status::fail(Error::denied,"identity session required");
        if(auto authorized=identities_->context(request_session_,speaker); !authorized) { identified_request_=false; user_input_={}; return authorized; }
    }
    auto status = cognition_.begin(clock_.monotonic_ms(), environment_, settings_.cognition);
    if (!status) {
        if(!environment_.connected && settings_.cognition) inference_error(status);
        message_.assign(status.message.view()); user_input_={}; identified_request_=false; return status;
    }
    cognitive_memory_=resources_.reserve(MemoryCategory::cognitive,MemoryPool::external,budget::context_bytes+budget::response_bytes+sizeof(MemoryPage));
    cognitive_lock_=power_.acquire(ServiceId::cognition,Resource::cpu|Resource::bluetooth,clock_.monotonic_ms(),cognition_.limits().timeout_ms);
    if(!cognitive_memory_ || !cognitive_lock_) { cognition_.complete(clock_.monotonic_ms(),false); cancel_cognition(); return Status::fail(Error::busy,"cognitive resources unavailable"); }
    auto storage=power_.acquire(ServiceId::storage,mask(Resource::storage),clock_.monotonic_ms(),30'000);
    if(!storage) { cognition_.complete(clock_.monotonic_ms(),false); cancel_cognition(); return Status::fail(Error::busy,"retrieval wake lock unavailable"); }
    ++database_operations_;
    MemoryPage page; ToolPage results;
    if(!secured()) { (void)store_.memories({},0,page); (void)store_.tool_results(results); }
    else if(speaker.strength==IdentityStrength::unlocked && speaker.role!=UserRole::guest) {
        (void)store_.memories_for(speaker.id.view(),{},0,page);
        (void)store_.tool_results_for(speaker.id.view(),results);
    }
    auto policy=assistant_.tools();
    if(secured()) {
        policy.notes=policy.notes && (speaker.grants & tool_bit(ToolName::save_note));
        policy.reminders=policy.reminders && (speaker.grants & tool_bit(ToolName::set_reminder));
        policy.phone_alarms=policy.phone_alarms && (speaker.grants & tool_bit(ToolName::set_alarm));
        policy.phone_messages=policy.phone_messages && (speaker.grants & tool_bit(ToolName::send_message));
        policy.agent_tasks=policy.agent_tasks && (speaker.grants & tool_bit(ToolName::schedule_agent_task));
        policy.services &= speaker.grants;
    }
    status = context_.build(creature_,page,user_input_.view(),policy,results,assistant_.service_bindings(),nullptr,secured() ? &speaker : nullptr,agent_,environment_);
    const bool provider_attempted=static_cast<bool>(status);
    if (status) status = provider_.start(context_.context(), cognition_.limits().max_tokens);
    if (!status) {
        cognition_.complete(clock_.monotonic_ms(),false); cancel_cognition();
        if(provider_attempted) inference_error(status); else show_face(CreatureFace::internal_error);
        message_.assign(status.message.view()); user_input_={}; identified_request_=false; return status;
    }
    cognitive_lifecycle_.assign("streaming"); next_cognitive_poll_=clock_.monotonic_ms();
    remote_unreachable_=false; receiving_response_=false; inference_link_override_.reset();
    parser_.reset(); message_.assign("Thinking / mock coprocessor...");
    return {};
}
Status Runtime::schedule_agent_task(const AgentTaskRequest& request) {
    if(secured()) return Status::fail(Error::denied,"identity session required");
    if(!booted_) return Status::fail(Error::unavailable,"runtime not booted");
    const auto status=agents_.schedule(request,assistant_.tool_mask() & ~tool_bit(ToolName::schedule_agent_task));
    if(status) next_task_scan_ms_=0;
    return status;
}
Status Runtime::acknowledge_agent_result(std::string_view id) {
    if(secured()) return Status::fail(Error::denied,"unlocked owner required");
    const auto status=agents_.acknowledge(id);
    if(status) { if(shown_notice_.view()==id) { shown_notice_={}; agent_notice_={}; } next_task_scan_ms_=0; }
    return status;
}
Status Runtime::next_wake_at(std::int64_t& epoch) {
    epoch=-1; std::int64_t tasks=-1,tools=-1; AssistantEntry reminder;
    if(auto status=agents_.next_deadline(tasks); !status) return status;
    if(auto status=store_.next_tool_time(tools); !status) return status;
    if(auto status=store_.next_reminder(reminder); !status) return status;
    for(const auto at:{tasks,tools,reminder.id ? reminder.due_at : std::int64_t{-1}})
        if(at>=0) epoch=epoch<0 ? at : std::min(epoch,at);
    return {};
}
void Runtime::end_agent_failure(const Status& status, bool retry) {
    if(active_task_.request.id.view().empty()) return;
    const auto saved=retry ? agents_.retry(active_task_.request.id.view()) :
        agents_.fail(active_task_.request.id.view(),status.message.view().empty() ? "Incarico non completato." : status.message.view());
    if(!saved) tasks_recovered_=false;
    active_task_={}; next_task_scan_ms_=0;
}
void Runtime::poll_agent_tasks() {
    const auto mono=clock_.monotonic_ms(), wall=clock_.wall_seconds();
    if(mono<next_task_scan_ms_ && wall<next_task_scan_wall_) return;
    next_task_scan_ms_=mono+60'000; next_task_scan_wall_=wall+60;
    if(!tasks_recovered_) {
        if(cognition_.active()) return;
        const auto recovered=agents_.recover();
    tasks_recovered_=static_cast<bool>(recovered) || recovered.code==Error::unavailable || recovered.code==Error::unsupported;
        if(!tasks_recovered_) return;
    }
    AgentTask notice;
    if(agents_.notice(notice) && shown_notice_!=notice.request.id) {
        agent_notice_=notice; shown_notice_=notice.request.id;
        if(secured()) {
            agent_notice_.speech.assign("Incarico terminato. Sblocca il tuo profilo per leggere il risultato.");
            agent_notice_.request.instruction={}; message_=agent_notice_.speech;
        } else message_=notice.speech;
        show_face(notice.state==AgentTaskState::completed ? CreatureFace::happy : CreatureFace::sad);
        if(notice.request.wake_on_complete) {
            last_input_=mono;
            if(creature_.sleeping) (void)CreatureEngine::interact(creature_,EventType::rest);
        }
    }
    if(cognition_.active()) return;
    AgentTask task;
    const auto ready=agents_.next(task);
    if(ready) {
        // Due work retries eligibility without keeping inference active while offline.
        next_task_scan_ms_=mono+1000; next_task_scan_wall_=wall+1;
        if(!environment_.connected || !settings_.cognition || (environment_.battery<15 && !environment_.charging)) {
            const auto delay=std::min<std::int64_t>(30,task.request.expires_at-wall);
            next_task_scan_ms_=mono+delay*1000; next_task_scan_wall_=wall+delay; return;
        }
        auto status=cognition_.begin(mono,environment_,settings_.cognition,task.turn>0);
        if(!status) return;
        cognitive_memory_=resources_.reserve(MemoryCategory::cognitive,MemoryPool::external,budget::context_bytes+budget::response_bytes+sizeof(MemoryPage));
        cognitive_lock_=power_.acquire(ServiceId::cognition,Resource::cpu|Resource::bluetooth,mono,cognition_.limits().timeout_ms);
        if(!cognitive_memory_ || !cognitive_lock_) { cognition_.complete(mono,false); cancel_cognition(); return; }
        status=agents_.begin(task.request.id.view());
        if(!status) { cognition_.complete(mono,false); cancel_cognition(); return; }
        active_task_=task;
        MemoryPage memories; if(!secured()) (void)store_.memories({},0,memories);
        ToolPage results; status=agents_.results(task,results);
        auto policy=assistant_.tools(); const auto grants=task.request.grants;
        policy.notes=policy.notes && (grants & tool_bit(ToolName::save_note));
        policy.reminders=policy.reminders && (grants & tool_bit(ToolName::set_reminder));
        policy.phone_alarms=policy.phone_alarms && (grants & tool_bit(ToolName::set_alarm));
        policy.phone_messages=policy.phone_messages && (grants & tool_bit(ToolName::send_message));
        policy.services &= grants; policy.agent_tasks=false;
        if(status) status=context_.build(creature_,memories,task.request.instruction.view(),policy,results,assistant_.service_bindings(),&task,nullptr,agent_,environment_);
        const bool provider_attempted=static_cast<bool>(status);
        if(status) status=provider_.start(context_.context(),cognition_.limits().max_tokens);
        if(!status) {
            cognition_.complete(mono,false); cancel_cognition();
            if(provider_attempted) inference_error(status); else show_face(CreatureFace::internal_error);
            end_agent_failure(status,status.code==Error::io || status.code==Error::unavailable || status.code==Error::busy); return;
        }
        cognitive_lifecycle_.assign("streaming"); next_cognitive_poll_=clock_.monotonic_ms();
    remote_unreachable_=false; receiving_response_=false; inference_link_override_.reset();
        parser_.reset(); message_.assign("Sto lavorando all'incarico programmato. Il reparto memoria e' operativo.");
    } else {
        std::int64_t deadline=-1;
        if(agents_.next_deadline(deadline) && deadline>=0) {
            const auto delay=deadline<=wall ? 1 : std::min<std::int64_t>(60,deadline-wall);
            next_task_scan_ms_=mono+delay*1000; next_task_scan_wall_=wall+delay;
        }
        else { next_task_scan_ms_=std::numeric_limits<std::int64_t>::max(); next_task_scan_wall_=std::numeric_limits<std::int64_t>::max(); }
        // An expired/budget-exhausted task may have produced a notice in next().
        if(ready.code!=Error::unavailable || !task.request.id.view().empty()) next_task_scan_ms_=mono+1000;
    }
}
void Runtime::poll_cognition() {
    if(!cognition_.active()) return;
    const auto now=clock_.monotonic_ms();
    if(!active_task_.request.id.view().empty()) {
        AgentTask current;
        const auto loaded=agents_.get(active_task_.request.id.view(),current);
        if(!loaded || current.state!=AgentTaskState::inferring || clock_.wall_seconds()>=current.request.expires_at) {
            cancel_cognition(); cognition_.complete(now,false);
            if(loaded && (current.state==AgentTaskState::cancelled || current.state==AgentTaskState::failed)) active_task_={};
            else end_agent_failure(loaded ? Status::fail(Error::invalid,"Incarico scaduto durante l'inferenza.") : loaded,!loaded);
            user_input_={}; identified_request_=false; next_task_scan_ms_=0; return;
        }
    }
    if(!environment_.connected || !settings_.cognition || resources_.pressured() || !cognitive_lock_ || (environment_.battery<15 && !environment_.charging) || cognition_.timed_out(now)) {
        if(!environment_.connected || cognition_.timed_out(now)) remote_unreachable_=true;
        cancel_cognition(); cognition_.complete(now,false);
        end_agent_failure(Status::fail(Error::unavailable,"inferenza interrotta; incarico in attesa"),true);
        user_input_={}; identified_request_=false; message_.assign("Cognition cancelled. Instincts remain awake."); return;
    }
    std::array<char,128> chunk{}; std::size_t size=0; bool done=false;
    auto status=provider_.poll(chunk,size,done);
    if(status && size) receiving_response_=true;
    if(size>chunk.size()) status=Status::fail(Error::invalid,"provider returned invalid chunk size");
    if(status && !parser_.append({chunk.data(),size})) status=Status::fail(Error::full,"response budget exceeded");
    if(!status) {
        inference_error(status);
        cancel_cognition(); cognition_.complete(now,false); message_.assign(status.message.view());
        end_agent_failure(status,status.code==Error::io || status.code==Error::unavailable); user_input_={}; identified_request_=false; return;
    }
    if(!done) return;
    CognitiveResponse response; status=parser_.finish(response);
    const bool scheduled=!active_task_.request.id.view().empty();
    if(status && response.tool_count && !scheduled && user_input_.view().empty()) status=Status::fail(Error::denied,"tool calls require an explicit user request");
    if(status) status=ActionValidator::validate(response,creature_app_.capabilities());
    const auto calls=std::span<const ToolCall>{response.tool_calls.data(),response.tool_count};
    if(scheduled) {
        if(status && !calls.empty()) status=assistant_.validate_calls(calls,active_task_.request.grants);
        if(status) status=agents_.commit(active_task_,calls,response.speech.view());
        if(status) { assistant_.storage_changed(); active_task_={}; next_task_scan_ms_=0; }
        else end_agent_failure(status,status.code==Error::io || status.code==Error::busy);
    } else {
        SpeakerContext speaker;
        if(status && secured()) status=identities_->context(request_session_,speaker);
        if(status && response.tool_count) status=secured() ? assistant_.submit(request_session_,calls) : assistant_.submit(calls);
        if(status) status=executor_.execute(response,creature_app_.capabilities());
        if(status) {
            if(response.tool_count) message_.assign(assistant_.reply()); else if(!response.speech.view().empty()) message_=executor_.speech();
            if(secured() && speaker.strength==IdentityStrength::unlocked) { private_message_=true; message_session_=request_session_; }
            for(std::size_t i=0;i<response.action_count;++i)
                if(response.actions[i]==Capability::remember && response.schema_version==1 && !response.tool_count && (!secured() || (speaker.strength==IdentityStrength::unlocked && speaker.role!=UserRole::guest))) remember(response.speech.view().substr(0,383),"model-proposal",secured() ? speaker.id.view() : std::string_view{});
                else if(response.actions[i]==Capability::animate) {
                    if(const auto cue=executor_.face_cue()) show_face(*cue);
                }

            if(!response.tool_count && (!secured() || (speaker.strength==IdentityStrength::unlocked && speaker.role!=UserRole::guest)))
                for(std::size_t i=0;i<response.memory_count;++i) {
                    const auto& candidate=response.memory_candidates[i];
                    remember(candidate.content.view(),"model-proposal",secured() ? speaker.id.view() : std::string_view{},candidate.importance,candidate.valence);
                }
        }
    }
    if(!status) {
        // Transport errors were handled before decoding; failures here are local
        // validation/persistence/authorization, not proof the model is unreachable.
        show_face(status.code==Error::denied ? CreatureFace::sad : CreatureFace::internal_error);
        message_.assign(status.message.view());
    }
    cancel_cognition(); cognitive_lifecycle_.assign(status ? "completed" : "rejected");
    cognition_.complete(now,static_cast<bool>(status)); user_input_={}; identified_request_=false;
}
Status Runtime::schedule_agent_task(const UserSession& session,AgentTaskRequest request) {
    if(!booted_ || !identities_) return Status::fail(Error::unavailable,"runtime identity unavailable");
    SpeakerContext c; if(auto s=identities_->context(session,c); !s) return s;
    if(!(c.grants & tool_bit(ToolName::schedule_agent_task)) || (request.grants & ~c.grants))
        return Status::fail(Error::denied,"task exceeds identity grant");
    request.actor_id=c.id;
    const auto status=agents_.schedule(request,assistant_.tool_mask() & c.grants & ~tool_bit(ToolName::schedule_agent_task));
    if(status) next_task_scan_ms_=0;
    return status;
}
Status Runtime::agent_result(const UserSession& session,std::string_view id,AgentTask& task) {
    task={}; SpeakerContext c;
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto s=identities_->context(session,c); !s) return s;
    if(c.strength!=IdentityStrength::unlocked || c.role==UserRole::guest) return Status::fail(Error::denied,"unlock to read task results");
    AgentTask t; if(auto s=agents_.get(id,t); !s) return s;
    if(t.request.actor_id!=c.id && c.role!=UserRole::owner) return Status::fail(Error::denied,"task belongs to another identity");
    task=t; return {};
}
Status Runtime::acknowledge_agent_result(const UserSession& session,std::string_view id) {
    SpeakerContext c;
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto s=identities_->owner(session,c); !s) return s;
    const auto status=agents_.acknowledge(id);
    if(status) { if(shown_notice_.view()==id) { shown_notice_={}; agent_notice_={}; } next_task_scan_ms_=0; }
    return status;
}

void Runtime::cancel_cognition() { provider_.cancel(); cognitive_lock_.reset(); cognitive_memory_.reset(); cognitive_lifecycle_.assign("cancelled"); }
void Runtime::advance(std::int64_t now) {
    const auto elapsed=std::max<std::int64_t>(0,now-last_mono_);
    const auto health=creature_.needs.health;
    CreatureEngine::advance(creature_,elapsed);
    if (technical_sleep_) creature_.needs.health=std::max(health,creature_.needs.health);
    last_mono_=now; creature_.last_wall=std::max<std::int64_t>(0,clock_.wall_seconds());
    if (elapsed) { ++simulation_updates_; dirty_=true; }
}
void Runtime::coordinate_power() {
    const auto now=clock_.monotonic_ms();
    power_.update(environment_,std::max<std::int64_t>(0,now-last_input_),cognition_.active(),now);
    const bool sleep=!power_.display_awake();
    if (sleep && !suspended_) {
        sleep_started_=now; technical_sleep_=environment_.battery<=5 || !environment_.storage;
        // Persist continuity once before becoming quiescent, not at periodic sleep ticks.
        (void)flush(true);
        power_.update(environment_,std::max<std::int64_t>(0,now-last_input_),cognition_.active(),now);
    } else if (!sleep && suspended_) {
        last_sleep_ms_=std::max<std::int64_t>(0,now-sleep_started_); technical_sleep_=false;
    }
    suspended_=sleep;
}
std::int64_t Runtime::next_delay_ms() const {
    if (events_.pending()) return 0;
    const auto now=clock_.monotonic_ms();
    auto due=std::min(power_.next_deadline(),scheduler_.next_deadline());
    if (cognition_.active()) due=std::min(due,next_cognitive_poll_);
    due=std::min(due,assistant_.next_deadline_ms());
    if(!cognition_.active()) due=std::min(due,next_task_scan_ms_);
    if(clock_.wall_seconds()>=next_task_scan_wall_) due=std::min(due,now);
    if(speaking() || listening()) due=std::min(due,now+90);
    if(private_message_) due=std::min(due,now+1000);
    if (!suspended_) {
        if (face_cue_ && face_cue_started_+2500>now) due = std::min(due, face_cue_started_+2500);
        due=std::min(due,last_mono_+60'000-creature_.pending_ms);
        if (last_input_+60'000>now) due=std::min(due,last_input_+60'000);
        if (dirty_) due=std::min(due,last_save_+budget::save_interval_ms);
        due=std::min(due,last_checkpoint_+budget::checkpoint_interval_ms);
    }
    return due==std::numeric_limits<std::int64_t>::max() ? due : std::max<std::int64_t>(0,due-now);
}
Status Runtime::name(std::string_view name) {
    if (name.empty() || name.size()>31) return Status::fail(Error::invalid,"name must contain 1..31 bytes");
    for (const char c:name) if (static_cast<unsigned char>(c)<32 || c=='"' || c=='\\') return Status::fail(Error::invalid,"invalid identity name");
    if (!creature_.name.assign(name)) return Status::fail(Error::invalid,"invalid identity name");
    char greeting[96]{}; std::snprintf(greeting,sizeof(greeting),"Hello, keeper. I am %s.",creature_.name.c_str()); message_.assign(greeting);
    ++creature_.revision; ++view_revision_; dirty_=true; return {};
}
Status Runtime::backup(std::string_view path) {
    auto lock=power_.acquire(ServiceId::storage,mask(Resource::storage),clock_.monotonic_ms(),30'000);
    if (!lock) return Status::fail(Error::busy,"backup wake lock unavailable");
    auto result=flush();
    if (result) { ++database_operations_; result=store_.backup(path); }
    lock.reset(); coordinate_power(); return result;
}
}
