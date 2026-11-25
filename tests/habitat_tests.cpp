#include "mocks/platform.hpp"
#include "daemon/platform/desktop.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sqlite3.h>
#ifdef DAEMON_PRESENTATION_TESTS
#include "daemon/presentation/watch_view.hpp"
#endif

using namespace daemonos;
namespace {
int checks=0;
#define CHECK(expression) do { ++checks; if (!(expression)) throw std::runtime_error(std::string{__FILE__}+":"+std::to_string(__LINE__)+": " #expression); } while(false)
void wake_lock_lifecycle() {
    PowerManager power; Environment environment;
    CHECK(!power.acquire(ServiceId::wifi,mask(Resource::audio),0,100));
    CHECK(!power.acquire(ServiceId::wifi,mask(Resource::wifi),0,30'001));
    auto first=power.acquire(ServiceId::audio,Resource::cpu|Resource::audio,0,10);
    CHECK(first); CHECK(power.active_locks()==1);
    CHECK(power.update(environment,100'000,false,1)==PowerState::listening);
    CHECK(power.display_awake());
    auto moved=std::move(first); CHECK(!first); CHECK(moved);
    CHECK(power.update(environment,100'000,false,11)==PowerState::deep_sleep);
    CHECK(!moved); CHECK(power.expired_locks()==1); CHECK(power.last_expired_owner()=="audio");
    auto replacement=power.acquire(ServiceId::wifi,mask(Resource::wifi),12,10);
    moved.reset(); CHECK(replacement); CHECK(power.active_locks()==1);
    replacement.reset(); CHECK(power.active_locks()==0);
    auto speech=power.acquire(ServiceId::audio,mask(Resource::audio),20,100,true,PowerState::speaking);
    CHECK(power.update(environment,100'000,false,21)==PowerState::speaking);
}
void service_coordination() {
    PowerManager power; Environment environment; int transitions=0;
    CHECK(power.bind(ServiceId::wifi,+[](void* context,bool)->Status { ++*static_cast<int*>(context); return {}; },&transitions));
    CHECK(transitions==1);
    auto cognition=power.acquire(ServiceId::cognition,mask(Resource::cpu),0,100);
    power.update(environment,0,true,1);
    CHECK(!power.services()[static_cast<std::size_t>(ServiceId::storage)].enabled);
    CHECK(!power.services()[static_cast<std::size_t>(ServiceId::audio)].enabled);
    CHECK(power.services()[static_cast<std::size_t>(ServiceId::cognition)].enabled);
    auto wifi=power.acquire(ServiceId::wifi,mask(Resource::wifi),1,100);
    power.update(environment,0,true,2); CHECK(transitions==2);
    power.allow(Resource::wifi,false); power.update(environment,0,true,3); CHECK(transitions==3);
    CHECK(!power.services()[static_cast<std::size_t>(ServiceId::wifi)].enabled);
    power.allow(Resource::display,false); power.update(environment,0,true,4); CHECK(!power.display_awake());
}
void resource_enforcement() {
    struct Allocator final : MemoryAllocator {
        int live{};
        void* allocate(std::size_t bytes,MemoryPool) override { ++live; return std::malloc(bytes); }
        void release(void* data) override { --live; std::free(data); }
    } allocator;
    MemoryBudget limits; limits.pools={64,128}; limits.categories.fill(128);
    ResourceManager resources(limits,&allocator);
    auto critical=resources.reserve(MemoryCategory::runtime,MemoryPool::internal,48);
    CHECK(critical); CHECK(!resources.reserve(MemoryCategory::audio,MemoryPool::internal,17));
    auto pixels=resources.allocate(MemoryCategory::graphics,MemoryPool::external,96);
    CHECK(pixels); CHECK(pixels.bytes().size()==96); CHECK(allocator.live==1);
    CHECK(!resources.allocate(MemoryCategory::graphics,MemoryPool::external,33));
    auto moved=std::move(pixels); CHECK(!pixels); CHECK(moved); CHECK(resources.peak(MemoryPool::external)==96);
    resources.pressure(true); CHECK(!resources.reserve(MemoryCategory::assets,MemoryPool::external,1));
    auto essential=resources.reserve(MemoryCategory::runtime,MemoryPool::internal,16); CHECK(essential);
    moved.reset(); CHECK(allocator.live==0); CHECK(resources.used(MemoryPool::external)==0);
    CHECK(resources.rejected()==3);
    CHECK(!resources.reserve(MemoryCategory::count,MemoryPool::external,1));
}
void suspension_and_continuity() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    const auto identity=runtime.creature().id;
    for (int i=0;i<1000;++i) { clock.advance(10); runtime.tick(); }
    CHECK(runtime.simulation_updates()==0);
    runtime.post({EventType::suspend}); runtime.tick(); CHECK(runtime.applications_suspended());
    CHECK(!runtime.display_awake());
    const auto updates=runtime.simulation_updates(),writes=runtime.database_operations();
    clock.advance(3*86400'000LL);
    for (int i=0;i<10000;++i) runtime.tick();
    CHECK(runtime.simulation_updates()==updates); CHECK(runtime.database_operations()==writes);
    CHECK(runtime.next_delay_ms()==std::numeric_limits<std::int64_t>::max());
    runtime.post({EventType::wake}); runtime.tick();
    CHECK(runtime.display_awake()); CHECK(!runtime.applications_suspended()); CHECK(runtime.last_sleep_ms()==3*86400'000LL);
    CHECK(runtime.creature().id==identity); CHECK(runtime.creature().age_seconds==3*86400);
    const auto health=runtime.creature().needs.health;
    runtime.post({EventType::battery,0}); runtime.tick(); CHECK(!runtime.display_awake());
    clock.advance(10*86400'000LL); runtime.post({EventType::charging,1}); runtime.tick();
    CHECK(runtime.display_awake()); CHECK(runtime.creature().needs.health>=health);
    CHECK(valid(runtime.creature()));
    CHECK(runtime.creature().age_seconds==10*86400); // Seven-day catch-up cap plus earlier three days.
}
void pressure_cancels_cognition() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    runtime.post({EventType::connectivity,1}); runtime.post({EventType::think}); runtime.tick();
    CHECK(runtime.thinking()); CHECK(runtime.resources().used(MemoryCategory::cognitive)>0);
    CHECK(runtime.power_manager().active_locks()==1);
    runtime.post({EventType::memory_pressure,1}); runtime.tick();
    CHECK(!runtime.thinking()); CHECK(runtime.resources().used(MemoryCategory::cognitive)==0);
    CHECK(runtime.power_manager().active_locks()==0); CHECK(runtime.cognitive_lifecycle()=="cancelled");
    CHECK(!runtime.configure_agent(std::string(1024,'x'))); CHECK(runtime.configure_agent("Be gentle and curious."));
    CHECK(runtime.name("Nyx")); CHECK(runtime.creature().name.view()=="Nyx"); CHECK(!runtime.name("bad\nname"));
}
constexpr std::string_view silent=R"({"v":2,"speech":null,"emotion":"calm","intensity":0.2,"facial_expression":"neutral","animation":"idle","voice":null,"actions":["animate"],"memory_candidates":[],"personality_adjustments":[],"intentions":[]})";
void awakening_contract() {
    ResponseParser parser; CognitiveResponse response;
    CHECK(parser.append(silent)); CHECK(parser.finish(response)); CHECK(response.schema_version==2); CHECK(response.speech.view().empty());
    CapabilityExecutor executor; CHECK(executor.execute(response,7)); CHECK(executor.speech().view().empty());
    const std::string_view proposals=R"({"v":2,"speech":null,"emotion":"curious","intensity":0.4,"facial_expression":"neutral","animation":"idle","voice":null,"actions":[],"memory_candidates":[{"content":"We walked","importance":0.6,"valence":0.5}],"personality_adjustments":[{"trait":"curiosity","delta":0.01}],"intentions":[{"description":"Explore later","after_seconds":3600}]})";
    parser.reset(); parser.append(proposals); CHECK(parser.finish(response)); CHECK(response.memory_count==1);
    CHECK(!executor.execute(response,7)); CHECK(ActionValidator::validate(response,55));
    for (const auto& malformed: {
        std::string{silent}+" trailing",
        std::string{silent}.replace(silent.find("null"),4,"\"bad\\u0000\""),
        std::string{silent}.replace(silent.find("\"animate\""),9,"\"filesystem\""),
        std::string{silent}.replace(silent.find("\"v\":2"),5,"\"v\":2,\"v\":2"),
        std::string{silent}.replace(silent.find("null"),4,std::string{"\""}+char(0xff)+"\"")}) {
        parser.reset(); CHECK(parser.append(malformed)); CHECK(!parser.finish(response));
    }
    test::Clock clock; test::Random random; auto state=CreatureEngine::create(clock,random);
    ContextBuilder context; MemoryPage page; AgentConfiguration agent; agent.instructions.assign("LOCAL CUSTOM AGENT");
    CHECK(context.build(state,page,agent)); CHECK(context.context().find("LOCAL CUSTOM AGENT")!=std::string_view::npos);
    CHECK(context.context().find("Body:")!=std::string_view::npos); CHECK(context.context().find("Trigger=")!=std::string_view::npos);
}
void scheduled_sleep_wakeup() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    runtime.post({EventType::suspend}); runtime.tick(); int calls=0;
    CHECK(runtime.scheduler().schedule(42,1000,+[](void* value) { ++*static_cast<int*>(value); },&calls));
    CHECK(runtime.next_delay_ms()==1000); clock.advance(1000); runtime.tick(); CHECK(calls==1);
    CHECK(!runtime.display_awake()); CHECK(runtime.next_delay_ms()==std::numeric_limits<std::int64_t>::max());
}
void database_work_budget() {
    char path[]="/tmp/daemon-habitat-query-XXXXXX";
    const char* directory=mkdtemp(path); CHECK(directory);
    const auto file=std::filesystem::path(directory)/"history.sqlite";
    {
        desktop::SqliteStore store(file); test::Clock clock; test::Random random;
        const auto state=CreatureEngine::create(clock,random); CHECK(store.save(state,{}));
        sqlite3* db=nullptr; CHECK(sqlite3_open(file.c_str(),&db)==SQLITE_OK);
        const int seeded=sqlite3_exec(db,"WITH RECURSIVE series(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM series WHERE n<5000) "
            "INSERT INTO memories(creature_id,timestamp,category,content,provenance,importance,valence) SELECT creature.id,n,'test','bounded query','test',0.5,0 FROM series CROSS JOIN creature",nullptr,nullptr,nullptr);
        sqlite3_close(db); CHECK(seeded==SQLITE_OK);
        MemoryPage page; CHECK(store.memories({},0,page).code==Error::busy); CHECK(page.size==0);
        CHECK(store.trim()); CHECK(store.integrity());
    }
    std::filesystem::remove_all(directory);
}
#ifdef DAEMON_PRESENTATION_TESTS
void shared_presentation_quiescence() {
    lv_init();
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    auto* display=lv_display_create(410,502); CHECK(display);
    std::array<std::uint32_t,410*40> draw{}; int flushes=0;
    lv_display_set_color_format(display,LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display,draw.data(),nullptr,sizeof(draw),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display,&flushes);
    lv_display_set_flush_cb(display,+[](lv_display_t* d,const lv_area_t*,std::uint8_t*) {
        ++*static_cast<int*>(lv_display_get_user_data(d)); lv_display_flush_ready(d);
    });
    {
        presentation::WatchController controller(runtime,clock);
        presentation::WatchView view(controller,display);
        view.update(0); CHECK(flushes>0); int x=0,y=0; CHECK(view.target("open_creature",x,y));
        controller.activate("open_creature",{EventType::open_app,3}); runtime.tick(); view.update(10); CHECK(view.target("feed",x,y));
        runtime.post({EventType::suspend}); runtime.tick(); view.update(20); const int stopped=flushes;
        for (int i=0;i<1000;++i) { clock.advance(1000); runtime.tick(); view.update(static_cast<std::uint32_t>(i+30)); lv_timer_handler(); }
        CHECK(flushes==stopped);
        runtime.post({EventType::wake}); runtime.tick(); view.update(2000); CHECK(flushes>stopped);
    }
    lv_display_delete(display); lv_deinit();
}
#endif
}
int main() {
    struct Case { const char* name; void(*run)(); };
    const Case cases[]={{"wake lock lifecycle",wake_lock_lifecycle},{"service coordination",service_coordination},
        {"resource enforcement",resource_enforcement},{"suspension and continuity",suspension_and_continuity},
        {"pressure cancellation",pressure_cancels_cognition},{"Awakening contract",awakening_contract},{"scheduled sleeping work",scheduled_sleep_wakeup},{"database work budget",database_work_budget},
#ifdef DAEMON_PRESENTATION_TESTS
        {"shared presentation quiescence",shared_presentation_quiescence}
#endif
    };
    int failures=0;
    for (const auto& test:cases) try { test.run(); std::cout<<"PASS "<<test.name<<'\n'; }
    catch (const std::exception& e) { ++failures; std::cerr<<"FAIL "<<test.name<<": "<<e.what()<<'\n'; }
    std::cout<<std::size(cases)<<" Habitat cases, "<<checks<<" checks, "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
