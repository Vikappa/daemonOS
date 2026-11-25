#pragma once
#include "daemon/framework/runtime.hpp"
#include <lvgl.h>

namespace daemonos::presentation {
struct WatchState {
    const CreatureState& creature;
    const Environment& environment;
    const Settings& settings;
    AppId app;
    bool awake;
    std::string_view message;
    std::int64_t wall_seconds;
    std::uint64_t revision;
};
class WatchController {
public:
    WatchController(Runtime& runtime,hal::Clock& clock) : runtime_(runtime),clock_(clock) {}
    WatchState state() const { return {runtime_.creature(),runtime_.environment(),runtime_.settings(),runtime_.app(),runtime_.display_awake(),runtime_.message(),clock_.wall_seconds(),runtime_.view_revision()}; }
    void activate(std::string_view id,Event event) {
        if (id=="brightness_down") event.value=runtime_.settings().brightness-10;
        if (id=="brightness_up") event.value=runtime_.settings().brightness+10;
        if (id=="cognition") event.value=!runtime_.settings().cognition;
        runtime_.post(event);
    }
private: Runtime& runtime_; hal::Clock& clock_;
};
const lv_image_dsc_t& ember_sprite();
class WatchView {
public:
    WatchView(WatchController& controller,lv_display_t* display,const lv_image_dsc_t& sprite);
    void update(std::uint32_t real_ms);
    void invalidate() { dirty_=true; }
    std::uint32_t next_delay(std::uint32_t now) const { return !awake_ ? UINT32_MAX : dirty_ || static_cast<std::int32_t>(now-next_update_)>=0 ? 0 : next_update_-now; }
    bool target(std::string_view id,int& x,int& y) const;
private:
    struct Binding { WatchView* view{}; Text<32> id{}; Event event{}; lv_obj_t* object{}; };
    static void clicked(lv_event_t* event);
    lv_obj_t* button(lv_obj_t*,const char*,const char*,int,int,int,int,Event,bool=false);
    lv_obj_t* label(lv_obj_t*,const char*,int,int,std::uint32_t,const lv_font_t* = &lv_font_montserrat_14);
    lv_obj_t* panel(lv_obj_t*,int,int,int,int,std::uint32_t);
    void build();
    WatchController& controller_;
    lv_display_t* display_{};
    const lv_image_dsc_t& sprite_descriptor_;
    std::array<Binding,24> bindings_{};
    std::size_t binding_count_{};
    AppId last_app_{static_cast<AppId>(255)};
    lv_obj_t *watch_root_{},*time_label_{},*battery_label_{},*status_label_{},*message_label_{},*sprite_{},*clock_label_{},*date_label_{},*setting_label_{};
    std::array<lv_obj_t*,4> bars_{},values_{};
    std::uint64_t last_revision_{};
    std::uint32_t next_update_{};
    bool dirty_{true},awake_{true};
};
}
