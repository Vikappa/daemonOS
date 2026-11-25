#pragma once
#include "daemon/types.hpp"

namespace daemonos {
class PowerManager {
public:
    PowerState update(const Environment& environment, std::int64_t idle_ms, bool thinking) {
        if (environment.charging) state_ = PowerState::charging;
        else if (environment.battery <= 5) state_ = PowerState::deep_sleep;
        else if (thinking) state_ = PowerState::thinking;
        else if (idle_ms >= 60'000) state_ = environment.connected ? PowerState::ble_standby : PowerState::deep_sleep;
        else state_ = PowerState::interactive;
        return state_;
    }
    PowerState state() const { return state_; }
    bool display_awake() const { return state_ != PowerState::deep_sleep && state_ != PowerState::ble_standby; }
private:
    PowerState state_{PowerState::interactive};
};
}
