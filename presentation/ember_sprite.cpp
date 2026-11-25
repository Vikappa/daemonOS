#include "daemon/presentation/ember_sprite.hpp"

namespace daemonos::presentation {
namespace {
constexpr std::array<std::string_view, ember_height> silhouette{
    "..##.....##..",
    "..###...###..",
    "..#########..",
    ".###########.",
    "#############",
    "#############",
    "#############",
    "#############",
    "#############",
    ".###########.",
    "..#########..",
    "...#######...",
};

// Only these cutouts change. Eyes start at row 5, mouths at row 9.
using Eyes = std::array<std::string_view, 3>;
using Mouth = std::array<std::string_view, 2>;
struct Expression { Eyes eyes; Mouth mouth; };
constexpr Expression calm{{"", "...##...##...", "...##...##..."}, {".....###.....", ""}};
constexpr Eyes blink{"", "", "..###...###.."};

constexpr Expression expression(CreatureFace face, std::uint8_t phase) {
    switch (face) {
    case CreatureFace::calm:
        return calm;
    case CreatureFace::happy:
        return {{"", "...##...##...", "..#..#.#..#.."}, {".....###.....", ""}};
    case CreatureFace::curious:
        return {{"..###........", "..###...##...", "..###...##..."}, {"......#......", ""}};
    case CreatureFace::sad:
        return {{"", "....#...#....", ""}, {".....###.....", "....#...#...."}};
    case CreatureFace::hungry:
        return {{"", "..###...###..", "..###...###.."}, {"......##.....", "......##....."}};
    case CreatureFace::tired:
        return {{"", "..###...###..", ""}, {".....###.....", ".....###....."}};
    case CreatureFace::resting:
        return {{"..###...###..", "", ""}, {"......#......", ""}};
    case CreatureFace::thinking:
        return {{"...##........", "...##....#...", "...##........"}, {"......##.....", ""}};
    case CreatureFace::charging:
        return {{"", "..####.####..", ""}, {"....#...#....", "....#####...."}};
    case CreatureFace::installing_services:
        return phase%2 ? Expression{{"..##.....##..", "..#.#...#.#..", "..###...###.."}, {"....#####....", ""}} :
                         Expression{{"..###...###..", "..#.#...#.#..", "..##.....##.."}, {".....###.....", "....#...#...."}};
    case CreatureFace::phone_offline:
        return {{"..#.......#..", "...#.....#...", "....#...#...."}, {"....##.##....", ""}};
    case CreatureFace::inference_unreachable:
        return {{"..###........", "..#.#....#...", "..###....#..."}, {".....#.#.....", "......#......"}};
    case CreatureFace::internal_error:
        return {{"..#.#...#.#..", "...#.....#...", "..#.#...#.#.."}, {"....#.#.#....", ".....#.#....."}};
    case CreatureFace::connecting: {
        constexpr std::array gaze{"..##.....##..", "...##...##...", "....##.##....", "...##...##..."};
        return {{"", gaze[phase%gaze.size()], ""}, {".....###.....", ""}};
    }
    case CreatureFace::waiting_response: {
        constexpr std::array dots{"......#......", ".....#.#.....", "....#.#.#....", ".....#.#....."};
        return {{"", "..###...###..", ""}, {dots[phase%dots.size()], ""}};
    }
    case CreatureFace::listening:
        return {{"..###...###..", "..#.#...#.#..", "..###...###.."}, {".....###.....", ""}};
    case CreatureFace::count: break;
    }
    return calm;
}

bool marked(std::string_view row, std::size_t x) {
    return x < row.size() && row[x] == '#';
}
}

void paint_face(EmberPixels& pixels, EmberFrame frame) {
    const auto pose = expression(frame.face,frame.phase);
    const auto& eyes = frame.blink ? blink : pose.eyes;
    const Mouth speech=frame.mouth==MouthPose::closed ? Mouth{".....###.....",""} :
                       frame.mouth==MouthPose::half ? Mouth{"......##.....","......##....."} :
                       Mouth{".....###.....",".....###....."};
    const auto& mouth=frame.mouth==MouthPose::expression ? pose.mouth : speech;
    for (std::size_t y = 0; y < ember_height; ++y) {
        const std::string_view cutout = y >= 5 && y < 8 ? eyes[y - 5] :
                                        y >= 9 && y < 11 ? mouth[y - 9] : "";
        for (std::size_t x = 0; x < ember_width; ++x) {
            pixels[y * ember_width + x] = silhouette[y][x] == '#' && !marked(cutout, x);
        }
    }
}
}
