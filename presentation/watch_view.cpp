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
WatchView::WatchView(WatchController& controller,lv_display_t* display)
    : controller_(controller),display_(display) {
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
void WatchView::draw_sprite(lv_event_t* event) {
    constexpr auto width = static_cast<int>(ember_width);
    constexpr auto height = static_cast<int>(ember_height);
    const auto& pixels = static_cast<const WatchView*>(lv_event_get_user_data(event))->sprite_pixels_;
    auto* object = lv_event_get_current_target_obj(event);
    auto* layer = lv_event_get_layer(event);
    lv_area_t bounds{};
    lv_obj_get_coords(object, &bounds);
    const int cell = lv_obj_get_width(object) / width;
    lv_draw_rect_dsc_t ink{};
    lv_draw_rect_dsc_init(&ink);
    ink.bg_color = lv_color_hex(0x1f281a);
    for (int y = 0; y < height; ++y) {
        const auto row = static_cast<std::size_t>(y * width);
        auto active = [&](int x) { return pixels[row + static_cast<std::size_t>(x)]; };
        for (int x = 0; x < width;) {
            if (!active(x)) { ++x; continue; }
            const int start = x++;
            while (x < width && active(x)) ++x;
            const lv_area_t run{bounds.x1 + start * cell, bounds.y1 + y * cell,
                                bounds.x1 + x * cell - 1, bounds.y1 + (y + 1) * cell - 1};
            lv_draw_rect(layer, &ink, &run);
        }
    }
}
void WatchView::place_sprite(lv_obj_t* parent, AppId app) {
    constexpr std::uint32_t lcd_green = 0xc5e4c4;
    constexpr int padding = 10;
    const bool close_up = app == AppId::creature;
    const int cell = close_up ? 12 : 6;
    const int width = static_cast<int>(ember_width) * cell;
    const int height = static_cast<int>(ember_height) * cell;
    const int x = (close_up ? 205 : 300) - width / 2;
    const int y = (close_up ? 194 : 250) - height / 2;
    panel(parent, x - padding, y - padding, width + 2 * padding, height + 2 * padding, lcd_green);
    sprite_ = lv_obj_create(parent);
    lv_obj_remove_style_all(sprite_);
    lv_obj_remove_flag(sprite_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(sprite_, x, y);
    lv_obj_set_size(sprite_, width, height);
    lv_obj_add_event_cb(sprite_, draw_sprite, LV_EVENT_DRAW_MAIN, this);
}
void WatchView::build() {
    lv_display_set_default(display_);
    auto* root = lv_display_get_screen_active(display_); lv_obj_clean(root);
    binding_count_ = 0; watch_root_=root;
    time_label_=battery_label_=status_label_=message_label_=sprite_=clock_label_=date_label_=setting_label_=nullptr;
    last_frame_.reset();
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
        place_sprite(root, app);
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
        place_sprite(root, app);
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
    next_update_ = real_ms + 1000U;
    if (sprite_) {
        constexpr std::uint32_t blink_period_ms = 3700;
        constexpr std::uint32_t blink_start_ms = 3570;
        const bool can_blink = state.face != CreatureFace::resting && state.face != CreatureFace::happy;
        const auto phase = real_ms % blink_period_ms;
        const bool blink = can_blink && phase >= blink_start_ms;
        const EmberFrame frame{state.face, blink};
        if (frame != last_frame_) {
            paint_face(sprite_pixels_, frame);
            lv_obj_invalidate(sprite_);
            last_frame_ = frame;
        }
        if (can_blink) {
            const auto until_blink_edge = (blink ? blink_period_ms : blink_start_ms) - phase;
            next_update_ = real_ms + std::min(1000U, until_blink_edge);
        }
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
