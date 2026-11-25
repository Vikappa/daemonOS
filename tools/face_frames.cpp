#include "ember_sprite.hpp"
#include "daemon/services/speech_motion.hpp"
#include <iostream>

using namespace daemonos;
namespace {
void frame(presentation::EmberFrame pose) {
    presentation::EmberPixels pixels{}; presentation::paint_face(pixels,pose);
    std::cout<<'[';
    for(std::size_t y=0;y<presentation::ember_height;++y) {
        if(y) std::cout<<',';
        std::cout<<'"';
        for(std::size_t x=0;x<presentation::ember_width;++x) std::cout<<(pixels[y*presentation::ember_width+x] ? '#' : '.');
        std::cout<<'"';
    }
    std::cout<<']';
}
}
int main() {
    std::cout<<"{\"width\":"<<presentation::ember_width<<",\"height\":"<<presentation::ember_height<<",\"states\":[";
    for(unsigned i=0;i<static_cast<unsigned>(CreatureFace::count);++i) {
        if(i) std::cout<<',';
        const auto face=static_cast<CreatureFace>(i);
        std::cout<<"{\"name\":\""<<face_name(face)<<"\",\"label\":\""<<face_label(face)<<"\",\"frames\":[";
        for(std::uint8_t phase=0;phase<4;++phase) {
            if(phase) std::cout<<',';
            frame({face,false,MouthPose::expression,phase});
        }
        std::cout<<"]}";
    }
    std::cout<<"],\"speech\":[";
    SpeechMotion speech; speech.start(1,0,2000);
    for(int i=0;i<8;++i) { if(i) std::cout<<','; frame({CreatureFace::calm,false,speech.mouth(i*90),0}); }
    std::cout<<"]}\n";
}
