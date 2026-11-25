// Unvalidated integration seed: no board peripherals or persistence are enabled.
#include "daemon/services/creature.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctime>

namespace {
class Clock final : public daemonos::hal::Clock {
public:
    std::int64_t monotonic_ms() const override { return esp_timer_get_time()/1000; }
    std::int64_t wall_seconds() const override { return static_cast<std::int64_t>(std::time(nullptr)); }
};
class Random final : public daemonos::hal::Random {
public: std::uint32_t next() override { return esp_random(); }
};
}
extern "C" void app_main() {
    Clock clock; Random random;
    auto state=daemonos::CreatureEngine::create(clock,random);
    ESP_LOGW("genesis", "Integration seed only: no persistence, display, radio or validated board drivers");
    auto previous=clock.monotonic_ms();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const auto now=clock.monotonic_ms();
        daemonos::CreatureEngine::advance(state,now-previous); previous=now;
    }
}
