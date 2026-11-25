#pragma once
#include "daemon/apps/modules.hpp"
#include "daemon/services/creature.hpp"
#include "daemon/services/cognitive.hpp"
#include "daemon/services/power.hpp"
#include "daemon/services/registry.hpp"
#include "daemon/services/scheduler.hpp"
#include "daemon/services/resources.hpp"
#include <optional>

namespace daemonos {
class Runtime {
public:
    Runtime(hal::Clock& clock, hal::Random& random, StateStore& store, CheckpointStore& checkpoint, InferenceProvider& provider, ResourceManager* resources=nullptr);
    ~Runtime();
    Status boot();
    void tick();
    bool post(Event event) { return events_.post(event); }
    Status flush(bool force_checkpoint = false);
    Status backup(std::string_view path);
    const CreatureState& creature() const { return creature_; }
    const Environment& environment() const { return environment_; }
    const Settings& settings() const { return settings_; }
    AppId app() const { auto* app = navigation_.foreground(); return app ? app->id() : AppId::launcher; }
    PowerState power() const { return power_.state(); }
    bool display_awake() const { return power_.display_awake(); }
    CreatureFace face() const;
    bool thinking() const { return cognition_.active(); }
    std::string_view message() const { return message_.view(); }
    std::string_view storage_status() const { return storage_status_.view(); }
    std::size_t dropped_events() const { return events_.dropped(); }
    std::size_t pending_memories() const { return pending_count_; }
    std::size_t lost_memories() const { return lost_memories_; }
    ServiceRegistry& services() { return services_; }
    ResourceManager& resources() { return resources_; }
    const ResourceManager& resources() const { return resources_; }
    PowerManager& power_manager() { return power_; }
    Scheduler& scheduler() { return scheduler_; }
    const PowerManager& power_manager() const { return power_; }
    std::int64_t next_delay_ms() const;
    std::uint64_t view_revision() const { return view_revision_; }
    std::size_t event_queue_size() const { return events_.pending(); }
    std::size_t event_queue_peak() const { return events_.peak(); }
    std::size_t simulation_updates() const { return simulation_updates_; }
    std::size_t database_operations() const { return database_operations_; }
    bool applications_suspended() const { return navigation_.suspended(); }
    std::string_view last_wake_reason() const { return wake_reason_.view(); }
    std::int64_t last_sleep_ms() const { return last_sleep_ms_; }
    std::string_view cognitive_lifecycle() const { return cognitive_lifecycle_.view(); }
    Status name(std::string_view name);
    Status configure_agent(std::string_view instructions) {
        return !instructions.empty() && agent_.instructions.assign(instructions) ? Status{} : Status::fail(Error::invalid,"agent.md must contain 1..1023 bytes without NUL");
    }
private:
    static void on_event(void* self, const Event& event) { static_cast<Runtime*>(self)->handle(event); }
    void handle(const Event& event);
    void think();
    void poll_cognition();
    void show_face(CreatureFace face);
    void remember(std::string_view content, std::string_view provenance = "local",double importance=0.5,double valence=0);
    void advance(std::int64_t now);
    void coordinate_power();
    void cancel_cognition();
    hal::Clock& clock_;
    hal::Random& random_;
    StateStore& store_;
    CheckpointStore& checkpoint_;
    InferenceProvider& provider_;
    CreatureState creature_{};
    Settings settings_{};
    Environment environment_{};
    EventBus events_{};
    EventBus::Subscription subscription_{};
    ServiceRegistry services_{};
    LauncherApp launcher_{};
    ClockApp clock_app_{};
    SettingsApp settings_app_{};
    CreatureApp creature_app_{};
    ApplicationRegistry applications_{};
    Navigation navigation_{applications_};
    PowerManager power_{};
    Scheduler scheduler_{};
    ResourceManager local_resources_{};
    ResourceManager& resources_;
    ResourceManager::Lease runtime_memory_{},cognitive_memory_{};
    PowerManager::WakeLock cognitive_lock_{};
    CognitiveScheduler cognition_{};
    ResponseParser parser_{};
    ContextBuilder context_{};
    CapabilityExecutor executor_{};
    AgentConfiguration agent_{};
    std::array<Memory, 16> pending_{};
    std::size_t pending_count_{}, lost_memories_{};
    std::int64_t last_mono_{}, last_input_{}, last_save_{}, last_checkpoint_{};
    std::int64_t next_cognitive_poll_{},sleep_started_{},last_sleep_ms_{};
    std::optional<CreatureFace> face_cue_{};
    std::int64_t face_cue_until_{};
    std::uint64_t view_revision_{};
    std::size_t simulation_updates_{},database_operations_{};
    Text<32> wake_reason_{std::string_view{"boot"}},cognitive_lifecycle_{std::string_view{"idle"}};
    Text<512> message_{std::string_view{"Hello, keeper."}};
    Text<96> storage_status_{std::string_view{"Not initialized"}};
    bool booted_{};
    bool suspended_{},technical_sleep_{},dirty_{};
};
}
