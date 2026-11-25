#pragma once
#include "daemon/framework/runtime.hpp"
#include "daemon/platform/desktop.hpp"
#include "sdl_hal.hpp"
#include "daemon/presentation/watch_view.hpp"
#include <array>
#include <vector>

namespace daemonos::simulator {
class Ui {
public:
    Ui(Runtime& runtime, desktop::VirtualClock& clock, SdlDisplay& watch, SdlDisplay& debug);
    void update(std::uint32_t real_ms);
    bool target(std::string_view id, SdlDisplay*& display, int& x, int& y) const;
    void input() { view_.invalidate(); }
    std::uint32_t next_delay(std::uint32_t now) const { return std::min(view_.next_delay(now),now-last_update_>=500 ? 0U : 500-(now-last_update_)); }
private:
    struct Binding { Ui* ui{}; Text<32> id{}; Event event{}; lv_obj_t* object{}; bool debug{}; };
    static void clicked(lv_event_t* event);
    lv_obj_t* button(lv_obj_t* parent, const char* caption, const char* id, int x, int y, int w, int h, Event event, bool debug = false);
    lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, std::uint32_t color, const lv_font_t* font = &lv_font_montserrat_14);
    lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, std::uint32_t color);
    void build_debug();
    void invoke(const Binding& binding);
    Runtime& runtime_;
    desktop::VirtualClock& clock_;
    SdlDisplay& watch_;
    SdlDisplay& debug_;
    presentation::WatchController controller_;
    presentation::WatchView view_;
    std::array<Binding, 64> bindings_{};
    std::size_t binding_count_{};
    lv_obj_t* debug_state_{};
    std::uint32_t last_update_{};
    int last_brightness_{-1};
    std::size_t last_frames_{};
    double fps_{};
    bool show_contract_{};
    PowerManager::WakeLock audio_lock_{};
    PowerManager::WakeLock wifi_lock_{},sensor_lock_{};
};
}
