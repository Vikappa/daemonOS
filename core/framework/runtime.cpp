#include "daemon/framework/runtime.hpp"
#include <limits>
#include <cstdio>

namespace daemonos {
Runtime::Runtime(hal::Clock& clock, hal::Random& random, StateStore& store, CheckpointStore& checkpoint, InferenceProvider& provider,ResourceManager* resources)
    : clock_(clock), random_(random), store_(store), checkpoint_(checkpoint), provider_(provider),resources_(resources ? *resources : local_resources_) {
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
    coordinate_power();
    return {};
}
void Runtime::tick() {
    if (!booted_) return;
    const auto now = clock_.monotonic_ms();
    if (events_.pending()==0 && next_delay_ms()>0) return;
    if (events_.pending() || now-last_mono_>=60'000-creature_.pending_ms) advance(now);
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
    coordinate_power();
    if (!suspended_ && dirty_ && now-last_save_>=budget::save_interval_ms) { (void)flush(); last_save_=now; }
    if (!suspended_ && now-last_checkpoint_>=budget::checkpoint_interval_ms) {
        if (checkpoint_.save(creature_)) last_checkpoint_ = now;
    }
    ++view_revision_;
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
        switch (event.type) {
        case EventType::feed: message_.assign("Warm sparks. My favorite snack."); remember("My keeper fed me warm sparks."); break;
        case EventType::rest: message_.assign(creature_.sleeping ? "Recharging my inner fire..." : "Awake. What shall we discover?"); remember(creature_.sleeping ? "I settled down to rest." : "I woke up with my keeper."); break;
        case EventType::pet: message_.assign("A small kindness. I will remember."); remember("My keeper showed me affection."); break;
        case EventType::move:
            environment_.steps += static_cast<std::uint32_t>(std::clamp<std::int32_t>(event.value, 0, 10000));
            message_.assign("Movement! There is a world out there."); remember("We moved and explored together."); break;
        default: break;
        }
        // Debounced on the persistence cadence, keeping touch bursts off storage.
    }
    switch (event.type) {
    case EventType::battery: environment_.battery = static_cast<int>(std::clamp<std::int32_t>(event.value, 0, 100)); break;
    case EventType::charging: environment_.charging = event.value != 0; break;
    case EventType::connectivity: environment_.connected = event.value != 0; break;
    case EventType::storage:
        environment_.storage = event.value != 0; store_.available(environment_.storage);
        if (environment_.storage) (void)flush();
        else storage_status_.assign("Storage fault / RAM + checkpoint");
        break;
    case EventType::open_app:
        if (event.value >= 0 && event.value <= 3) navigation_.push(static_cast<AppId>(event.value));
        break;
    case EventType::back: navigation_.back(); break;
    case EventType::home: navigation_.home(); break;
    case EventType::think: think(); break;
    case EventType::toggle_cognition: settings_.cognition = event.value != 0; break;
    case EventType::brightness: settings_.brightness = static_cast<int>(std::clamp<std::int32_t>(event.value, 10, 100)); break;
    case EventType::suspend: last_input_ = now - 60'000; break;
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
void Runtime::remember(std::string_view content, std::string_view provenance,double importance,double valence) {
    if (pending_count_ == pending_.size()) { ++lost_memories_; return; }
    auto& memory = pending_[pending_count_++];
    memory = {}; memory.timestamp = creature_.last_wall;
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
void Runtime::think() {
    if (resources_.pressured()) { message_.assign("Cognition deferred / memory pressure"); return; }
    auto status = cognition_.begin(clock_.monotonic_ms(), environment_, settings_.cognition);
    if (!status) { message_.assign(status.message.view()); return; }
    cognitive_memory_=resources_.reserve(MemoryCategory::cognitive,MemoryPool::external,budget::context_bytes+budget::response_bytes+sizeof(MemoryPage));
    cognitive_lock_=power_.acquire(ServiceId::cognition,Resource::cpu|Resource::bluetooth,clock_.monotonic_ms(),cognition_.limits().timeout_ms);
    if (!cognitive_memory_ || !cognitive_lock_) { cognition_.complete(clock_.monotonic_ms(),false); cancel_cognition(); message_.assign("Cognitive resources unavailable"); return; }
    MemoryPage page;
    MemoryRetriever retriever(store_);
    auto storage_lock=power_.acquire(ServiceId::storage,mask(Resource::storage),clock_.monotonic_ms(),30'000);
    if (!storage_lock) { cognition_.complete(clock_.monotonic_ms(),false); cancel_cognition(); message_.assign("Memory retrieval wake lock unavailable"); return; }
    power_.update(environment_,std::max<std::int64_t>(0,clock_.monotonic_ms()-last_input_),true,clock_.monotonic_ms());
    ++database_operations_;
    (void)retriever.retrieve({}, page);
    storage_lock.reset();
    status = context_.build(creature_, page,agent_,environment_,{EventType::think});
    if (status) status = provider_.start(context_.context(), cognition_.limits().max_tokens);
    if (!status) { cognition_.complete(clock_.monotonic_ms(), false); cancel_cognition(); message_.assign(status.message.view()); return; }
    cognitive_lifecycle_.assign("streaming"); next_cognitive_poll_=clock_.monotonic_ms();
    parser_.reset(); message_.assign("Thinking / mock coprocessor...");
}
void Runtime::poll_cognition() {
    if (!cognition_.active()) return;
    const auto now = clock_.monotonic_ms();
    if (!environment_.connected || !settings_.cognition || resources_.pressured() || !cognitive_lock_ || (environment_.battery < 15 && !environment_.charging) || cognition_.timed_out(now)) {
        cancel_cognition(); cognition_.complete(now, false); cognitive_lifecycle_.assign("cancelled");
        message_.assign("Cognition cancelled. Instincts remain awake."); return;
    }
    std::array<char, 128> chunk{}; std::size_t size = 0; bool done = false;
    auto status = provider_.poll(chunk, size, done);
    if (size > chunk.size()) status = Status::fail(Error::invalid, "provider returned invalid chunk size");
    if (status && !parser_.append({chunk.data(), size})) status = Status::fail(Error::full, "response budget exceeded");
    if (!status) { cancel_cognition(); cognition_.complete(now, false); cognitive_lifecycle_.assign("failed"); message_.assign(status.message.view()); return; }
    if (done) {
        CognitiveResponse response;
        status = parser_.finish(response);
        if (status) status = executor_.execute(response, creature_app_.capabilities());
        if (status) {
            if (!response.speech.view().empty()) message_ = executor_.speech();
            for (std::size_t i = 0; i < response.action_count; ++i)
                if (response.actions[i] == Capability::remember && response.schema_version==1) remember(response.speech.view().substr(0, 383), "model-proposal");
            for (std::size_t i=0;i<response.memory_count;++i) {
                const auto& candidate=response.memory_candidates[i];
                remember(candidate.content.view(),"model-proposal",candidate.importance,candidate.valence);
            }
        } else message_.assign(status.message.view());
        cognition_.complete(now, static_cast<bool>(status));
        cancel_cognition(); cognitive_lifecycle_.assign(status ? "completed" : "rejected");
    }
}
void Runtime::cancel_cognition() { provider_.cancel(); cognitive_lock_.reset(); cognitive_memory_.reset(); }
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
    if (!suspended_) {
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
