#pragma once
#include "daemon/apps/modules.hpp"
#include "daemon/services/creature.hpp"
#include "daemon/services/cognitive.hpp"
#include "daemon/services/power.hpp"
#include "daemon/services/registry.hpp"
#include "daemon/services/scheduler.hpp"

namespace daemonos {
class Runtime {
public:
    Runtime(hal::Clock& clock, hal::Random& random, StateStore& store, CheckpointStore& checkpoint, InferenceProvider& provider);
    ~Runtime();
    Status boot();
    void tick();
    bool post(Event event) { return events_.post(event); }
    Status flush(bool force_checkpoint = false);
    Status backup(std::string_view path) {
        if (auto status = flush(); !status) return status;
        return store_.backup(path);
    }
    const CreatureState& creature() const { return creature_; }
    const Environment& environment() const { return environment_; }
    const Settings& settings() const { return settings_; }
    AppId app() const { auto* app = navigation_.foreground(); return app ? app->id() : AppId::launcher; }
    PowerState power() const { return power_.state(); }
    bool display_awake() const { return power_.display_awake(); }
    bool thinking() const { return cognition_.active(); }
    std::string_view message() const { return message_.view(); }
    std::string_view storage_status() const { return storage_status_.view(); }
    std::size_t dropped_events() const { return events_.dropped(); }
    std::size_t pending_memories() const { return pending_count_; }
    std::size_t lost_memories() const { return lost_memories_; }
    ServiceRegistry& services() { return services_; }
private:
    static void on_event(void* self, const Event& event) { static_cast<Runtime*>(self)->handle(event); }
    void handle(const Event& event);
    void think();
    void poll_cognition();
    void remember(std::string_view content, std::string_view provenance = "local");
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
    CognitiveScheduler cognition_{};
    ResponseParser parser_{};
    ContextBuilder context_{};
    CapabilityExecutor executor_{};
    std::array<Memory, 16> pending_{};
    std::size_t pending_count_{}, lost_memories_{};
    std::int64_t last_mono_{}, last_input_{}, last_save_{}, last_checkpoint_{};
    Text<512> message_{std::string_view{"Hello, keeper. I am Ember."}};
    Text<96> storage_status_{std::string_view{"Not initialized"}};
    bool booted_{};
};
}
