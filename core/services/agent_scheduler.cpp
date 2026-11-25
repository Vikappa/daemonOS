#include "daemon/services/agent_scheduler.hpp"
#include "daemon/services/assistant.hpp"
#include <cstdio>

namespace daemonos {
Status validate_agent_task(const AgentTaskRequest& r) {
    if(!valid_identifier(r.id.view()) || r.instruction.view().empty() || r.execute_at<0 || r.expires_at<=r.execute_at ||
        (r.grants & ~all_tools) || (r.grants & tool_bit(ToolName::schedule_agent_task)))
        return Status::fail(Error::invalid,"invalid agent task, deadline or grants");
    return {};
}
Status AgentScheduler::schedule(const AgentTaskRequest& request, ToolMask allowed) {
    if(auto status=validate_agent_task(request); !status) return status;
    const auto now=clock_.wall_seconds();
    if(request.execute_at<now || request.expires_at>now+budget::max_reminder_seconds)
        return Status::fail(Error::invalid,"agent task outside scheduling horizon");
    if(request.grants & ~allowed) return Status::fail(Error::denied,"task requests unavailable capabilities");
    return store_.schedule_agent_task(request);
}
Status AgentScheduler::next(AgentTask& task) {
    task={};
    if(auto status=store_.ready_agent_task(clock_.wall_seconds(),task); !status) return status;
    if(task.request.expires_at<=clock_.wall_seconds()) {
        (void)fail(task.request.id.view(),"Incarico scaduto prima di poter completare il lavoro.");
        return Status::fail(Error::unavailable,"agent task expired");
    }
    if(task.turn>=budget::agent_turns || task.attempts>=budget::agent_attempts) {
        (void)fail(task.request.id.view(),"Incarico fermato: raggiunto il limite di tentativi o passaggi.");
        return Status::fail(Error::unavailable,"agent task budget exhausted");
    }
    if(identities_ && identities_->enforced()) {
        const auto authorized=identities_->delegated(task.request.actor_id.view(),task.request.grants);
        if(!authorized) { (void)fail(task.request.id.view(),"Permessi revocati: incarico fermato."); return authorized; }
    }
    return {};
}
Status AgentScheduler::commit(const AgentTask& task, std::span<const ToolCall> calls, std::string_view speech) {
    if(identities_ && identities_->enforced()) {
        if(auto status=identities_->delegated(task.request.actor_id.view(),task.request.grants); !status) return status;
        for(const auto& call:calls) if(auto status=identities_->delegated(task.request.actor_id.view(),task.request.grants,&call); !status) return status;
    }
    if(clock_.wall_seconds()>=task.request.expires_at) return Status::fail(Error::invalid,"agent task deadline expired");
    if(calls.size()>budget::tool_calls || (calls.empty() && speech.empty()) || speech.size()>=512 ||
        (!calls.empty() && task.turn+1>=budget::agent_turns)) return Status::fail(Error::full,"agent response exceeds turn or output budget");
    std::array<ToolCall,budget::tool_calls> normalized{};
    for(std::size_t i=0;i<calls.size();++i) {
        normalized[i]=calls[i]; auto& call=normalized[i];
        if(!(task.request.grants & tool_bit(call.name)) || call.name==ToolName::schedule_agent_task ||
            call.execute_at>=task.request.expires_at || (call.expires_at && call.expires_at>task.request.expires_at))
            return Status::fail(Error::denied,"tool exceeds scheduled task grant or deadline");
        std::array<char,40> id{};
        const auto length=std::snprintf(id.data(),id.size(),"task-%lld-%u-%zu",static_cast<long long>(task.serial),task.turn+1,i);
        if(length<0 || static_cast<std::size_t>(length)>=id.size() || !call.id.assign(id.data())) return Status::fail(Error::full,"task operation id budget");
        if(!call.expires_at) call.expires_at=task.request.expires_at;
        if(auto status=validate_tool(call); !status) return status;
    }
    return store_.commit_agent_turn(task.request.id.view(),task.turn,{normalized.data(),calls.size()},speech,clock_.wall_seconds());
}
}
