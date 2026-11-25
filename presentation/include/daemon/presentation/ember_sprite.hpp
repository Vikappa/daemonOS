#pragma once
#include "daemon/types.hpp"

namespace daemonos::presentation {
struct EmberFrame {
    CreatureFace face;
    bool blink;
    bool operator==(const EmberFrame&) const = default;
};

inline constexpr std::size_t ember_width = 13;
inline constexpr std::size_t ember_height = 12;
using EmberPixels = std::array<bool, ember_width * ember_height>;

// Off pixels let the same LCD background show through and around the head.
void paint_face(EmberPixels& pixels, EmberFrame frame);
}
