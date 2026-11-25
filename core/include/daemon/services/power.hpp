#pragma once
#include "daemon/types.hpp"
#include <utility>

namespace daemonos {
enum class Resource : std::uint16_t { cpu=1, display=2, audio=4, bluetooth=8, wifi=16, storage=32, sensors=64, scheduled=128, application=256 };
using ResourceMask = std::uint16_t;
constexpr ResourceMask mask(Resource resource) { return static_cast<ResourceMask>(resource); }
constexpr ResourceMask operator|(Resource a, Resource b) { return mask(a) | mask(b); }
enum class ServiceId : std::uint8_t { display, audio, bluetooth, wifi, storage, sensors, scheduler, applications, cognition, count };
struct PowerPlan {
    PowerState state{PowerState::interactive};
    ResourceMask enabled{};
    int max_cpu_mhz{240};
    bool awake{};
    bool operator==(const PowerPlan&) const = default;
};
struct ServicePower {
    Text<24> name{};
    ResourceMask resources{};
    std::int64_t max_lock_ms{30'000};
    Status (*apply)(void*, bool){};
    void* context{};
    bool enabled{};
    Error last_error{Error::none};
};
class PowerManager {
public:
    struct LockInfo {
        ServiceId service{};
        ResourceMask resources{};
        std::int64_t acquired{}, deadline{};
        std::uint32_t generation{};
        bool blocking{}, active{};
        PowerState activity{PowerState::interactive};
    };
    class WakeLock {
    public:
        WakeLock() noexcept : owner_(nullptr),slot_(0),generation_(0) {}
        ~WakeLock() { reset(); }
        WakeLock(const WakeLock&) = delete;
        WakeLock& operator=(const WakeLock&) = delete;
        WakeLock(WakeLock&& other) noexcept : owner_(std::exchange(other.owner_,nullptr)),slot_(other.slot_),generation_(other.generation_) {}
        WakeLock& operator=(WakeLock&& other) noexcept {
            if (this != &other) { reset(); owner_=std::exchange(other.owner_,nullptr); slot_=other.slot_; generation_=other.generation_; }
            return *this;
        }
        void reset();
        explicit operator bool() const;
    private:
        friend class PowerManager;
        PowerManager* owner_{};
        std::size_t slot_{};
        std::uint32_t generation_{};
    };
    PowerManager();
    bool bind(ServiceId id, Status (*apply)(void*,bool), void* context);
    WakeLock acquire(ServiceId service, ResourceMask resources, std::int64_t now, std::int64_t duration_ms, bool blocking=true,PowerState activity=PowerState::interactive);
    PowerState update(const Environment& environment, std::int64_t idle_ms, bool thinking, std::int64_t now=0);
    void allow(Resource resource, bool enabled);
    PowerState state() const { return plan_.state; }
    const PowerPlan& plan() const { return plan_; }
    bool display_awake() const { return plan_.awake; }
    const auto& locks() const { return locks_; }
    const auto& services() const { return services_; }
    std::size_t active_locks() const;
    std::size_t expired_locks() const { return expired_; }
    std::string_view last_expired_owner() const { return last_expired_.view(); }
    std::int64_t next_deadline() const;
private:
    std::array<ServicePower,static_cast<std::size_t>(ServiceId::count)> services_{};
    std::array<LockInfo,16> locks_{};
    PowerPlan plan_{};
    ResourceMask allowed_{0xffff};
    std::uint32_t generation_{};
    std::size_t expired_{};
    Text<24> last_expired_{};
};
}
