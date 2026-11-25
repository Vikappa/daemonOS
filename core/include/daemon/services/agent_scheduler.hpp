#pragma once
#include "daemon/hal/interfaces.hpp"
#include "daemon/services/store.hpp"
#include "daemon/services/voice_directory.hpp"

namespace daemonos {
Status validate_agent_task(const AgentTaskRequest& request);
// Persistent coordination only; Runtime lends its single inference provider per turn.
class AgentScheduler {
public:
    AgentScheduler(hal::Clock& clock, StateStore& store,VoiceDirectory* identities = nullptr) : clock_(clock), store_(store), identities_(identities) {}
    Status recover() { return store_.recover_agent_tasks(); }
    Status schedule(const AgentTaskRequest& request, ToolMask allowed);
    Status next(AgentTask& task);
    Status begin(std::string_view id) { return store_.begin_agent_turn(id,clock_.wall_seconds()); }
    Status commit(const AgentTask& task, std::span<const ToolCall> calls, std::string_view speech);
    Status retry(std::string_view id) { return store_.retry_agent_task(id); }
    Status fail(std::string_view id, std::string_view detail) { return store_.stop_agent_task(id,AgentTaskState::failed,detail); }
    Status cancel(std::string_view id) { return store_.stop_agent_task(id,AgentTaskState::cancelled,"Incarico annullato."); }
    Status get(std::string_view id, AgentTask& task) { return store_.agent_task(id,task); }
    Status list(std::size_t offset, AgentTaskPage& page) { return store_.agent_tasks(offset,page); }
    Status results(const AgentTask& task, ToolPage& page) { return store_.agent_tool_results(task.request.id.view(),task.turn,page); }
    Status notice(AgentTask& task) { return store_.agent_notice(task); }
    Status acknowledge(std::string_view id) { return store_.acknowledge_agent_notice(id); }
    Status next_deadline(std::int64_t& at) { return store_.next_agent_deadline(at); }
private:
    hal::Clock& clock_;
    StateStore& store_;
    VoiceDirectory* identities_{};
};
}
