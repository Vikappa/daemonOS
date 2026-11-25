#pragma once
#include "daemon/types.hpp"
#include <span>

namespace daemonos {
// All calls are serialized on the runtime thread. load returns unavailable if empty.
struct StateStore {
    virtual ~StateStore() = default;
    virtual std::string_view backend_name() const { return "Local state"; }
    virtual Status load(CreatureState& state, Settings& settings) = 0;
    virtual Status save(const CreatureState& state, const Settings& settings, const Memory* memory = nullptr) = 0;
    virtual Status memories(std::string_view query, std::size_t offset, MemoryPage& page) = 0;
    // Explicit user notes/tasks are separate from autobiographical model proposals.
    virtual Status create_entry(AssistantEntry&) { return Status::fail(Error::unavailable, "assistant storage unsupported"); }
    virtual Status entries(EntryKind, std::string_view, std::size_t, AssistantPage& page) {
        page.size = 0; return Status::fail(Error::unavailable, "assistant storage unsupported");
    }
    virtual Status change_entry(std::int64_t, EntryChange, std::int64_t = 0) {
        return Status::fail(Error::unavailable, "assistant storage unsupported");
    }
    virtual Status next_reminder(AssistantEntry& entry) {
        entry = {}; return Status::fail(Error::unavailable, "assistant storage unsupported");
    }
    virtual Status enqueue_tools(std::span<const ToolCall>, bool, ToolMask = 0) { return Status::fail(Error::unavailable, "tool storage unsupported"); }
    virtual Status tool_jobs(std::int64_t, ToolPage& page) { page.size = 0; return Status::fail(Error::unavailable, "tool storage unsupported"); }
    virtual Status tool_job(std::string_view, ToolJob&) { return Status::fail(Error::unavailable, "tool storage unsupported"); }
    virtual Status tool_confirmation(ToolJob&) { return Status::fail(Error::unavailable,"tool storage unsupported"); }
    virtual Status next_tool_time(std::int64_t& at) { at=-1; return Status::fail(Error::unavailable, "tool storage unsupported"); }
    virtual Status tool_results(ToolPage& page) { page.size=0; return Status::fail(Error::unavailable, "tool storage unsupported"); }
    virtual Status update_tool(std::string_view, ToolState, ToolState, std::string_view) {
        return Status::fail(Error::unavailable, "tool storage unsupported");
    }
    virtual Status finish_tool(const ToolResult&, ToolState) { return Status::fail(Error::unavailable,"tool storage unsupported"); }
    virtual Status schedule_agent_task(const AgentTaskRequest&) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status agent_task(std::string_view, AgentTask&) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status agent_tasks(std::size_t, AgentTaskPage& page) { page.size=0; return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status ready_agent_task(std::int64_t, AgentTask&) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status begin_agent_turn(std::string_view, std::int64_t) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status commit_agent_turn(std::string_view, unsigned, std::span<const ToolCall>, std::string_view, std::int64_t) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status retry_agent_task(std::string_view) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status stop_agent_task(std::string_view, AgentTaskState, std::string_view) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status recover_agent_tasks() { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status agent_tool_results(std::string_view, unsigned, ToolPage&) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status agent_notice(AgentTask&) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status acknowledge_agent_notice(std::string_view) { return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    virtual Status next_agent_deadline(std::int64_t& at) { at=-1; return Status::fail(Error::unavailable,"agent scheduler unsupported"); }
    // Atomically create the local entry and mark its job successful, or neither.
    virtual Status execute_local_tool(std::string_view, std::int64_t) {
        return Status::fail(Error::unavailable, "tool storage unsupported");
    }
    // Actor-scoped paths are mandatory once the voice directory is initialized.
    virtual Status memories_for(std::string_view, std::string_view, std::size_t, MemoryPage& page) { page={}; return Status::fail(Error::unavailable,"scoped storage unsupported"); }
    virtual Status entries_for(std::string_view, EntryKind, std::string_view, std::size_t, AssistantPage& page) { page={}; return Status::fail(Error::unavailable,"scoped storage unsupported"); }
    virtual Status tool_results_for(std::string_view, ToolPage& page) { page={}; return Status::fail(Error::unavailable,"scoped storage unsupported"); }
    virtual Status enqueue_for(std::string_view, std::span<const ToolCall>, ToolMask) { return Status::fail(Error::unavailable,"scoped storage unsupported"); }
    virtual Status approve_tool(std::string_view, std::string_view, std::uint64_t) { return Status::fail(Error::unavailable,"approval storage unsupported"); }
    virtual Status backup(std::string_view destination) = 0;
    virtual void available(bool value) = 0;
    virtual Status trim() { return Status::fail(Error::unsupported,"cache trimming unavailable"); }
};
struct CheckpointStore {
    virtual ~CheckpointStore() = default;
    virtual Status load(CreatureState& state) = 0;
    virtual Status save(const CreatureState& state) = 0;
};
}
