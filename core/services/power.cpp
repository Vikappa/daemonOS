#include "daemon/services/power.hpp"
#include <limits>

namespace daemonos {
PowerManager::PowerManager() {
    services_ = {{
        {Text<24>{"display"},mask(Resource::display),1000},
        {Text<24>{"audio"},mask(Resource::audio)|mask(Resource::cpu),120'000},
        {Text<24>{"bluetooth"},mask(Resource::bluetooth),120'000},
        {Text<24>{"wifi"},mask(Resource::wifi),30'000},
        {Text<24>{"storage"},mask(Resource::storage)|mask(Resource::cpu),30'000},
        {Text<24>{"sensors"},mask(Resource::sensors),30'000},
        {Text<24>{"scheduler"},mask(Resource::scheduled),30'000},
        {Text<24>{"applications"},mask(Resource::application),120'000},
        {Text<24>{"cognition"},mask(Resource::cpu)|mask(Resource::bluetooth)|mask(Resource::wifi),15'000}
    }};
}
bool PowerManager::bind(ServiceId id, Status (*apply)(void*,bool), void* context) {
    const auto index=static_cast<std::size_t>(id);
    if (index>=services_.size() || !apply) return false;
    auto& service=services_[index]; service.apply=apply; service.context=context;
    service.last_error=apply(context,service.enabled).code; return true;
}
void PowerManager::WakeLock::reset() {
    if (owner_ && owner_->locks_[slot_].generation==generation_) owner_->locks_[slot_].active=false;
    owner_=nullptr;
}
PowerManager::WakeLock::operator bool() const {
    return owner_ && owner_->locks_[slot_].active && owner_->locks_[slot_].generation==generation_;
}
PowerManager::WakeLock PowerManager::acquire(ServiceId service, ResourceMask resources, std::int64_t now, std::int64_t duration, bool blocking,PowerState activity) {
    const auto index=static_cast<std::size_t>(service);
    if (index>=services_.size() || !resources || (resources & services_[index].resources)!=resources ||
        duration<=0 || duration>services_[index].max_lock_ms || now>std::numeric_limits<std::int64_t>::max()-duration) return {};
    for (std::size_t i=0;i<locks_.size();++i) if (!locks_[i].active) {
        locks_[i]={service,resources,now,now+duration,++generation_,blocking,true,activity};
        WakeLock result; result.owner_=this; result.slot_=i; result.generation_=generation_; return result;
    }
    return {};
}
void PowerManager::allow(Resource resource, bool enabled) {
    if (enabled) allowed_ |= mask(resource);
    else allowed_ &= static_cast<ResourceMask>(~mask(resource));
}
PowerState PowerManager::update(const Environment& e, std::int64_t idle_ms, bool thinking, std::int64_t now) {
    ResourceMask requested=0; bool blocked=false,speaking=false;
    for (auto& lock:locks_) if (lock.active) {
        if (now>=lock.deadline) { lock.active=false; ++expired_; last_expired_=services_[static_cast<std::size_t>(lock.service)].name; }
        else { requested |= lock.resources; blocked |= lock.blocking; speaking |= lock.activity==PowerState::speaking && (lock.resources & mask(Resource::audio))!=0; }
    }
    PowerPlan next;
    if (e.charging) next.state=PowerState::charging;
    else if (e.battery<=5) next.state=PowerState::deep_sleep;
    else if ((requested & mask(Resource::audio))!=0) next.state=speaking ? PowerState::speaking : PowerState::listening;
    else if (thinking) next.state=PowerState::thinking;
    else if (idle_ms>=60'000 && !blocked) next.state=e.connected ? PowerState::standby : PowerState::deep_sleep;
    else next.state=PowerState::interactive;
    next.awake=(idle_ms<60'000 || blocked) && next.state!=PowerState::deep_sleep;
    next.enabled=requested | (next.awake ? mask(Resource::display)|mask(Resource::application) : 0);
    if (e.connected && next.state!=PowerState::deep_sleep) next.enabled |= mask(Resource::bluetooth);
    next.enabled &= allowed_;
    next.awake=next.awake && (next.enabled & mask(Resource::display))!=0;
    if (next.state==PowerState::deep_sleep) next.enabled=0;
    next.max_cpu_mhz=next.awake || blocked ? 240 : 80;
    plan_=next;
    constexpr std::array primary{Resource::display,Resource::audio,Resource::bluetooth,Resource::wifi,Resource::storage,Resource::sensors,Resource::scheduled,Resource::application,Resource::cpu};
    for (std::size_t i=0;i<services_.size();++i) {
        auto& service=services_[i];
        bool enabled=(mask(primary[i]) & next.enabled)!=0;
        if (i==static_cast<std::size_t>(ServiceId::cognition)) {
            enabled=thinking;
            for (const auto& lock:locks_) enabled=enabled || (lock.active && lock.service==ServiceId::cognition);
        }
        if (enabled!=service.enabled) {
            service.enabled=enabled;
            if (service.apply) service.last_error=service.apply(service.context,enabled).code;
        }
    }
    return plan_.state;
}
std::size_t PowerManager::active_locks() const { std::size_t count=0; for (const auto& lock:locks_) if (lock.active) ++count; return count; }
std::int64_t PowerManager::next_deadline() const {
    auto next=std::numeric_limits<std::int64_t>::max();
    for (const auto& lock:locks_) if (lock.active) next=std::min(next,lock.deadline);
    return next;
}
}
