#pragma once
#include "daemon/hal/interfaces.hpp"
#include "daemon/services/store.hpp"
#include "daemon/services/voice_directory.hpp"
#include "daemon/services/resources.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>

namespace daemonos::desktop {
class Allocator final : public MemoryAllocator {
public: void* allocate(std::size_t bytes,MemoryPool pool) override; void release(void* memory) override;
};
class VirtualClock final : public hal::Clock {
public:
    explicit VirtualClock(std::int64_t epoch);
    std::int64_t monotonic_ms() const override;
    std::int64_t wall_seconds() const override;
    void advance(std::int64_t milliseconds);
    void speed(double multiplier);
    double speed() const { return speed_; }
    void set_wall(std::int64_t epoch);
private:
    void synchronize();
    std::chrono::steady_clock::time_point origin_;
    std::int64_t virtual_ms_{};
    std::int64_t wall_origin_{};
    double speed_{1};
};
class Random final : public hal::Random {
public:
    Random();
    explicit Random(std::uint32_t seed) : generator_(seed) {}
    std::uint32_t next() override { return static_cast<std::uint32_t>(generator_()); }
private: std::mt19937 generator_;
};
class SqliteStore final : public StateStore, public IdentityStore {
public:
    std::string_view backend_name() const override { return "SQLite / local authority"; }
    explicit SqliteStore(const std::filesystem::path& path);
    ~SqliteStore() override;
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;
    Status load(CreatureState& state, Settings& settings) override;
    Status save(const CreatureState& state, const Settings& settings, const Memory* memory = nullptr) override;
    Status memories(std::string_view query, std::size_t offset, MemoryPage& page) override;
    Status create_entry(AssistantEntry& entry) override;
    Status entries(EntryKind kind, std::string_view query, std::size_t offset, AssistantPage& page) override;
    Status change_entry(std::int64_t id, EntryChange change, std::int64_t due_at = 0) override;
    Status next_reminder(AssistantEntry& entry) override;
    Status enqueue_tools(std::span<const ToolCall> calls, bool confirm_messages, ToolMask grants = 0) override;
    Status tool_jobs(std::int64_t now, ToolPage& page) override;
    Status tool_job(std::string_view id, ToolJob& job) override;
    Status tool_confirmation(ToolJob& job) override;
    Status next_tool_time(std::int64_t& at) override;
    Status tool_results(ToolPage& page) override;
    Status update_tool(std::string_view id, ToolState expected, ToolState next, std::string_view result) override;
    Status execute_local_tool(std::string_view id, std::int64_t now) override;
    Status finish_tool(const ToolResult& result, ToolState expected) override;
    Status schedule_agent_task(const AgentTaskRequest& request) override;
    Status agent_task(std::string_view id, AgentTask& task) override;
    Status agent_tasks(std::size_t offset, AgentTaskPage& page) override;
    Status ready_agent_task(std::int64_t now, AgentTask& task) override;
    Status begin_agent_turn(std::string_view id, std::int64_t now) override;
    Status commit_agent_turn(std::string_view id, unsigned turn, std::span<const ToolCall> calls, std::string_view speech, std::int64_t now) override;
    Status retry_agent_task(std::string_view id) override;
    Status stop_agent_task(std::string_view id, AgentTaskState state, std::string_view speech) override;
    Status recover_agent_tasks() override;
    Status agent_tool_results(std::string_view id, unsigned turn, ToolPage& results) override;
    Status agent_notice(AgentTask& task) override;
    Status acknowledge_agent_notice(std::string_view id) override;
    Status next_agent_deadline(std::int64_t& at) override;
    Status backup(std::string_view destination) override;
    void available(bool value) override { available_ = value; }
    Status identity_owner(VoiceProfile& profile) override;
    Status identity_profile(std::string_view id,VoiceProfile& profile) override;
    Status identity_profiles(std::size_t offset,VoiceProfilePage& page) override;
    Status identity_bootstrap(const VoiceProfile& profile) override;
    Status identity_update(const VoiceProfile& profile,std::uint64_t expected_revision) override;
    Status identity_enroll(const VoiceTemplate& voice) override;
    Status identity_template(std::string_view id,unsigned slot,VoiceTemplate& voice) override;
    Status identity_observe(const VoiceEncounter& encounter,const VoiceProfile* guest,const VoiceTemplate* voice) override;
    Status identity_forget(std::string_view id) override;
    Status identity_encounters(std::string_view id,std::size_t offset,VoiceEncounterPage& page) override;
    Status identity_capture_seen(std::string_view capture,bool& seen) override;
    Status identity_set_guest_tracking(bool enabled) override;
    Status identity_guest_tracking(bool& enabled) override;
    Status memories_for(std::string_view, std::string_view, std::size_t, MemoryPage& page) override;
    Status entries_for(std::string_view, EntryKind, std::string_view, std::size_t, AssistantPage& page) override;
    Status tool_results_for(std::string_view, ToolPage& page) override;
    Status enqueue_for(std::string_view, std::span<const ToolCall>, ToolMask) override;
    Status approve_tool(std::string_view, std::string_view, std::uint64_t) override;
    Status integrity();
    Status trim() override;
    static std::string_view version();
    static Status configure_heap(void* memory,std::size_t size);
    static Status shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool available_{true};
};
class FileCheckpoint final : public CheckpointStore {
public:
    explicit FileCheckpoint(std::filesystem::path path) : path_(std::move(path)) {}
    Status load(CreatureState& state) override;
    Status save(const CreatureState& state) override;
private: std::filesystem::path path_;
};
}
