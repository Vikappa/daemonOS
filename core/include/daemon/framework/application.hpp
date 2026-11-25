#pragma once
#include "daemon/types.hpp"
#include "daemon/services/event_bus.hpp"

namespace daemonos {
enum class Capability : std::uint8_t { animate = 1, speak = 2, remember = 4, interact = 8, propose_personality = 16, propose_intention = 32 };
using Capabilities = std::uint8_t;
constexpr Capabilities operator|(Capability a, Capability b) {
    return static_cast<Capabilities>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
struct CapabilityRegistry {
    static constexpr std::array entries{Capability::animate,Capability::speak,Capability::remember,Capability::interact,Capability::propose_personality,Capability::propose_intention};
    static bool permits(Capabilities granted,Capability requested) {
        for (const auto known:entries) if (known==requested) return (granted & static_cast<Capabilities>(requested))!=0;
        return false;
    }
};
struct Application {
    virtual ~Application() = default;
    virtual AppId id() const = 0;
    virtual std::string_view title() const = 0;
    virtual Capabilities capabilities() const { return 0; }
    virtual void start() {}
    virtual void resume() {}
    virtual void pause() {}
    virtual void stop() {}
    virtual void event(const Event&) {}
};
class ApplicationRegistry {
public:
    bool add(Application& app);
    Application* get(AppId id) const;
private:
    std::array<Application*, 8> apps_{};
};
class Navigation {
public:
    explicit Navigation(ApplicationRegistry& registry) : registry_(registry) {}
    ~Navigation();
    bool push(AppId id);
    bool back();
    void home();
    Application* foreground() const;
    std::size_t depth() const { return size_; }
    void suspend(bool suspended);
    bool suspended() const { return suspended_; }
private:
    ApplicationRegistry& registry_;
    std::array<Application*, 8> stack_{};
    std::size_t size_{};
    bool transitioning_{};
    bool suspended_{};
};
}
