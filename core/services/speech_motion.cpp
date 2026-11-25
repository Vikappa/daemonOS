#include "daemon/services/speech_motion.hpp"

namespace daemonos {
void SpeechMotion::start(std::uint32_t token,std::int64_t now,std::int32_t duration_ms) {
    token_=token; started_=sampled_=now; amplitude_=0; has_level_=false; playing_=true;
    until_=now+(duration_ms>0 ? std::clamp<std::int32_t>(duration_ms,1,300'000) : 30'000);
}
void SpeechMotion::level(std::uint32_t token,int amplitude,std::int64_t now) {
    if(!active(now) || token!=token_) return;
    amplitude_=std::clamp(amplitude,0,100); sampled_=now; has_level_=true;
    until_=std::max(until_,now+3000);
}
void SpeechMotion::stop(std::uint32_t token) { if(token==token_) playing_=false; }
MouthPose SpeechMotion::mouth(std::int64_t now) const {
    if(!active(now)) return MouthPose::expression;
    if(has_level_) {
        if(now-sampled_>250 || amplitude_<8) return MouthPose::closed;
        if(amplitude_<35) return MouthPose::half;
        return ((now-started_)/90)%3==1 ? MouthPose::half : MouthPose::open;
    }
    // A bounded fallback for an adapter with playback timing but no audio envelope.
    constexpr std::array pattern{MouthPose::closed,MouthPose::half,MouthPose::open,MouthPose::half,
                                 MouthPose::closed,MouthPose::open,MouthPose::half,MouthPose::closed};
    return pattern[static_cast<std::size_t>(std::max<std::int64_t>(0,now-started_)/90)%pattern.size()];
}
}
