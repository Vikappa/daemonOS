#pragma once
#include "daemon/hal/interfaces.hpp"
#include "daemon/services/resources.hpp"
#include <SDL.h>
#include <lvgl.h>
#include <memory>
#include <vector>

namespace daemonos::simulator {
class SdlDisplay final : public hal::Display, public hal::Touch {
public:
    SdlDisplay(const char* title, int width, int height, int x, int y, bool hidden,ResourceManager* resources=nullptr);
    ~SdlDisplay() override;
    SdlDisplay(const SdlDisplay&) = delete;
    SdlDisplay& operator=(const SdlDisplay&) = delete;
    Status flush(int x, int y, int width, int height, std::span<const std::uint32_t> pixels) override;
    void brightness(int percent) override;
    hal::TouchPoint read() const override { return pointer_; }
    void event(const SDL_Event& event);
    void present();
    Status screenshot(const std::string& path) const;
    lv_display_t* lv_display() const { return display_; }
    std::uint32_t window_id() const { return SDL_GetWindowID(window_); }
    std::size_t rendered_frames() const { return frames_; }
    std::size_t flush_count() const { return flushes_; }
    void click(int x, int y, bool down);
private:
    static void flush_cb(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels);
    static void read_cb(lv_indev_t* input, lv_indev_data_t* data);
    int width_{}, height_{};
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    lv_display_t* display_{};
    lv_indev_t* input_{};
    std::vector<std::uint32_t> frame_;
    std::vector<std::uint32_t> draw_buffer_;
    ResourceManager::Lease draw_memory_{};
    hal::TouchPoint pointer_{};
    bool dirty_{true};
    std::size_t frames_{};
    std::size_t flushes_{};
};
}
