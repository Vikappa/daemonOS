#pragma once
#include "daemon/framework/runtime.hpp"
#include "daemon/platform/desktop.hpp"
#include "sdl_hal.hpp"
#include <array>
#include <vector>

namespace daemonos::simulator {
class Ui {
public:
    Ui(Runtime& runtime, desktop::VirtualClock& clock, SdlDisplay& watch, SdlDisplay& debug);
    void update(std::uint32_t real_ms);
    bool target(std::string_view id, SdlDisplay*& display, int& x, int& y) const;
private:
    struct Binding { Ui* ui{}; Text<32> id{}; Event event{}; lv_obj_t* object{}; bool debug{}; };
    static void clicked(lv_event_t* event);
    lv_obj_t* button(lv_obj_t* parent, const char* caption, const char* id, int x, int y, int w, int h, Event event, bool debug = false);
    lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, std::uint32_t color, const lv_font_t* font = &lv_font_montserrat_14);
    lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, std::uint32_t color);
    void build_watch();
    void build_debug();
    void sprite();
    void invoke(const Binding& binding);
    Runtime& runtime_;
    desktop::VirtualClock& clock_;
    SdlDisplay& watch_;
    SdlDisplay& debug_;
    AppId last_app_{static_cast<AppId>(255)};
    std::array<Binding, 64> bindings_{};
    std::size_t binding_count_{};
    std::size_t debug_binding_count_{};
    lv_obj_t* watch_root_{};
    lv_obj_t* time_label_{};
    lv_obj_t* battery_label_{};
    lv_obj_t* status_label_{};
    lv_obj_t* message_label_{};
    lv_obj_t* sprite_{};
    lv_obj_t* clock_label_{};
    lv_obj_t* date_label_{};
    lv_obj_t* setting_label_{};
    lv_obj_t* debug_state_{};
    std::array<lv_obj_t*,4> bars_{};
    std::array<lv_obj_t*,4> values_{};
    std::vector<std::uint32_t> sprite_pixels_;
    lv_image_dsc_t sprite_descriptor_{};
    std::uint32_t last_update_{};
    int last_brightness_{-1};
};
}
