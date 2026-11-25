#include "ui.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

namespace {
using namespace daemonos;
struct Options {
    std::filesystem::path data{"var/simulator"};
    std::string script, screenshot, debug_screenshot, report, backup,name;
    std::string agent{DAEMON_DEFAULT_AGENT};
    bool headless{};
    std::uint32_t frames{};
    std::int64_t mock_delay_ms{};
    std::optional<std::int64_t> epoch;
    std::optional<std::uint32_t> seed;
};
template<class T> T number(std::string_view value) {
    T result{}; const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size()) throw std::runtime_error("Invalid numeric argument");
    return result;
}
Options options(int argc, char** argv) {
    Options o;
    for (int i=1;i<argc;++i) {
        const std::string_view arg{argv[i]};
        if (arg=="--help") {
            std::cout << "DaeMON OS Habitat v0.2.0\n"
                "daemon_sim [--data-dir PATH] [--headless] [--frames N] [--epoch UNIX]\n"
                "           [--seed N] [--script PATH] [--screenshot PATH.bmp]\n"
                "           [--debug-screenshot PATH.bmp] [--report PATH.json] [--backup PATH.sqlite]\n"
                "           [--mock-delay-ms 0..9000]\n"
                "           [--agent PATH.md] [--name NAME]\n"
                "Headless mode uses deterministic 16 ms ticks and SDL's dummy driver.\n"
                "Scripts: click TARGET, advance SECONDS, wall EPOCH, wait FRAMES, activity STATE.\n";
            std::exit(0);
        }
        if (arg=="--headless") { o.headless=true; continue; }
        if (i+1>=argc) throw std::runtime_error("Missing argument value");
        const std::string value{argv[++i]};
        if (arg=="--data-dir") o.data=value;
        else if (arg=="--frames") o.frames=number<std::uint32_t>(value);
        else if (arg=="--epoch") { o.epoch=number<std::int64_t>(value); if (*o.epoch < 0 || *o.epoch > 4'102'444'800LL) throw std::runtime_error("Epoch outside 1970-2100"); }
        else if (arg=="--seed") o.seed=number<std::uint32_t>(value);
        else if (arg=="--script") o.script=value;
        else if(arg=="--mock-delay-ms") { o.mock_delay_ms=number<std::int64_t>(value); if(o.mock_delay_ms<0 || o.mock_delay_ms>9000) throw std::runtime_error("Mock delay outside 0..9000 ms"); }
        else if (arg=="--screenshot") o.screenshot=value;
        else if (arg=="--debug-screenshot") o.debug_screenshot=value;
        else if (arg=="--report") o.report=value;
        else if (arg=="--backup") o.backup=value;
        else if (arg=="--agent") o.agent=value;
        else if (arg=="--name") o.name=value;
        else throw std::runtime_error("Unknown argument: "+std::string{arg});
    }
    if (o.headless && o.frames==0) o.frames=120;
    if (!o.script.empty() && o.frames==0) throw std::runtime_error("Script execution requires --frames");
    return o;
}
struct ScriptCommand { std::string name; std::string value; };
struct SimulatorProvider final : InferenceProvider {
    hal::Clock& clock; std::int64_t delay{}, ready{}; MockProvider mock;
    SimulatorProvider(hal::Clock& value,std::int64_t milliseconds) : clock(value),delay(milliseconds) {}
    Status start(std::string_view context,std::uint32_t tokens) override {
        ready=clock.monotonic_ms()+delay; return mock.start(context,tokens);
    }
    Status poll(std::span<char> chunk,std::size_t& size,bool& done) override {
        if(clock.monotonic_ms()<ready) { size=0; done=false; return {}; }
        return mock.poll(chunk,size,done);
    }
    void cancel() override { mock.cancel(); }
};
std::vector<ScriptCommand> script(const std::string& path) {
    std::vector<ScriptCommand> result;
    if (path.empty()) return result;
    std::ifstream file(path); if (!file) throw std::runtime_error("Cannot open simulator script");
    std::string line;
    while (std::getline(file,line)) {
        if (line.empty() || line.front()=='#') continue;
        if (result.size()>=256 || line.size()>128) throw std::runtime_error("Script budget exceeded");
        std::istringstream input(line); ScriptCommand command; std::string extra;
        if (!(input>>command.name>>command.value) || (input>>extra)) throw std::runtime_error("Invalid script command");
        result.push_back(std::move(command));
    }
    return result;
}
void mouse(daemonos::simulator::SdlDisplay& display,int x,int y,bool down) {
    SDL_Event event{}; event.type=down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.windowID=display.window_id(); event.button.button=SDL_BUTTON_LEFT;
    event.button.x=x; event.button.y=y;
    if (SDL_PushEvent(&event)<0) throw std::runtime_error(SDL_GetError());
}
Event activity(std::string_view name) {
    if(name=="install") return {EventType::services_install,1};
    if(name=="installed") return {EventType::services_install,0};
    if(name=="phone_offline") return {EventType::phone_link,0};
    if(name=="phone_connecting") return {EventType::phone_link,1};
    if(name=="phone_ready") return {EventType::phone_link,2};
    if(name=="phone_auto") return {EventType::phone_link,-1};
    if(name=="inference_offline") return {EventType::inference_link,0};
    if(name=="inference_connecting") return {EventType::inference_link,1};
    if(name=="inference_ready") return {EventType::inference_link,2};
    if(name=="inference_auto") return {EventType::inference_link,-1};
    if(name=="error") return {EventType::internal_fault,1};
    if(name=="clear_error") return {EventType::internal_fault,0};
    if(name=="listen") return {EventType::listen_start,10'000,42};
    if(name=="stop_listen") return {EventType::listen_stop,0,42};
    if(name=="speak") return {EventType::speech_start,10'000,42};
    if(name=="voice") return {EventType::speech_level,80,42};
    if(name=="silence") return {EventType::speech_level,0,42};
    if(name=="stop_speak") return {EventType::speech_stop,0,42};
    throw std::runtime_error("Unknown activity state");
}
void report(const Options& o, const Runtime& r, std::size_t frames, std::size_t clicks,std::size_t asleep_flushes) {
    if (o.report.empty()) return;
    std::ofstream output(o.report);
    const auto& s=r.creature();
    output << "{\n\"id\":\"" << s.id.c_str() << "\",\n\"name\":\""<<s.name.c_str()<<"\",\n\"age_seconds\":" << s.age_seconds
        << ",\n\"revision\":" << s.revision << ",\n\"hunger\":" << s.needs.hunger << ",\n\"energy\":" << s.needs.energy
        << ",\n\"sleeping\":" << (s.sleeping ? "true" : "false")
        << ",\n\"app\":" << static_cast<int>(r.app()) << ",\n\"steps\":" << r.environment().steps
        << ",\n\"battery\":" << r.environment().battery << ",\n\"brightness\":" << r.settings().brightness
        << ",\n\"charging\":" << (r.environment().charging ? "true" : "false")
        << ",\n\"connected\":" << (r.environment().connected ? "true" : "false")
        << ",\n\"thinking\":" << (r.thinking() ? "true" : "false")
        << ",\n\"face\":\"" << face_name(r.face()) << "\",\n\"mouth\":" << static_cast<unsigned>(r.mouth())
        << ",\n\"speaking\":" << (r.speaking() ? "true" : "false")
        << ",\n\"listening\":" << (r.listening() ? "true" : "false")
        << ",\n\"mock_reply\":" << (r.message().starts_with("Sono qui, capo.") ? "true" : "false")
        << ",\n\"power\":\"" << power_name(r.power()) << "\",\n\"rendered_frames\":" << frames
        << ",\n\"mouse_clicks\":" << clicks
        << ",\n\"asleep_flushes\":" << asleep_flushes
        << ",\n\"simulation_updates\":" << r.simulation_updates()
        << ",\n\"wake_locks\":" << r.power_manager().active_locks()
        << ",\n\"last_sleep_ms\":" << r.last_sleep_ms()
        << ",\n\"memory_peak_external\":" << r.resources().peak(MemoryPool::external)
        << ",\n\"cognitive_bytes\":" << r.resources().used(MemoryCategory::cognitive)
        << ",\n\"queue_peak\":" << r.event_queue_peak() << "\n}\n";
    if (!output) throw std::runtime_error("Cannot write simulator report");
}
}
int main(int argc,char** argv) {
    try {
        const auto o=options(argc,argv); const auto commands=script(o.script);
        if (o.headless) { SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1); }
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_EVENTS)!=0) throw std::runtime_error(SDL_GetError());
        struct SdlGuard { ~SdlGuard() { SDL_Quit(); } } sdl_guard;
        desktop::Allocator allocator;
        MemoryBudget limits; limits.categories[static_cast<std::size_t>(MemoryCategory::database)]=1024*1024;
        ResourceManager resources(limits,&allocator);
        auto task_memory=resources.reserve(MemoryCategory::runtime,MemoryPool::internal,32*1024);
        auto graphics=resources.reserve(MemoryCategory::graphics,MemoryPool::external,512*1024);
        auto database_heap=resources.allocate(MemoryCategory::database,MemoryPool::external,1024*1024);
        if (!task_memory || !graphics || !database_heap || !desktop::SqliteStore::configure_heap(database_heap.data(),database_heap.bytes().size())) throw std::runtime_error("cannot establish bounded desktop memory arenas");
        struct SqliteGuard { ~SqliteGuard() { (void)desktop::SqliteStore::shutdown(); } } sqlite_guard;
        lv_init(); struct LvGuard { ~LvGuard() { lv_deinit(); } } lv_guard;
        const auto system_epoch=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        desktop::VirtualClock clock(o.epoch.value_or(system_epoch)); if (o.headless) clock.speed(0);
        desktop::Random random=o.seed ? desktop::Random{*o.seed} : desktop::Random{};
        desktop::SqliteStore store(o.data/"sd"/"creature.sqlite");
        desktop::FileCheckpoint checkpoint(o.data/"flash"/"identity.checkpoint");
        SimulatorProvider provider(clock,o.mock_delay_ms);
        VoiceDirectory identities(clock,random,store);
        Runtime runtime(clock,random,store,checkpoint,provider,&identities,&resources);
        auto status=runtime.boot(); if (!status) throw std::runtime_error("Boot: "+std::string{status.message.view()});
        if (!o.name.empty() && !runtime.name(o.name)) throw std::runtime_error("Invalid familiar name");
        if (!o.agent.empty()) {
            std::ifstream file(o.agent,std::ios::binary); std::array<char,1024> instructions{};
            if (!file) throw std::runtime_error("Cannot read agent.md");
            file.read(instructions.data(),instructions.size());
            const auto bytes=static_cast<std::size_t>(file.gcount());
            if (file.bad() || !runtime.configure_agent({instructions.data(),bytes})) throw std::runtime_error("Invalid or oversized agent.md");
        }
        simulator::SdlDisplay watch("DaemonOS / T-Watch Ultra / 410 x 502",410,502,60,80,o.headless,&resources);
        simulator::SdlDisplay debug("DaemonOS / Habitat Lab",460,960,500,40,o.headless,&resources);
        simulator::Ui ui(runtime,clock,watch,debug);
        std::cout << "{\"event\":\"boot\",\"version\":\"0.2.0\",\"sqlite\":\"" << desktop::SqliteStore::version() << "\",\"identity\":\"" << runtime.creature().id.c_str() << "\"}\n";
        bool running=true; std::uint32_t frame=0; auto previous=SDL_GetTicks();
        std::size_t command_index=0, clicks=0,asleep_flushes=0;
        std::uint32_t next_command=6, release_at=0;
        simulator::SdlDisplay* pressed=nullptr; int pressed_x=0,pressed_y=0;
        while (running && (o.frames==0 || frame<o.frames)) {
            if (pressed && frame>=release_at) { mouse(*pressed,pressed_x,pressed_y,false); pressed=nullptr; }
            if (command_index<commands.size() && frame>=next_command) {
                const auto& command=commands[command_index++]; next_command=frame+8;
                if (command.name=="click") {
                    if (!ui.target(command.value,pressed,pressed_x,pressed_y)) throw std::runtime_error("Unavailable UI target: "+command.value);
                    mouse(*pressed,pressed_x,pressed_y,true); release_at=frame+3; ++clicks;
                } else if (command.name=="advance") {
                    const auto seconds=number<std::int64_t>(command.value);
                    if (seconds<0 || seconds>budget::max_offline_seconds) throw std::runtime_error("Script advance outside 0..7 days");
                    clock.advance(seconds*1000);
                } else if (command.name=="wall") {
                    const auto epoch=number<std::int64_t>(command.value);
                    if (epoch<0 || epoch>4'102'444'800LL) throw std::runtime_error("Script epoch outside 1970-2100");
                    clock.set_wall(epoch);
                }
                else if (command.name=="wait") next_command=frame+number<std::uint32_t>(command.value);
                else if(command.name=="activity") {
                    if(!runtime.post(activity(command.value))) throw std::runtime_error("Activity event queue full");
                }
                else throw std::runtime_error("Unknown script command");
            }
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE)) running=false;
                if (event.type==SDL_KEYDOWN && event.key.repeat==0) {
                    if (event.key.keysym.sym==SDLK_h) runtime.post({EventType::home});
                    else if (event.key.keysym.sym==SDLK_ESCAPE) runtime.post({EventType::back});
                    else if (event.key.keysym.sym==SDLK_SPACE) runtime.post({EventType::wake});
                }
                watch.event(event); debug.event(event);
                if (event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION) ui.input();
            }
            const auto now=SDL_GetTicks(); const auto delta=o.headless ? 16U : std::max(1U,now-previous); previous=now;
            lv_tick_inc(delta);
            if (o.headless) clock.advance(16);
            runtime.tick(); const auto flushes=watch.flush_count(); ui.update(lv_tick_get()); lv_timer_handler();
            if (!runtime.display_awake()) asleep_flushes+=watch.flush_count()-flushes;
            watch.present(); debug.present(); ++frame;
            if (!o.headless) {
                const auto wait=std::clamp<std::int64_t>(std::min(runtime.next_delay_ms(),static_cast<std::int64_t>(ui.next_delay(lv_tick_get()))),1,1000);
                SDL_WaitEventTimeout(nullptr,static_cast<int>(wait));
            }
        }
        if (command_index<commands.size()) throw std::runtime_error("Frame limit reached before script completed");
        if (auto result=runtime.flush(true); !result) std::cerr << "{\"event\":\"storage_degraded\",\"state\":\"checkpoint retained\"}\n";
        if (!o.backup.empty()) { status=runtime.backup(o.backup); if (!status) throw std::runtime_error("Backup: "+std::string{status.message.view()}); }
        if (!o.screenshot.empty()) { status=watch.screenshot(o.screenshot); if (!status) throw std::runtime_error("Screenshot: "+std::string{status.message.view()}); }
        if (!o.debug_screenshot.empty()) { status=debug.screenshot(o.debug_screenshot); if (!status) throw std::runtime_error("Debug screenshot: "+std::string{status.message.view()}); }
        report(o,runtime,watch.rendered_frames(),clicks,asleep_flushes);
        std::cout << "{\"event\":\"shutdown\",\"frames\":" << frame << ",\"rendered_frames\":" << watch.rendered_frames() << "}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "DaemonOS: " << error.what() << '\n'; return 1; }
}
