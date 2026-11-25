#include "daemon/presentation/watch_view.hpp"
#include <cstdio>
#include <ctime>

namespace daemonos::presentation {
namespace {
constexpr std::uint32_t orange=0xff7547, white=0xe9e6df, muted=0x8b939c, green=0xa8f0b2;
void text(lv_obj_t* object,const char* value) {
    if (object && std::string_view{lv_label_get_text(object)}!=value) lv_label_set_text(object,value);
}
}
WatchView::WatchView(WatchController& controller,lv_display_t* display,const lv_image_dsc_t& sprite)
    : controller_(controller),display_(display),sprite_descriptor_(sprite) {
    build(); lv_timer_pause(lv_display_get_refr_timer(display_));
}
void WatchView::clicked(lv_event_t* event) {
    auto* binding=static_cast<Binding*>(lv_event_get_user_data(event));
    binding->view->controller_.activate(binding->id.view(),binding->event);
    binding->view->dirty_=true;
}
bool WatchView::target(std::string_view id,int& x,int& y) const {
    lv_obj_update_layout(watch_root_);
    for (std::size_t i=0;i<binding_count_;++i) if (bindings_[i].id.view()==id) {
        lv_area_t area{}; lv_obj_get_coords(bindings_[i].object,&area);
        x=(area.x1+area.x2)/2; y=(area.y1+area.y2)/2; return true;
    }
    return false;
}
lv_obj_t* WatchView::label(lv_obj_t* parent, const char* value, int x, int y, std::uint32_t color, const lv_font_t* font) {
    auto* object = lv_label_create(parent); lv_label_set_text(object,value); lv_obj_set_pos(object,x,y);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0); lv_obj_set_style_text_font(object,font,0);
    return object;
}
lv_obj_t* WatchView::panel(lv_obj_t* parent, int x, int y, int w, int h, std::uint32_t color) {
    auto* object = lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_obj_set_style_bg_color(object,lv_color_hex(color),0); lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_radius(object,10,0); lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE); return object;
}
lv_obj_t* WatchView::button(lv_obj_t* parent, const char* caption, const char* id, int x, int y, int w, int h, Event event, bool) {
    if (binding_count_ == bindings_.size()) return nullptr;
    auto* object = lv_button_create(parent); lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x1c2024),0);
    lv_obj_set_style_radius(object,9,0); lv_obj_set_style_shadow_width(object,0,0);
    lv_obj_set_style_border_width(object,1,0); lv_obj_set_style_border_color(object,lv_color_hex(0x32373b),0);
    auto* caption_label = label(object,caption,0,0,white); lv_obj_center(caption_label);
    auto& binding = bindings_[binding_count_++]; binding = {}; binding.view = this; binding.id.assign(id); binding.event = event; binding.object = object;
    lv_obj_add_event_cb(object,clicked,LV_EVENT_CLICKED,&binding);
    return object;
}
void WatchView::build() {
    lv_display_set_default(display_);
    auto* root = lv_display_get_screen_active(display_); lv_obj_clean(root);
    binding_count_ = 0; watch_root_=root;
    time_label_=battery_label_=status_label_=message_label_=sprite_=clock_label_=date_label_=setting_label_=nullptr;
    bars_.fill(nullptr); values_.fill(nullptr);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x000000),0); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    time_label_=label(root,"00:00",22,15,white,&lv_font_montserrat_12);
    label(root,"D / OS",166,15,orange,&lv_font_montserrat_12);
    battery_label_=label(root,"85%",318,15,green,&lv_font_montserrat_12);
    panel(root,22,41,366,1,0x2b2d30);
    const auto state = controller_.state();
    const auto app = state.app;
    if (app == AppId::launcher) {
        label(root,"A SMALL FIRE.",24,67,white,&lv_font_montserrat_28);
        label(root,"A WORLD WITHIN.",24,101,orange,&lv_font_montserrat_28);
        label(root,"YOUR OFFLINE DIGITAL FAMILIAR",24,147,muted,&lv_font_montserrat_12);
        sprite_ = lv_image_create(root); lv_image_set_src(sprite_,&sprite_descriptor_);
        lv_image_set_scale(sprite_,1152); lv_image_set_antialias(sprite_,false); lv_obj_set_pos(sprite_,280,239);
        label(root,state.creature.name.c_str(),26,213,white,&lv_font_montserrat_20);
        label(root,"Identity held locally.\nInstincts always awake.",26,245,muted);
        auto* enter=button(root,"Meet familiar   >","open_creature",24,338,362,52,{EventType::open_app,3});
        lv_obj_set_style_bg_color(enter,lv_color_hex(0x6d3428),0);
        button(root,"Clock","open_clock",24,404,175,48,{EventType::open_app,1});
        button(root,"Settings","open_settings",211,404,175,48,{EventType::open_app,2});
        label(root,"HABITAT 002    /    LOCAL-FIRST LIFE",24,473,muted,&lv_font_montserrat_12);
    } else if (app == AppId::creature) {
        label(root,state.creature.name.c_str(),22,57,white,&lv_font_montserrat_28);
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
        label(root,"Time moves. Your familiar lives.\nEven while the display sleeps.",24,340,white);
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
void WatchView::update(std::uint32_t real_ms) {
    const auto state = controller_.state();
    if (!state.awake) { lv_timer_pause(lv_display_get_refr_timer(display_)); awake_=false; return; }
    if (!awake_) { awake_=true; dirty_=true; lv_obj_invalidate(lv_display_get_screen_active(display_)); }
    if (!dirty_ && state.revision==last_revision_ && static_cast<std::int32_t>(real_ms-next_update_)<0) return;
    if (state.app != last_app_) build();
    last_revision_=state.revision; dirty_=false;
    next_update_=real_ms + (state.creature.sleeping || state.app==AppId::clock || state.app==AppId::settings ? 1000U : 220U);
    if (sprite_) {
        const int offset=state.creature.sleeping ? 0 : static_cast<int>((real_ms / 220) % 6);
        const int bob=offset < 3 ? offset : 5-offset;
        lv_obj_set_y(sprite_,(last_app_ == AppId::creature ? 177 : 239) - bob);
        lv_obj_set_style_image_opa(sprite_,state.creature.sleeping ? LV_OPA_60 : LV_OPA_COVER,0);
    }
    const auto epoch=static_cast<std::time_t>(state.wall_seconds); std::tm calendar{}; if (const auto* utc=std::gmtime(&epoch)) calendar=*utc;
    char buffer[512]{}; std::strftime(buffer,sizeof(buffer),"%H:%M UTC",&calendar); text(time_label_,buffer);
    std::snprintf(buffer,sizeof(buffer),"%s %d%%",state.environment.charging ? "+" : "",state.environment.battery); text(battery_label_,buffer);
    std::strftime(buffer,sizeof(buffer),"%H:%M:%S",&calendar); text(clock_label_,buffer);
    std::strftime(buffer,sizeof(buffer),"%a, %d %b %Y",&calendar); text(date_label_,buffer);
    text(status_label_,state.creature.sleeping ? "RESTING / INNER FIRE" : "AWAKE / FAMILIAR 001");
    if (message_label_) {
        if (last_app_ == AppId::clock) {
            std::snprintf(buffer,sizeof(buffer),"Lifetime: %lld hours\nTimezone: UTC",static_cast<long long>(state.creature.age_seconds/3600)); text(message_label_,buffer);
        } else text(message_label_,state.message.data());
    }
    std::snprintf(buffer,sizeof(buffer),"Brightness: %d%%\nMock cognition: %s",state.settings.brightness,state.settings.cognition ? "ON" : "OFF"); text(setting_label_,buffer);
    const auto& n=state.creature.needs;
    const std::array values{100.0-n.hunger,n.energy,n.affection,n.happiness};
    for (std::size_t i=0;i<4;++i) if (bars_[i]) {
        const auto value=static_cast<int>(values[i]);
        if (lv_bar_get_value(bars_[i]) != value) lv_bar_set_value(bars_[i],value,LV_ANIM_OFF);
        std::snprintf(buffer,sizeof(buffer),"%d",value); text(values_[i],buffer);
    }
    lv_refr_now(display_);
    lv_timer_pause(lv_display_get_refr_timer(display_));
}
}
