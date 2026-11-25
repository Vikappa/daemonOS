#include "daemon/presentation/watch_view.hpp"
#include "ember_pixels.hpp"

namespace daemonos::presentation {
const lv_image_dsc_t& ember_sprite() {
    static const auto descriptor=[] {
        lv_image_dsc_t image{};
        image.header.magic=LV_IMAGE_HEADER_MAGIC; image.header.cf=LV_COLOR_FORMAT_ARGB8888;
        image.header.w=sprite_width; image.header.h=sprite_height; image.header.stride=sprite_width*4;
        image.data_size=sizeof(sprite_data); image.data=reinterpret_cast<const std::uint8_t*>(sprite_data);
        return image;
    }();
    return descriptor;
}
}
