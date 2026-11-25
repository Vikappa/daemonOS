#include "daemon/services/assistant.hpp"
#include <cstdio>

namespace daemonos {
Status validate_tool(const ToolCall& c) {
    if(!valid_identifier(c.id.view()) || tool_name(c.name).empty() || c.execute_at<0 || c.expires_at<0 || c.at<0 ||
        (c.expires_at && c.expires_at<=c.execute_at)) return Status::fail(Error::invalid,"invalid tool identity or deadline");
    const bool timed=c.name==ToolName::set_alarm || c.name==ToolName::set_reminder || c.name==ToolName::schedule_agent_task;
    if(timed ? c.at<=c.execute_at : c.at!=0) return Status::fail(Error::invalid,"invalid scheduled time");
    if(c.name==ToolName::email_read ? !c.text.view().empty() : c.text.view().empty()) return Status::fail(Error::invalid,"invalid tool text");
    const bool recipient=c.name==ToolName::send_message || c.name==ToolName::email_send;
    if(recipient ? !valid_identifier(c.recipient.view()) : !c.recipient.view().empty()) return Status::fail(Error::invalid,"invalid recipient reference");
    if(!c.binding.view().empty() && (!phone_tool(c.name) || !valid_identifier(c.binding.view()))) return Status::fail(Error::invalid,"invalid service binding");
    if(c.name==ToolName::email_send ? c.subject.view().empty() : !c.subject.view().empty()) return Status::fail(Error::invalid,"invalid email subject");
    const bool reference=c.name==ToolName::email_read || c.name==ToolName::notification_reply;
    if(reference ? !valid_identifier(c.reference.view()) : !c.reference.view().empty()) return Status::fail(Error::invalid,"invalid message/notification reference");
    if((phone_tool(c.name) && !c.expires_at) || (c.name==ToolName::schedule_agent_task && c.expires_at<=c.at))
        return Status::fail(Error::invalid,"service/task requires a valid expiry");
    return {};
}
ToolMask AssistantService::tool_mask() const {
    const auto policy=tools();
    ToolMask mask=(policy.notes ? tool_bit(ToolName::save_note) : 0) |
        (policy.reminders ? tool_bit(ToolName::set_reminder) : 0) |
        (policy.agent_tasks ? tool_bit(ToolName::schedule_agent_task) : 0) |
        (policy.phone_alarms ? tool_bit(ToolName::set_alarm) : 0) |
        (policy.phone_messages ? tool_bit(ToolName::send_message) : 0);
    return mask | (policy.services & bridge_tools);
}
Status AssistantService::validate_calls(std::span<const ToolCall> calls, ToolMask allowed) const {
    if(calls.empty() || calls.size()>budget::tool_calls) return Status::fail(Error::invalid,"tool call budget");
    const auto now=clock_.wall_seconds();
    if(now<0) return Status::fail(Error::invalid,"clock unavailable");
    for(std::size_t i=0;i<calls.size();++i) {
        const auto& c=calls[i];
        if(auto status=validate_tool(c); !status) return status;
        for(std::size_t j=0;j<i;++j) if(c.id==calls[j].id) return Status::fail(Error::invalid,"duplicate tool call id");
        const bool timed=c.name==ToolName::set_alarm || c.name==ToolName::set_reminder || c.name==ToolName::schedule_agent_task;
        if(c.execute_at>now+budget::max_reminder_seconds || c.at>now+budget::max_reminder_seconds ||
            c.expires_at>now+budget::max_reminder_seconds || (c.expires_at && c.expires_at<=now) || (timed && c.at<=now))
            return Status::fail(Error::invalid,"tool time expired or outside one-year horizon");
        if(!(tool_mask() & allowed & tool_bit(c.name)) ||
            (phone_tool(c.name) && (!phone_ || !phone_->supports_binding(c.name,c.binding.view()))))
            return Status::fail(Error::denied,"tool or binding not enabled for this request");
    }
    return {};
}
Status AssistantService::submit(std::span<const ToolCall> calls) {
    if(secured()) return Status::fail(Error::denied,"identity session required");
    return submit_impl(calls,nullptr);
}
Status AssistantService::submit(const UserSession& session,std::span<const ToolCall> calls) {
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    SpeakerContext c; if(auto status=identities_->context(session,c); !status) return status;
    for(const auto& call:calls) if(auto status=identities_->authorize(session,call); !status) return status;
    return submit_impl(calls,&c);
}
Status AssistantService::submit_impl(std::span<const ToolCall> calls,const SpeakerContext* actor) {
    if(auto status=validate_calls(calls,all_tools); !status) return status;
    const auto grants=tool_mask() & (actor ? actor->grants : all_tools);
    const auto status=actor ? store_.enqueue_for(actor->id.view(),calls,grants) : store_.enqueue_tools(calls,true,grants);
    if(!status) { tell("Non ho salvato la richiesta. La memoria oggi ha problemi anche qui."); return status; }
    tell("Richiesta salvata in coda. Eseguita e accodata sono due cose diverse, capo.");
    if(calls.size()==1) {
        ToolJob job;
        if(store_.tool_job(calls.front().id.view(),job)) {
            if(job.state==ToolState::succeeded) tell("Questa richiesta risulta gia' completata. Niente bis involontari.");
            else if(job.state==ToolState::needs_confirmation) {
                std::array<char,512> prompt{};
                std::snprintf(prompt.data(),prompt.size(),"Conferma invio a %s:\n%s\nRichiesta %s; esecuzione UTC Unix %lld.",actor ? "[privato]" : job.call.recipient.c_str(),actor ? "[privato]" : job.call.text.c_str(),job.call.id.c_str(),static_cast<long long>(job.call.execute_at));
                tell(prompt.data());
            } else if(job.state==ToolState::failed || job.state==ToolState::uncertain || job.state==ToolState::cancelled)
                tell("Richiesta gia' registrata con esito non riuscito, incerto o annullato. Non la ripeto.");
        }
    }
    storage_changed();
    return {};
}
Status AssistantService::confirm(std::string_view id) {
    if(secured()) return Status::fail(Error::denied,"unlocked owner required");
    return confirm_impl(id,nullptr);
}
Status AssistantService::confirm(const UserSession& session,std::string_view id) {
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    SpeakerContext c; if(auto status=identities_->owner(session,c); !status) return status;
    return confirm_impl(id,&c);
}
Status AssistantService::confirm_impl(std::string_view id,const SpeakerContext* owner) {
    ToolJob job;
    if (auto status = store_.tool_job(id, job); !status) return status;
    if (job.state != ToolState::needs_confirmation || !message_tool(job.call.name))
        return Status::fail(Error::invalid, "job is not awaiting message confirmation");
    if (job.call.expires_at <= clock_.wall_seconds()) return Status::fail(Error::invalid, "message request expired");
    if(owner) if(auto status=identities_->delegated(job.actor_id.view(),job.grants,&job.call); !status) return status;
    const auto revision=owner ? owner->revision : 0;
    auto status = owner ? store_.approve_tool(id,owner->id.view(),revision) :
        store_.update_tool(id, ToolState::needs_confirmation, ToolState::queued, "user confirmed exact recipient and message");
    if (status) { tell("Invio autorizzato; resta in coda fino alla scadenza e alla connessione del telefono."); storage_changed(); }
    return status;
}
Status AssistantService::cancel(std::string_view id) {
    if(secured()) return Status::fail(Error::denied,"unlocked owner required");
    return cancel_impl(id);
}
Status AssistantService::cancel(const UserSession& session,std::string_view id) {
    SpeakerContext c; if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto status=identities_->owner(session,c); !status) return status;
    return cancel_impl(id);
}
Status AssistantService::cancel_impl(std::string_view id) {
    ToolJob job;
    if (auto status = store_.tool_job(id, job); !status) return status;
    if (job.state != ToolState::queued && job.state != ToolState::needs_confirmation)
        return Status::fail(Error::denied, "an already dispatched action cannot be unsent");
    const auto status = store_.update_tool(id, job.state, ToolState::cancelled, "cancelled locally");
    if (status) tell("Annullato. Questa volta puoi dimenticartene sul serio.");
    return status;
}
Status AssistantService::complete_reminder(std::int64_t id, std::int64_t seconds) {
    if(secured()) return Status::fail(Error::denied,"unlocked owner required");
    return complete_reminder_impl(id,seconds);
}
Status AssistantService::complete_reminder(const UserSession& session,std::int64_t id,std::int64_t seconds) {
    SpeakerContext c; if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto status=identities_->owner(session,c); !status) return status;
    return complete_reminder_impl(id,seconds);
}
Status AssistantService::complete_reminder_impl(std::int64_t id,std::int64_t seconds) {
    if (seconds < 0 || seconds > budget::max_reminder_seconds) return Status::fail(Error::invalid, "invalid snooze interval");
    const auto status = store_.change_entry(id, seconds ? EntryChange::reschedule : EntryChange::complete,
                                           seconds ? clock_.wall_seconds() + seconds : 0);
    if (status) {
        if(reminder_.id==id) { alerting_ = false; reminder_ = {}; }
        storage_changed(); tell(seconds ? "Rimandato. La procrastinazione ora ha un orario." : "Fatto. Una cosa in meno da tenere a mente.");
    }
    return status;
}
void AssistantService::execute(ToolJob& job) {
    const auto& c = job.call;
    const auto now = clock_.wall_seconds();
    if (job.state == ToolState::dispatched) {
        // Expiry cannot tell us whether an already-dispatched effect happened.
        if (active_id_ != c.id || clock_.monotonic_ms() - dispatched_ms_ >= 10'000) {
            if (store_.update_tool(c.id.view(), ToolState::dispatched, ToolState::uncertain, "delivery outcome unknown; never replay automatically")) {
                tell("Il telefono non ha confermato l'esito. Non ripeto l'azione: potrei duplicarla.");
                if (active_id_ == c.id) active_id_ = {};
            }
        }
        return;
    }
    if ((c.expires_at && now >= c.expires_at) ||
        (c.name == ToolName::set_alarm && now >= c.at)) {
        if (store_.update_tool(c.id.view(), job.state, ToolState::failed, "deadline expired; action was not executed"))
            tell("Richiesta scaduta: non l'ho eseguita in ritardo di nascosto.");
        return;
    }
    if(secured()) {
        auto authorized=identities_->delegated(job.actor_id.view(),job.grants,&c);
        if(authorized && message_tool(c.name) && job.state==ToolState::queued)
            authorized=identities_->approval(job.approved_by.view(),job.approval_revision);
        if(!authorized) {
            (void)store_.update_tool(c.id.view(),job.state,ToolState::failed,"identity grant or owner approval revoked");
            tell("Permessi mancanti o revocati. Richiesta fermata."); return;
        }
    }
    if (job.state == ToolState::needs_confirmation) return;
    if (c.execute_at > now) return;
    if (!phone_tool(c.name)) {
        if(!(tool_mask() & tool_bit(c.name))) return;
        const auto status = store_.execute_local_tool(c.id.view(), now);
        if (status) {
            tell(c.name==ToolName::save_note ?
                "Appunto salvato. La tua memoria dura meno della mia batteria. Ma almeno io mi ricarico." :
                c.name==ToolName::schedule_agent_task ? "Incarico programmato. Mi sveglio io: tu conserva le energie per dimenticartelo." :
                "Promemoria impostato. Alla scadenza ti avviso io, visto il reparto memoria.");
        } else tell("Non ho completato il salvataggio; la richiesta resta in coda.");
        return;
    }
    if (!phone_ || !phone_->authenticated() || !phone_->supports_binding(c.name,c.binding.view()) || !active_id_.view().empty()) return;
    if(!(tool_mask() & tool_bit(c.name))) return;
    // Persist before crossing the device boundary. A dispatch failure may have delivered bytes.
    if (!store_.update_tool(c.id.view(), ToolState::queued, ToolState::dispatched, "awaiting phone execution result")) return;
    active_id_ = c.id; dispatched_ms_ = clock_.monotonic_ms();
    const auto status = phone_->dispatch(c);
    tell(status ? "Richiesta inoltrata al telefono. Aspetto il risultato." : "Invio al telefono interrotto; esito da verificare, nessuna ripetizione automatica.");
    if (!status && store_.update_tool(c.id.view(), ToolState::dispatched, ToolState::uncertain, "transport failed after dispatch boundary")) active_id_ = {};
}
void AssistantService::tick() {
    if (phone_ && phone_->authenticated()) {
        // A bounded poll loop; duplicates and unsolicited results do not change unrelated jobs.
        for (std::size_t i = 0; i < budget::tool_calls; ++i) {
            ToolResult result;
            if (!phone_->poll(result)) break;
            if(!validate_service_result(result)) { phone_->accept_result(result.id.view()); continue; }
            ToolJob job;
            const auto loaded=store_.tool_job(result.id.view(),job);
            if(!loaded) {
                if(loaded.code!=Error::unavailable) break; // Preserve the result across storage faults.
                phone_->accept_result(result.id.view()); continue;
            }
            if(!phone_tool(job.call.name) || (job.state!=ToolState::dispatched && job.state!=ToolState::uncertain)) {
                phone_->accept_result(result.id.view()); continue;
            }
            if (store_.finish_tool(result,job.state)) {
                phone_->accept_result(result.id.view());
                if (active_id_ == result.id) active_id_ = {};
                tell(!result.success ? "Il servizio segnala che l'azione non e' riuscita." :
                    message_tool(job.call.name) ? "Il servizio conferma: messaggio inviato." :
                    job.call.name==ToolName::set_alarm ? "Il telefono conferma: sveglia impostata." : "Risultati ricevuti dal servizio.");
            } else break;
        }
    }
    const auto now_ms = clock_.monotonic_ms();
    if (now_ms < next_scan_ms_ && clock_.wall_seconds() < next_scan_wall_) return;
    next_scan_ms_ = now_ms + 60'000;
    next_scan_wall_=clock_.wall_seconds()+60;
    ToolPage page;
    if (store_.tool_jobs(clock_.wall_seconds(), page))
        for (std::size_t i = 0; i < page.size; ++i) execute(page.items[i]);
    AssistantEntry due;
    const auto reminder_status=store_.next_reminder(due);
    if(!reminder_status) { pending_work_=reminder_status.code!=Error::unavailable && reminder_status.code!=Error::unsupported; return; }
    if (due.id && due.due_at <= clock_.wall_seconds() && (!alerting_ || due.id != reminder_.id)) {
        reminder_ = due; alerting_ = true;
        std::array<char, 512> text{};
        std::snprintf(text.data(), text.size(), "Promemoria: %s\nIo me lo sono ricordato. Siamo gia' a meta' dell'opera.", secured() ? "Hai un promemoria privato. Sblocca il tuo profilo per leggerlo." : due.content.c_str());
        tell(text.data());
    }
    const auto wall=clock_.wall_seconds();
    const auto schedule_scan = [&](std::int64_t at) {
        const auto seconds=at<=wall ? 1 : std::min<std::int64_t>(60,at-wall);
        next_scan_ms_=std::min(next_scan_ms_,now_ms+seconds*1000);
        next_scan_wall_=std::min(next_scan_wall_,wall+seconds);
    };
    std::int64_t work{};
    const auto tools=store_.next_tool_time(work);
    pending_work_=(!tools && tools.code!=Error::unavailable && tools.code!=Error::unsupported) || work>=0 || (due.id && !alerting_);
    if(tools && work>=0) schedule_scan(work);
    if(due.id && !alerting_) schedule_scan(due.due_at);
}
Status AssistantService::notes(const UserSession& session,std::string_view query,std::size_t offset,AssistantPage& page) {
    page={}; SpeakerContext c;
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto s=identities_->context(session,c); !s) return s;
    if(c.strength!=IdentityStrength::unlocked || c.role==UserRole::guest) return Status::fail(Error::denied,"unlock to read private notes");
    return store_.entries_for(c.id.view(),EntryKind::note,query,offset,page);
}
Status AssistantService::confirmation(const UserSession& session,ToolJob& job) {
    job={}; SpeakerContext c;
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto s=identities_->owner(session,c); !s) return s;
    return store_.tool_confirmation(job);
}
Status AssistantService::job(const UserSession& session,std::string_view id,ToolJob& value) {
    value={}; SpeakerContext c;
    if(!identities_) return Status::fail(Error::denied,"identity directory unavailable");
    if(auto s=identities_->context(session,c); !s) return s;
    if(c.strength!=IdentityStrength::unlocked || c.role==UserRole::guest) return Status::fail(Error::denied,"unlock to read private jobs");
    ToolJob j; if(auto s=store_.tool_job(id,j); !s) return s;
    if(c.role!=UserRole::owner && j.actor_id!=c.id) return Status::fail(Error::denied,"job belongs to another identity");
    value=j; return {};
}

}
