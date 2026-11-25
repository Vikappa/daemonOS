#pragma once
#include "daemon/types.hpp"

namespace daemonos {
// Playback-driven mouth movement. Text arrival alone must never start it.
class SpeechMotion {
public:
    void start(std::uint32_t token,std::int64_t now,std::int32_t duration_ms = 0);
    void level(std::uint32_t token,int amplitude,std::int64_t now);
    void stop(std::uint32_t token);
    void cancel() { playing_=false; }
    bool active(std::int64_t now) const { return playing_ && now<until_; }
    MouthPose mouth(std::int64_t now) const;
private:
    std::uint32_t token_{};
    std::int64_t started_{}, until_{}, sampled_{};
    int amplitude_{};
    bool playing_{}, has_level_{};
};
}
