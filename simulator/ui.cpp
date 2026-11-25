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
    : runtime_(runtime), clock_(clock), watch_(watch), debug_(debug) {
    sprite(); build_debug(); debug_binding_count_ = binding_count_; build_watch();
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
        runtime_.post(event);
    }
}
bool Ui::target(std::string_view id, SdlDisplay*& display, int& x, int& y) const {
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
void Ui::sprite() {
    std::ifstream file(std::string{DAEMON_ASSET_DIR} + "/sprites/ember.sprite");
    if (!file) throw std::runtime_error("Missing Ember sprite asset");
    std::vector<std::string> lines; std::string line;
    while (std::getline(file,line)) if (!line.empty()) lines.push_back(line);
    if (lines.empty() || lines.size() > 48 || lines[0].empty() || lines[0].size() > 48) throw std::runtime_error("Invalid sprite dimensions");
    const auto width = lines[0].size();
    for (const auto& row : lines) {
        if (row.size() != width) throw std::runtime_error("Invalid sprite row");
        for (const auto pixel : row) {
            std::uint32_t color = 0;
            switch (pixel) {
            case '.': break; case 'x': color=0xff633438; break; case 'r': color=0xffdd533f; break;
            case 'o': color=0xffff8653; break; case 'h': color=0xffffc67a; break;
            case 'e': color=0xffc2ffd4; break; case 'b': color=0xff241d28; break;
            default: throw std::runtime_error("Invalid sprite palette");
            }
            sprite_pixels_.push_back(color);
        }
    }
    sprite_descriptor_.header.magic=LV_IMAGE_HEADER_MAGIC;
    sprite_descriptor_.header.cf=LV_COLOR_FORMAT_ARGB8888;
    sprite_descriptor_.header.w=static_cast<std::uint16_t>(width);
    sprite_descriptor_.header.h=static_cast<std::uint16_t>(lines.size());
    sprite_descriptor_.header.stride=static_cast<std::uint16_t>(width * 4);
    sprite_descriptor_.data_size=static_cast<std::uint32_t>(sprite_pixels_.size() * 4);
    sprite_descriptor_.data=reinterpret_cast<const std::uint8_t*>(sprite_pixels_.data());
}
void Ui::build_debug() {
    lv_display_set_default(debug_.lv_display());
    auto* root = lv_display_get_screen_active(debug_.lv_display());
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101316),0); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    label(root,"GENESIS / LAB",24,22,orange,&lv_font_montserrat_28);
    label(root,"SIMULATOR CONTROLS  /  v0.1.0",24,61,muted,&lv_font_montserrat_12);
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
    debug_state_ = label(root,"",24,467,white); lv_obj_set_width(debug_state_,412);
    label(root,"HOME: H  /  BACK: Esc  /  WAKE: Space\nClose either window to save and exit.\nPhysical drivers and power draw are unvalidated.",24,606,muted,&lv_font_montserrat_12);
}
void Ui::build_watch() {
    lv_display_set_default(watch_.lv_display());
    auto* root = lv_display_get_screen_active(watch_.lv_display()); lv_obj_clean(root);
    binding_count_ = debug_binding_count_; watch_root_=root;
    time_label_=battery_label_=status_label_=message_label_=sprite_=clock_label_=date_label_=setting_label_=nullptr;
    bars_.fill(nullptr); values_.fill(nullptr);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x000000),0); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    time_label_=label(root,"00:00",22,15,white,&lv_font_montserrat_12);
    label(root,"D / OS",166,15,orange,&lv_font_montserrat_12);
    battery_label_=label(root,"85%",318,15,green,&lv_font_montserrat_12);
    panel(root,22,41,366,1,0x2b2d30);
    const auto app = runtime_.app();
    if (app == AppId::launcher) {
        label(root,"A SMALL FIRE.",24,67,white,&lv_font_montserrat_28);
        label(root,"A WORLD WITHIN.",24,101,orange,&lv_font_montserrat_28);
        label(root,"YOUR OFFLINE DIGITAL FAMILIAR",24,147,muted,&lv_font_montserrat_12);
        sprite_ = lv_image_create(root); lv_image_set_src(sprite_,&sprite_descriptor_);
        lv_image_set_scale(sprite_,1152); lv_image_set_antialias(sprite_,false); lv_obj_set_pos(sprite_,280,239);
        label(root,"EMBER",26,213,white,&lv_font_montserrat_20);
        label(root,"Identity held locally.\nInstincts always awake.",26,245,muted);
        auto* enter=button(root,"Meet Ember   >","open_creature",24,338,362,52,{EventType::open_app,3});
        lv_obj_set_style_bg_color(enter,lv_color_hex(0x6d3428),0);
        button(root,"Clock","open_clock",24,404,175,48,{EventType::open_app,1});
        button(root,"Settings","open_settings",211,404,175,48,{EventType::open_app,2});
        label(root,"GENESIS 001    /    LOCAL-FIRST LIFE",24,473,muted,&lv_font_montserrat_12);
    } else if (app == AppId::creature) {
        label(root,"EMBER",22,57,white,&lv_font_montserrat_28);
        status_label_=label(root,"AWAKE / FAMILIAR 001",22,94,green,&lv_font_montserrat_12);
        label(root,"+",28,146,0x49352f,&lv_font_montserrat_20);
        label(root,"+",366,223,0x49352f,&lv_font_montserrat_20);
        auto* shadow=panel(root,133,267,144,4,0x53251e); lv_obj_set_style_radius(shadow,LV_RADIUS_CIRCLE,0);
        sprite_=lv_image_create(root); lv_image_set_src(sprite_,&sprite_descriptor_); lv_image_set_scale(sprite_,1280); lv_image_set_antialias(sprite_,false); lv_obj_set_pos(sprite_,189,177);
        message_label_=label(root,"",22,280,white,&lv_font_montserrat_12); lv_obj_set_size(message_label_,366,32);
        constexpr std::array names{"SATIETY","ENERGY","AFFECTION","SPIRIT"};
        for (std::size_t i=0;i<4;++i) {
            const int x=22 + static_cast<int>(i % 2)*190, y=321 + static_cast<int>(i / 2)*43;
            label(root,names[i],x,y,muted,&lv_font_montserrat_12);
            values_[i]=label(root,"0",x+130,y,white,&lv_font_montserrat_12);
            bars_[i]=lv_bar_create(root); lv_obj_set_pos(bars_[i],x,y+19); lv_obj_set_size(bars_[i],166,5);
            lv_obj_set_style_bg_color(bars_[i],lv_color_hex(0x24282b),LV_PART_MAIN);
            lv_obj_set_style_bg_color(bars_[i],lv_color_hex(i==0 ? orange : green),LV_PART_INDICATOR);
        }
        button(root,"Feed","feed",22,414,112,43,{EventType::feed});
        button(root,"Rest / wake","rest",149,414,112,43,{EventType::rest});
        button(root,"Pet","pet",276,414,112,43,{EventType::pet});
        button(root,"< Home","home",22,467,112,29,{EventType::home});
        button(root,"Think / mock","think",246,467,142,29,{EventType::think});
    } else if (app == AppId::clock) {
        label(root,"TIME / LOCAL",24,70,muted,&lv_font_montserrat_12);
        clock_label_=label(root,"00:00:00",24,140,white,&lv_font_montserrat_28);
        date_label_=label(root,"",24,188,orange,&lv_font_montserrat_20);
        message_label_=label(root,"",24,267,muted); lv_obj_set_width(message_label_,360);
        label(root,"Time moves. Ember lives.\nEven while the display sleeps.",24,340,white);
        button(root,"< Home","home",24,437,362,43,{EventType::home});
    } else {
        label(root,"SETTINGS",24,70,white,&lv_font_montserrat_28);
        label(root,"DISPLAY / COGNITION",24,112,muted,&lv_font_montserrat_12);
        setting_label_=label(root,"",24,161,orange,&lv_font_montserrat_20);
        button(root,"Dimmer","brightness_down",24,206,175,48,{EventType::brightness});
        button(root,"Brighter","brightness_up",211,206,175,48,{EventType::brightness});
        button(root,"Toggle mock cognition","cognition",24,279,362,48,{EventType::toggle_cognition});
        label(root,"No cloud account required.\nIdentity and memories stay on this device.\nMicrophone is off.",24,354,muted,&lv_font_montserrat_12);
        button(root,"< Home","home",24,437,362,43,{EventType::home});
    }
    last_app_=app;
}
void Ui::update(std::uint32_t real_ms) {
    if (runtime_.app() != last_app_) build_watch();
    if (sprite_) {
        const int offset=runtime_.creature().sleeping ? 0 : static_cast<int>((real_ms / 220) % 6);
        const int bob=offset < 3 ? offset : 5-offset;
        lv_obj_set_y(sprite_,(last_app_ == AppId::creature ? 177 : 239) - bob);
        lv_obj_set_style_image_opa(sprite_,runtime_.creature().sleeping ? LV_OPA_60 : LV_OPA_COVER,0);
    }
    if (real_ms - last_update_ < 100 && last_update_ != 0) return;
    last_update_=real_ms;
    const auto epoch=static_cast<std::time_t>(clock_.wall_seconds()); std::tm calendar{}; gmtime_r(&epoch,&calendar);
    char buffer[512]{}; std::strftime(buffer,sizeof(buffer),"%H:%M UTC",&calendar); text(time_label_,buffer);
    std::snprintf(buffer,sizeof(buffer),"%s %d%%",runtime_.environment().charging ? "+" : "",runtime_.environment().battery); text(battery_label_,buffer);
    std::strftime(buffer,sizeof(buffer),"%H:%M:%S",&calendar); text(clock_label_,buffer);
    std::strftime(buffer,sizeof(buffer),"%a, %d %b %Y",&calendar); text(date_label_,buffer);
    text(status_label_,runtime_.creature().sleeping ? "RESTING / INNER FIRE" : "AWAKE / FAMILIAR 001");
    if (message_label_) {
        if (last_app_ == AppId::clock) {
            std::snprintf(buffer,sizeof(buffer),"Lifetime: %lld hours\nSimulation speed: %.0fx\nTimezone: UTC",static_cast<long long>(runtime_.creature().age_seconds/3600),clock_.speed()); text(message_label_,buffer);
        } else text(message_label_,std::string{runtime_.message()}.c_str());
    }
    std::snprintf(buffer,sizeof(buffer),"Brightness: %d%%\nMock cognition: %s",runtime_.settings().brightness,runtime_.settings().cognition ? "ON" : "OFF"); text(setting_label_,buffer);
    const auto& n=runtime_.creature().needs;
    const std::array values{100.0-n.hunger,n.energy,n.affection,n.happiness};
    for (std::size_t i=0;i<4;++i) if (bars_[i]) {
        const auto value=static_cast<int>(values[i]);
        if (lv_bar_get_value(bars_[i]) != value) lv_bar_set_value(bars_[i],value,LV_ANIM_OFF);
        std::snprintf(buffer,sizeof(buffer),"%d",value); text(values_[i],buffer);
    }
    const auto& e=runtime_.environment();
    std::snprintf(buffer,sizeof(buffer),"%s  /  %.0fx  /  %s\nBattery %d%%  |  charging %s  |  steps %u\nStorage: %s\nQueue drops %zu  |  unsaved memories %zu / lost %zu\nHunger %.1f  energy %.1f  health %.1f\nBoredom %.1f  stress %.1f  curiosity %.1f",
        power_name(runtime_.power()).data(),clock_.speed(),e.connected ? "MOCK ONLINE" : "OFFLINE",e.battery,e.charging ? "yes" : "no",e.steps,
        runtime_.storage_status().data(),runtime_.dropped_events(),runtime_.pending_memories(),runtime_.lost_memories(),n.hunger,n.energy,n.health,n.boredom,n.stress,n.curiosity);
    text(debug_state_,buffer);
    const int brightness=runtime_.display_awake() ? runtime_.settings().brightness : 0;
    if (brightness != last_brightness_) { watch_.brightness(brightness); last_brightness_=brightness; }
}
}
