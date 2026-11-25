#include "ui.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>

namespace daemonos::simulator {
namespace {
constexpr std::uint32_t orange = 0xff7547, white = 0xe9e6df, muted = 0x8b939c, green = 0xa8f0b2;
void text(lv_obj_t* object, const char* value) { if (object && std::string_view{lv_label_get_text(object)} != value) lv_label_set_text(object,value); }
}
Ui::Ui(Runtime& runtime, desktop::VirtualClock& clock, SdlDisplay& watch, SdlDisplay& debug)
    : runtime_(runtime), clock_(clock), watch_(watch), debug_(debug), controller_(runtime,clock), view_(controller_,watch.lv_display()) {
    build_debug();
    lv_timer_pause(lv_display_get_refr_timer(debug_.lv_display()));
}
lv_obj_t* Ui::label(lv_obj_t* parent, const char* value, int x, int y, std::uint32_t color, const lv_font_t* font) {
    auto* object = lv_label_create(parent); lv_label_set_text(object,value); lv_obj_set_pos(object,x,y);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0); lv_obj_set_style_text_font(object,font,0);
    return object;
}
lv_obj_t* Ui::panel(lv_obj_t* parent, int x, int y, int w, int h, std::uint32_t color) {
    auto* object = lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_obj_set_style_bg_color(object,lv_color_hex(color),0); lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_radius(object,10,0); lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE); return object;
}
lv_obj_t* Ui::button(lv_obj_t* parent, const char* caption, const char* id, int x, int y, int w, int h, Event event, bool debug) {
    if (binding_count_ == bindings_.size()) throw std::runtime_error("UI binding budget exceeded");
    auto* object = lv_button_create(parent); lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x1c2024),0);
    lv_obj_set_style_radius(object,9,0); lv_obj_set_style_shadow_width(object,0,0);
    lv_obj_set_style_border_width(object,1,0); lv_obj_set_style_border_color(object,lv_color_hex(0x32373b),0);
    auto* caption_label = label(object,caption,0,0,white); lv_obj_center(caption_label);
    auto& binding = bindings_[binding_count_++]; binding = {}; binding.ui = this; binding.id.assign(id); binding.event = event; binding.object = object; binding.debug = debug;
    lv_obj_add_event_cb(object,clicked,LV_EVENT_CLICKED,&binding);
    return object;
}
void Ui::clicked(lv_event_t* event) {
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(event)); binding->ui->invoke(*binding);
}
void Ui::invoke(const Binding& b) {
    const auto id = b.id.view();
    if (id == "advance_hour") clock_.advance(3600'000);
    else if (id == "speed") clock_.speed(clock_.speed() == 1 ? 60 : 1);
    else if (id=="contract") { show_contract_=!show_contract_; last_update_=0; }
    else if (id=="audio_lock") {
        if (audio_lock_) audio_lock_.reset();
        else audio_lock_=runtime_.power_manager().acquire(ServiceId::audio,Resource::audio|Resource::cpu,clock_.monotonic_ms(),5000,true,PowerState::speaking);
        runtime_.post({EventType::wake});
    }
    else if (id=="wifi" || id=="sensors") {
        auto& lock=id=="wifi" ? wifi_lock_ : sensor_lock_;
        if (lock) lock.reset();
        else lock=runtime_.power_manager().acquire(id=="wifi" ? ServiceId::wifi : ServiceId::sensors,mask(id=="wifi" ? Resource::wifi : Resource::sensors),clock_.monotonic_ms(),5000);
        runtime_.post({EventType::wake});
    }
    else {
        Event event = b.event;
        if (id == "battery_down") event.value = runtime_.environment().battery - 10;
        if (id == "battery_up") event.value = runtime_.environment().battery + 10;
        if (id == "charge") event.value = !runtime_.environment().charging;
        if (id == "network") event.value = !runtime_.environment().connected;
        if (id == "storage") event.value = !runtime_.environment().storage;
        if (id == "cognition") event.value = !runtime_.settings().cognition;
        if (id == "brightness_down") event.value = runtime_.settings().brightness - 10;
        if (id == "brightness_up") event.value = runtime_.settings().brightness + 10;
        if (id=="pressure") event.value=!runtime_.resources().pressured();
        runtime_.post(event);
    }
}
bool Ui::target(std::string_view id, SdlDisplay*& display, int& x, int& y) const {
    if (view_.target(id,x,y)) { display=&watch_; return true; }
    for (std::size_t i = 0; i < binding_count_; ++i) {
        const auto& binding = bindings_[i];
        if (binding.id.view() == id) {
            lv_area_t area{}; lv_obj_get_coords(binding.object,&area);
            x = (area.x1 + area.x2) / 2; y = (area.y1 + area.y2) / 2;
            display = binding.debug ? &debug_ : &watch_; return true;
        }
    }
    return false;
}
void Ui::build_debug() {
    lv_display_set_default(debug_.lv_display());
    auto* root = lv_display_get_screen_active(debug_.lv_display());
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101316),0); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    label(root,"HABITAT / LAB",24,22,orange,&lv_font_montserrat_28);
    label(root,"SIMULATOR CONTROLS  /  v0.2.0",24,61,muted,&lv_font_montserrat_12);
    label(root,"Inject conditions through the shared runtime.",24,89,white);
    label(root,"VIRTUAL TIME",24,127,muted,&lv_font_montserrat_12);
    button(root,"+ 1 hour","advance_hour",24,151,200,42,{},true);
    button(root,"1x / 60x","speed",236,151,200,42,{},true);
    label(root,"DEVICE & ENVIRONMENT",24,216,muted,&lv_font_montserrat_12);
    button(root,"Battery -10%","battery_down",24,240,200,42,{EventType::battery},true);
    button(root,"Battery +10%","battery_up",236,240,200,42,{EventType::battery},true);
    button(root,"Toggle charging","charge",24,294,200,42,{EventType::charging},true);
    button(root,"Walk 100 steps","move",236,294,200,42,{EventType::move,100},true);
    button(root,"Toggle connection","network",24,348,200,42,{EventType::connectivity},true);
    button(root,"Storage fault / heal","storage",236,348,200,42,{EventType::storage},true);
    button(root,"Suspend","suspend",24,402,200,42,{EventType::suspend},true);
    button(root,"Wake / button","wake",236,402,200,42,{EventType::wake},true);
    debug_state_ = label(root,"",24,467,white,&lv_font_montserrat_12); lv_obj_set_width(debug_state_,412);
    button(root,"Memory pressure","pressure",24,718,200,42,{EventType::memory_pressure},true);
    button(root,"Battery empty","empty_battery",236,718,200,42,{EventType::battery,0},true);
    button(root,"Speaking lock / 5s","audio_lock",24,772,200,42,{},true);
    button(root,"Wi-Fi use / 5s","wifi",236,772,200,42,{EventType::peripheral},true);
    button(root,"Sensors use / 5s","sensors",24,826,200,42,{EventType::peripheral},true);
    button(root,"Inspect contract","contract",236,826,200,42,{},true);
    label(root,"HOME: H / BACK: Esc / WAKE: Space\nClose either window to save and exit.\nPeripheral states are simulated; no measured power draw.",24,902,muted,&lv_font_montserrat_12);
}
void Ui::update(std::uint32_t real_ms) {
    view_.update(real_ms);
    const int brightness=runtime_.display_awake() ? runtime_.settings().brightness : 0;
    if (brightness!=last_brightness_) { watch_.brightness(brightness); last_brightness_=brightness; }
    if (real_ms-last_update_<500 && last_update_!=0) return;
    const auto elapsed=real_ms-last_update_;
    fps_=elapsed ? static_cast<double>(watch_.rendered_frames()-last_frames_)*1000.0/elapsed : 0;
    last_frames_=watch_.rendered_frames(); last_update_=real_ms;
    char buffer[1024]{};
    const auto& e=runtime_.environment();
    std::string active,locks;
    for (const auto& service:runtime_.power_manager().services()) if (service.enabled) { if (!active.empty()) active+=", "; active+=service.name.view(); }
    for (const auto& lock:runtime_.power_manager().locks()) if (lock.active) {
        if (!locks.empty()) locks+=", ";
        locks+=runtime_.power_manager().services()[static_cast<std::size_t>(lock.service)].name.view();
    }
    const auto& memory=runtime_.resources();
    std::snprintf(buffer,sizeof(buffer),
        "%s / %.0fx / %s / %.1f FPS\nActive: %s\nWake locks: %s / expired %zu\nRAM internal %zu / peak %zu B\nRAM external %zu / peak %zu B\nPressure %s / denied allocations %zu\nEvents %zu / 64 / peak %zu / drops %zu\nStore calls %zu / %s\nCognition: %s / memories queued %zu\nPeripherals 0x%03x / battery %d%% / steps %u\nWake: %s / last sleep %.1f s",
        power_name(runtime_.power()).data(),clock_.speed(),e.connected ? "MOCK ONLINE" : "OFFLINE",fps_,active.c_str(),locks.empty() ? "none" : locks.c_str(),runtime_.power_manager().expired_locks(),
        memory.used(MemoryPool::internal),memory.peak(MemoryPool::internal),memory.used(MemoryPool::external),memory.peak(MemoryPool::external),memory.pressured() ? "YES" : "no",memory.rejected(),
        runtime_.event_queue_size(),runtime_.event_queue_peak(),runtime_.dropped_events(),runtime_.database_operations(),runtime_.storage_status().data(),runtime_.cognitive_lifecycle().data(),runtime_.pending_memories(),
        static_cast<unsigned>(runtime_.power_manager().plan().enabled),e.battery,e.steps,runtime_.last_wake_reason().data(),static_cast<double>(runtime_.last_sleep_ms())/1000);
    if (show_contract_) std::snprintf(buffer,sizeof(buffer),"AWAKENING / RESPONSE v2\n2048 bytes / 4 actions / 2 proposals per kind\nSpeech: string | null / voice: object | null\nEmotion + intensity / face / animation\nMemory candidates: validated local proposals\nPersonality: bounded deltas (execution denied)\nIntentions: bounded delays (execution denied)\nCapabilities: explicit watch registry\nagent.md: <=1023 bytes / local configuration\nNo real provider / no remote authoritative state");
    text(debug_state_,buffer);
    lv_refr_now(debug_.lv_display());
    lv_timer_pause(lv_display_get_refr_timer(debug_.lv_display()));
}
}
