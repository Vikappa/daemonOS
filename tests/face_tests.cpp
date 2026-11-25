#include "mocks/platform.hpp"
#include "ember_sprite.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace daemonos;
using namespace daemonos::presentation;
namespace {
int checks{};
#define CHECK(c) do { ++checks; if(!(c)) throw std::runtime_error(std::string{__FILE__}+":"+std::to_string(__LINE__)+": " #c); } while(false)
void silhouette_and_state_identity() {
    EmberPixels calm{}; paint_face(calm,{CreatureFace::calm,false});
    constexpr std::array left{2,2,2,1,0,0,0,0,0,1,2,3};
    constexpr std::array right{10,10,10,11,12,12,12,12,12,11,10,9};
    std::set<EmberPixels> identities;
    for(unsigned i=0;i<static_cast<unsigned>(CreatureFace::count);++i) {
        const auto face=static_cast<CreatureFace>(i); EmberPixels first{}; paint_face(first,{face,false}); identities.insert(first);
        CHECK(!face_name(face).empty()); CHECK(!face_label(face).empty());
        for(std::uint8_t phase=0;phase<4;++phase) for(const auto mouth:{MouthPose::expression,MouthPose::closed,MouthPose::half,MouthPose::open}) {
            EmberPixels pixels{}; paint_face(pixels,{face,false,mouth,phase});
            for(std::size_t y=0;y<ember_height;++y) for(std::size_t x=0;x<ember_width;++x) {
                const auto index=y*ember_width+x;
                if(y<5 || y==8 || y==11) CHECK(pixels[index]==calm[index]);
                if(static_cast<int>(x)<left[y] || static_cast<int>(x)>right[y]) CHECK(!pixels[index]);
            }
        }
    }
    CHECK(identities.size()==static_cast<unsigned>(CreatureFace::count));
    CHECK(!calm[6*ember_width+3]); CHECK(!calm[9*ember_width+6]); CHECK(calm[6*ember_width+6]);
}
void animation_changes_only_features() {
    for(const auto face:{CreatureFace::installing_services,CreatureFace::connecting,CreatureFace::waiting_response}) {
        EmberPixels first{},second{}; paint_face(first,{face,false,MouthPose::expression,0}); paint_face(second,{face,false,MouthPose::expression,1}); CHECK(first!=second);
    }
    for(const auto face:{CreatureFace::calm,CreatureFace::phone_offline,CreatureFace::inference_unreachable,CreatureFace::internal_error,CreatureFace::listening}) {
        EmberPixels first{},second{}; paint_face(first,{face,false,MouthPose::expression,0}); paint_face(second,{face,false,MouthPose::expression,3}); CHECK(first==second);
    }
    for(const auto face:{CreatureFace::calm,CreatureFace::happy,CreatureFace::phone_offline}) {
        EmberPixels closed{},half{},open{};
        paint_face(closed,{face,false,MouthPose::closed}); paint_face(half,{face,false,MouthPose::half}); paint_face(open,{face,false,MouthPose::open});
        CHECK(closed!=half); CHECK(half!=open); CHECK(open!=closed);
        for(std::size_t y=0;y<ember_height;++y) if(y<9 || y>10)
            for(std::size_t x=0;x<ember_width;++x) CHECK(closed[y*ember_width+x]==open[y*ember_width+x]);
    }
}
void playback_timing_and_audio_envelope() {
    SpeechMotion speech; CHECK(!speech.active(0)); CHECK(speech.mouth(0)==MouthPose::expression);
    speech.start(7,1000,2000); CHECK(speech.active(1000));
    CHECK(speech.mouth(1000)==MouthPose::closed); CHECK(speech.mouth(1090)==MouthPose::half); CHECK(speech.mouth(1180)==MouthPose::open);
    speech.stop(6); CHECK(speech.active(1180)); speech.level(6,0,1180); CHECK(speech.mouth(1180)==MouthPose::open);
    speech.level(7,0,1200); CHECK(speech.mouth(1200)==MouthPose::closed);
    speech.level(7,20,1250); CHECK(speech.mouth(1250)==MouthPose::half);
    speech.level(7,80,1300); CHECK(speech.mouth(1300)!=MouthPose::closed); CHECK(speech.mouth(1551)==MouthPose::closed);
    speech.stop(7); CHECK(speech.mouth(1551)==MouthPose::expression);
    speech.start(8,2000); speech.stop(7); speech.level(7,0,2000); CHECK(speech.active(2000)); CHECK(!speech.active(32'000));
    speech.start(9,3000,2000); speech.cancel(); CHECK(!speech.active(3000));
}
struct Provider final : InferenceProvider {
    InferencePhase state{InferencePhase::waiting_response}; bool hold{true}, fail{};
    std::size_t offset{};
    static constexpr std::string_view reply=R"({"speech":"ciao","emotion":"happy","intensity":0.5,"animation":"idle","actions":["speak","animate"]})";
    Status start(std::string_view,std::uint32_t) override { offset=0; return fail ? Status::fail(Error::unavailable,"remote unavailable") : Status{}; }
    Status poll(std::span<char> chunk,std::size_t& size,bool& done) override {
        size=0; done=false;
        if(hold) return {};
        size=std::min(chunk.size(),reply.size()-offset); std::copy_n(reply.data()+offset,size,chunk.data()); offset+=size; done=offset==reply.size(); return {};
    }
    void cancel() override {}
    InferencePhase phase() const override { return state; }
};
void runtime_activity_and_no_text_lipsync() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; Provider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); CHECK(runtime.face()==CreatureFace::calm);
    runtime.post({EventType::services_install,1}); runtime.tick(); CHECK(runtime.face()==CreatureFace::installing_services);
    runtime.post({EventType::internal_fault,1}); runtime.tick(); CHECK(runtime.face()==CreatureFace::internal_error);
    runtime.post({EventType::internal_fault,0}); runtime.post({EventType::services_install,0}); runtime.post({EventType::phone_link,0}); runtime.tick(); CHECK(runtime.face()==CreatureFace::phone_offline);
    runtime.post({EventType::phone_link,1}); runtime.tick(); CHECK(runtime.face()==CreatureFace::connecting);
    runtime.post({EventType::phone_link,-1}); runtime.post({EventType::inference_link,0}); runtime.tick(); CHECK(runtime.face()==CreatureFace::inference_unreachable);
    runtime.post({EventType::inference_link,1}); runtime.tick(); CHECK(runtime.face()==CreatureFace::connecting);
    runtime.post({EventType::inference_link,-1}); runtime.post({EventType::rest}); runtime.tick(); CHECK(runtime.face()==CreatureFace::resting);
    runtime.post({EventType::listen_start,1000,7}); runtime.tick(); CHECK(runtime.face()==CreatureFace::listening); CHECK(!runtime.creature().sleeping); CHECK(runtime.power()==PowerState::listening);
    runtime.post({EventType::listen_stop,0,6}); runtime.tick(); CHECK(runtime.listening());
    runtime.post({EventType::listen_stop,0,7}); runtime.post({EventType::connectivity,1}); runtime.tick();
    provider.state=InferencePhase::connecting; CHECK(runtime.request("Ciao")); runtime.tick(); CHECK(runtime.face()==CreatureFace::connecting);
    provider.state=InferencePhase::waiting_response; runtime.tick(); CHECK(runtime.face()==CreatureFace::waiting_response);
    provider.hold=false; clock.advance(20); runtime.tick(); CHECK(runtime.face()==CreatureFace::happy); CHECK(!runtime.speaking()); CHECK(runtime.mouth()==MouthPose::expression);
    runtime.post({EventType::speech_start,2000,8}); runtime.tick(); CHECK(runtime.speaking()); CHECK(runtime.power()==PowerState::speaking);
    const auto closed=runtime.mouth(); clock.advance(180); runtime.tick(); CHECK(runtime.mouth()!=closed);
    runtime.post({EventType::speech_level,0,8}); runtime.tick(); CHECK(runtime.mouth()==MouthPose::closed);
    runtime.post({EventType::speech_stop,0,7}); runtime.tick(); CHECK(runtime.speaking());
    runtime.post({EventType::speech_stop,0,8}); runtime.tick(); CHECK(!runtime.speaking()); CHECK(runtime.mouth()==MouthPose::expression);
    clock.advance(60'000); provider.fail=true; CHECK(!runtime.request("Riprova")); CHECK(runtime.face()==CreatureFace::inference_unreachable);
}
}
int main() {
    const std::pair<const char*,void(*)()> cases[]={
        {"approved silhouette and distinct activity faces",silhouette_and_state_identity}, {"feature-only animation",animation_changes_only_features},
        {"playback timing, silence and stale audio callbacks",playback_timing_and_audio_envelope}, {"runtime activity routing and text/audio separation",runtime_activity_and_no_text_lipsync}};
    int failures{};
    for(const auto& [name,run]:cases) { try { run(); std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& error) { ++failures; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; } }
    std::cout<<std::size(cases)<<" cases, "<<checks<<" checks, "<<failures<<" failures\n"; return failures ? 1 : 0;
}
