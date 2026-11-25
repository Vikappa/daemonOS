#include "sdl_hal.hpp"
#include <stdexcept>

namespace daemonos::simulator {
SdlDisplay::SdlDisplay(const char* title, int width, int height, int x, int y, bool hidden,ResourceManager* resources)
    : width_(width), height_(height), frame_(static_cast<std::size_t>(width * height), 0xff000000U) {
    const auto pixels=static_cast<std::size_t>(width*40);
    if (resources) {
        draw_memory_=resources->allocate(MemoryCategory::graphics,MemoryPool::external,pixels*4);
        if (!draw_memory_) throw std::runtime_error("display draw buffer budget exhausted");
    } else draw_buffer_.resize(pixels);
    window_ = SDL_CreateWindow(title,x,y,width,height,hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    if (window_) renderer_ = SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
    if (renderer_) texture_ = SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
    if (!window_ || !renderer_ || !texture_) {
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetTextureBlendMode(texture_,SDL_BLENDMODE_NONE);
    display_ = lv_display_create(width,height);
    if (!display_) throw std::runtime_error("LVGL display allocation failed");
    lv_display_set_user_data(display_,this);
    lv_display_set_color_format(display_,LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display_,draw_memory_ ? draw_memory_.data() : draw_buffer_.data(),nullptr,static_cast<std::uint32_t>(pixels*4),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_,flush_cb);
    input_ = lv_indev_create();
    lv_indev_set_type(input_,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input_,display_);
    lv_indev_set_user_data(input_,this);
    lv_indev_set_read_cb(input_,read_cb);
    lv_timer_pause(lv_indev_get_read_timer(input_));
}
SdlDisplay::~SdlDisplay() {
    if (input_) lv_indev_delete(input_);
    if (display_) lv_display_delete(display_);
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}
Status SdlDisplay::flush(int x, int y, int width, int height, std::span<const std::uint32_t> pixels) {
    if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > width_ || y + height > height_ || pixels.size() < static_cast<std::size_t>(width * height))
        return Status::fail(Error::invalid,"display flush bounds");
    for (int row = 0; row < height; ++row)
        std::copy_n(pixels.data() + row * width,width,frame_.data() + (y + row) * width_ + x);
    dirty_ = true; ++flushes_; return {};
}
void SdlDisplay::brightness(int percent) {
    const auto value = static_cast<std::uint8_t>(std::clamp(percent,0,100) * 255 / 100);
    SDL_SetTextureColorMod(texture_,value,value,value); dirty_ = true;
}
void SdlDisplay::flush_cb(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels) {
    auto& self = *static_cast<SdlDisplay*>(lv_display_get_user_data(display));
    const int width = area->x2 - area->x1 + 1, height = area->y2 - area->y1 + 1;
    const auto status = self.flush(area->x1,area->y1,width,height,{reinterpret_cast<const std::uint32_t*>(pixels),static_cast<std::size_t>(width * height)});
    if (!status) SDL_Log("Display flush failed: %s",status.message.c_str());
    lv_display_flush_ready(display);
}
void SdlDisplay::read_cb(lv_indev_t* input, lv_indev_data_t* data) {
    const auto point = static_cast<SdlDisplay*>(lv_indev_get_user_data(input))->read();
    data->point.x = point.x; data->point.y = point.y;
    data->state = point.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void SdlDisplay::event(const SDL_Event& event) {
    if (event.type == SDL_MOUSEMOTION && event.motion.windowID == window_id()) {
        pointer_.x = event.motion.x; pointer_.y = event.motion.y;
        if (pointer_.pressed) lv_indev_read(input_);
    } else if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.windowID == window_id() && event.button.button == SDL_BUTTON_LEFT) {
        click(event.button.x,event.button.y,event.type == SDL_MOUSEBUTTONDOWN);
        lv_indev_read(input_);
    } else if (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id()) {
        if (event.window.event == SDL_WINDOWEVENT_EXPOSED) dirty_ = true;
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) pointer_.pressed = false;
    }
}
void SdlDisplay::click(int x, int y, bool down) {
    pointer_ = {std::clamp(x,0,width_ - 1),std::clamp(y,0,height_ - 1),down};
}
void SdlDisplay::present() {
    if (!dirty_) return;
    SDL_UpdateTexture(texture_,nullptr,frame_.data(),width_ * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderClear(renderer_); SDL_RenderCopy(renderer_,texture_,nullptr,nullptr); SDL_RenderPresent(renderer_);
    dirty_ = false; ++frames_;
}
Status SdlDisplay::screenshot(const std::string& path) const {
    auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint32_t*>(frame_.data()),width_,height_,32,width_ * 4,SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return Status::fail(Error::io,SDL_GetError());
    const int result = SDL_SaveBMP(surface,path.c_str()); SDL_FreeSurface(surface);
    return result == 0 ? Status{} : Status::fail(Error::io,SDL_GetError());
}
}
