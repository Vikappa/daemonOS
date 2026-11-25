#include "mocks/platform.hpp"
#include "daemon/platform/desktop.hpp"
#include <sqlite3.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

using namespace daemonos;
namespace {
int checks=0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string{__FILE__}+":"+std::to_string(__LINE__)+": " #condition); } while(false)
bool near(double a,double b) { return std::abs(a-b)<1e-8; }
struct Temp {
    std::filesystem::path path;
    Temp() { char pattern[]="/tmp/daemonos-tests-XXXXXX"; const char* result=mkdtemp(pattern); if (!result) throw std::runtime_error("mkdtemp"); path=result; }
    ~Temp() { std::error_code error; std::filesystem::remove_all(path,error); }
};
void raw_sql(const std::filesystem::path& path,const char* sql) {
    sqlite3* db=nullptr;
    CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    const int result=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);
    sqlite3_close(db); CHECK(result==SQLITE_OK);
}
CreatureState initial() { test::Clock clock; test::Random random; return CreatureEngine::create(clock,random); }
void event_queue() {
    EventBus bus; int count=0;
    auto subscription=bus.subscribe([](void* p,const Event&) { ++*static_cast<int*>(p); },&count);
    CHECK(subscription);
    for (std::size_t i=0;i<budget::events;++i) CHECK(bus.post({EventType::feed}));
    CHECK(!bus.post({})); CHECK(bus.dropped()==1); CHECK(bus.dispatch(10)==10); CHECK(count==10);
    subscription.reset(); CHECK(bus.dispatch()==54); CHECK(count==10);
    std::array<EventBus::Subscription,budget::subscribers> slots;
    for (auto& slot:slots) { slot=bus.subscribe([](void*,const Event&) {},nullptr); CHECK(slot); }
    CHECK(!bus.subscribe([](void*,const Event&) {},nullptr));
}
void event_reentrancy() {
    struct Context { EventBus* bus; int count{}; };
    EventBus bus; Context context{&bus};
    auto subscription=bus.subscribe([](void* pointer,const Event&) {
        auto& c=*static_cast<Context*>(pointer); ++c.count;
        c.bus->post({}); CHECK(c.bus->dispatch()==0);
    },&context);
    bus.post({}); CHECK(bus.dispatch(7)==7); CHECK(context.count==7); CHECK(bus.pending()==1);
}
void registry_and_navigation() {
    ServiceRegistry services; int value=42;
    CHECK(services.get<int>()==nullptr); CHECK(services.add(value)); CHECK(!services.add(value)); CHECK(*services.get<int>()==42);
    struct App final : Application {
        AppId key; int starts{},resumes{},pauses{},stops{};
        explicit App(AppId key_value):key(key_value) {}
        AppId id() const override { return key; } std::string_view title() const override { return "test"; }
        void start() override { ++starts; } void resume() override { ++resumes; }
        void pause() override { ++pauses; } void stop() override { ++stops; }
    } launcher(AppId::launcher),creature(AppId::creature);
    ApplicationRegistry registry; CHECK(registry.add(launcher)); CHECK(!registry.add(launcher)); CHECK(registry.add(creature));
    { Navigation nav(registry); CHECK(nav.push(AppId::launcher)); CHECK(!nav.back()); CHECK(!nav.push(AppId::settings));
      CHECK(nav.push(AppId::creature)); CHECK(launcher.pauses==1); CHECK(!nav.push(AppId::launcher));
      CHECK(creature.starts==1); CHECK(nav.depth()==2); nav.home(); CHECK(nav.depth()==1);
      CHECK(creature.stops==1); CHECK(launcher.resumes==2); }
    CHECK(launcher.stops==1);
}
void deterministic_needs() {
    auto one=initial(),many=one;
    CreatureEngine::advance(one,3600'000);
    for (int i=0;i<3600;++i) CreatureEngine::advance(many,1000);
    CHECK(near(one.needs.hunger,many.needs.hunger)); CHECK(near(one.needs.health,many.needs.health));
    CHECK(one.age_seconds==3600); CHECK(one.revision==many.revision);
    CHECK(one.needs.hunger>25); CHECK(one.needs.energy<80);
    CHECK(CreatureEngine::interact(one,EventType::feed)); CHECK(one.needs.hunger<many.needs.hunger);
    CHECK(CreatureEngine::interact(one,EventType::rest)); const auto energy=one.needs.energy;
    CreatureEngine::advance(one,600'000); CHECK(one.needs.energy>energy);
    CreatureEngine::advance(one,1000LL*365*86400); CHECK(valid(one));
    CHECK(one.age_seconds==3600+600+budget::max_offline_seconds);
    auto remainder=initial(); CreatureEngine::advance(remainder,59'999); CHECK(remainder.age_seconds==0);
    CreatureEngine::advance(remainder,1); CHECK(remainder.age_seconds==60); CHECK(remainder.pending_ms==0);
    auto exhausted=initial(); exhausted.needs.energy=9; CreatureEngine::advance(exhausted,60'000); CHECK(exhausted.sleeping);
}
void time_discontinuities() {
    CHECK(CreatureEngine::offline_elapsed(100,200)==0);
    CHECK(CreatureEngine::offline_elapsed(200,100)==100);
    CHECK(CreatureEngine::offline_elapsed(9'999'999,0)==budget::max_offline_seconds);
    CHECK(CreatureEngine::offline_elapsed(100,-1)==0);
    desktop::VirtualClock clock(1'700'000'000); clock.speed(0);
    const auto mono=clock.monotonic_ms(); clock.set_wall(100); CHECK(clock.monotonic_ms()==mono); CHECK(clock.wall_seconds()==100);
    clock.advance(3600'000); CHECK(clock.wall_seconds()==3700); CHECK(clock.monotonic_ms()==mono+3600'000);
}
void power_and_scheduler() {
    Environment e; PowerManager power;
    CHECK(power.update(e,0,false)==PowerState::interactive);
    CHECK(power.update(e,60'000,false)==PowerState::deep_sleep);
    e.connected=true; CHECK(power.update(e,60'000,false)==PowerState::ble_standby);
    CHECK(power.update(e,0,true)==PowerState::thinking);
    e.battery=3; CHECK(power.update(e,0,true)==PowerState::deep_sleep);
    e.charging=true; CHECK(power.update(e,0,true)==PowerState::charging);
    Scheduler scheduler; int calls=0;
    auto callback=+[](void* pointer) { ++*static_cast<int*>(pointer); };
    CHECK(scheduler.schedule(1,100,callback,&calls)); CHECK(!scheduler.schedule(1,100,callback,&calls));
    CHECK(scheduler.run(99)==0); CHECK(scheduler.run(100)==1); CHECK(scheduler.run(100)==0); CHECK(calls==1);
    CHECK(scheduler.schedule(2,200,callback,&calls)); scheduler.cancel(2); CHECK(scheduler.run(300)==0);
    struct Reschedule { Scheduler* scheduler; int* calls; } replacement{&scheduler,&calls};
    CHECK(scheduler.schedule(1,0,+[](void* pointer) {
        auto& data=*static_cast<Reschedule*>(pointer);
        data.scheduler->cancel(2);
        CHECK(data.scheduler->schedule(2,0,+[](void* counter) { ++*static_cast<int*>(counter); },data.calls));
    },&replacement));
    CHECK(scheduler.schedule(2,0,callback,&calls));
    CHECK(scheduler.run(100)==1); CHECK(calls==1);
    CHECK(scheduler.run(100)==1); CHECK(calls==2);
}
constexpr std::string_view good=R"({"speech":"hello","emotion":"happy","intensity":0.4,"animation":"idle","actions":["speak","animate"]})";
void response_validation() {
    ResponseParser parser; CognitiveResponse response;
    for (const auto c:good) CHECK(parser.append({&c,1}));
    CHECK(parser.finish(response)); CHECK(response.speech.view()=="hello"); CHECK(response.action_count==2);
    CapabilityExecutor executor; CHECK(!executor.execute(response,1)); CHECK(executor.speech().view().empty());
    CHECK(executor.execute(response,3)); CHECK(executor.speech().view()=="hello");
    CHECK(executor.face_cue() == CreatureFace::happy);
    response.action_count = 1; // speak alone cannot repeat the previous animate action
    CHECK(executor.execute(response,3)); CHECK(!executor.face_cue());
    response.actions[0] = Capability::animate;
    response.emotion.assign("curious");
    CHECK(executor.execute(response,3)); CHECK(executor.face_cue() == CreatureFace::curious);
    response.animation.assign("sleep");
    CHECK(executor.execute(response,3)); CHECK(executor.face_cue() == CreatureFace::resting);
    for (const auto invalid : {
        R"({"speech":"x","emotion":"happy","intensity":0.4,"animation":"idle","actions":["filesystem"]})",
        R"({"speech":"x","emotion":"happy","intensity":5,"animation":"idle","actions":[]})",
        R"({"speech":"x","emotion":"happy","intensity":0.4,"animation":"idle","actions":["speak","speak"]})",
        R"({"speech":"x","speech":"z","emotion":"happy","intensity":0.4,"animation":"idle","actions":[]})",
        R"({"speech":"x","emotion":"happy","intensity":0.4,"animation":"idle","actions":[],"code":"run"})",
        R"({"speech":"x","emotion":"happy","intensity":NaN,"animation":"idle","actions":[]})",
        R"({"speech":"x","emotion":"happy","intensity":01,"animation":"idle","actions":[]})",
        R"({"speech":"x","emotion":"happy","intensity":.5,"animation":"idle","actions":[]})",
        R"({"speech":"x","emotion":"happy","intensity":1.,"animation":"idle","actions":[]})",
        R"({"speech":"x","emotion":"happy","intensity":0.4,"animation":"idle","actions":[],})",
        "{}", "[]", "null", "{", ""}) {
        parser.reset(); CHECK(parser.append(invalid)); CHECK(!parser.finish(response));
    }
    parser.reset(); const std::string oversized(budget::response_bytes+1,'x'); CHECK(!parser.append(oversized)); CHECK(!parser.finish(response));
    std::uint32_t random=12345;
    for (int i=0;i<500;++i) {
        std::array<char,64> noise{};
        for (auto& c:noise) { random=random*1664525U+1013904223U; c=static_cast<char>(random>>24U); }
        parser.reset(); parser.append({noise.data(),noise.size()}); CHECK(!parser.finish(response));
    }
}
void cognition_limits() {
    Environment e; e.connected=true; CognitiveScheduler scheduler;
    CHECK(scheduler.begin(0,e,true)); CHECK(!scheduler.begin(1,e,true)); CHECK(!scheduler.timed_out(9999)); CHECK(scheduler.timed_out(10000));
    scheduler.complete(10000,false); CHECK(!scheduler.begin(120000,e,true)); CHECK(scheduler.begin(130000,e,true));
    scheduler.complete(130000,true); CHECK(!scheduler.begin(189999,e,true)); CHECK(scheduler.begin(190000,e,true));
    scheduler.complete(190000,true); e.connected=false; CHECK(!scheduler.begin(999999,e,true));
    e.connected=true; e.battery=1; CHECK(!scheduler.begin(999999,e,true)); e.charging=true; CHECK(scheduler.begin(999999,e,true));
    MemoryPage page; ContextBuilder context; CHECK(context.build(initial(),page)); CHECK(context.context().size()<budget::context_bytes);
    MockProvider mock; CHECK(mock.start(context.context(),256)); CHECK(!mock.start(context.context(),256));
    std::array<char,32> chunk{}; std::size_t size{}; bool done=false; ResponseParser parser;
    while (!done) { CHECK(mock.poll(chunk,size,done)); CHECK(parser.append({chunk.data(),size})); }
    CognitiveResponse response; CHECK(parser.finish(response)); mock.cancel(); CHECK(!mock.poll(chunk,size,done));
}
void sqlite_roundtrip_and_backup() {
    Temp temp; auto state=initial(); Settings settings; settings.brightness=40;
    {
        desktop::SqliteStore store(temp.path/"live.sqlite"); CHECK(store.integrity());
        CreatureState loaded; Settings loaded_settings; CHECK(store.load(loaded,loaded_settings).code==Error::unavailable);
        CHECK(store.save(state,settings));
        for (int i=0;i<20;++i) {
            Memory memory; memory.timestamp=state.last_wall+i; memory.content.assign(i==3 ? "A curious keeper's spark" : "Walked together");
            memory.tags.assign(i==3 ? "curious" : "walk"); memory.entities.assign("keeper"); memory.importance=i==3 ? 1 : 0.5;
            CHECK(store.save(state,settings,&memory));
        }
        CHECK(store.load(loaded,loaded_settings)); CHECK(loaded.id==state.id); CHECK(loaded_settings.brightness==40);
        MemoryPage page; CHECK(store.memories({},0,page)); CHECK(page.size==8); CHECK(page.items[0].content.view()=="A curious keeper's spark");
        CHECK(store.memories("keeper's",0,page)); CHECK(page.size==1);
        CHECK(store.memories("' OR 1=1 --",0,page)); CHECK(page.size==0);
        CHECK(store.memories("keeper",8,page)); CHECK(page.size==8);
        CHECK(!store.memories({},10001,page)); CHECK(!store.memories(std::string(97,'a'),0,page));
        CHECK(store.backup((temp.path/"export.sqlite").string())); CHECK(!store.backup((temp.path/"export.sqlite").string()));
        store.available(false); CHECK(!store.save(state,settings)); CHECK(!store.memories({},0,page)); CHECK(!store.backup((temp.path/"fault.sqlite").string()));
    }
    desktop::SqliteStore restored(temp.path/"export.sqlite"); CreatureState loaded; Settings config;
    CHECK(restored.load(loaded,config)); CHECK(loaded.id==state.id); CHECK(config.brightness==40);
    MemoryPage page; CHECK(restored.memories({},0,page)); CHECK(page.size==8);
}
void sqlite_transaction_rollback() {
    Temp temp; const auto path=temp.path/"rollback.sqlite";
    desktop::SqliteStore store(path); auto state=initial(); Settings settings;
    CHECK(store.save(state,settings));
    raw_sql(path,"CREATE TRIGGER deny_memory BEFORE INSERT ON memories BEGIN SELECT RAISE(ABORT,'injected failure'); END;");
    auto changed=state; changed.needs.hunger=99; ++changed.revision;
    Memory memory; memory.content.assign("This transaction must roll back");
    CHECK(!store.save(changed,settings,&memory));
    CreatureState loaded; CHECK(store.load(loaded,settings)); CHECK(loaded.needs.hunger==state.needs.hunger); CHECK(loaded.revision==state.revision);
}
void sqlite_migrations_and_corruption() {
    Temp temp; auto state=initial(); Settings settings;
    const auto path=temp.path/"version.sqlite";
    { desktop::SqliteStore store(path); CHECK(store.save(state,settings)); }
    raw_sql(path,"DROP INDEX memories_actor; DROP INDEX entries_actor; DROP INDEX tools_actor; ALTER TABLE memories DROP COLUMN actor_id; ALTER TABLE assistant_entries DROP COLUMN actor_id; ALTER TABLE tool_jobs DROP COLUMN actor_id; ALTER TABLE tool_jobs DROP COLUMN approved_by; ALTER TABLE tool_jobs DROP COLUMN approval_revision; ALTER TABLE agent_tasks DROP COLUMN actor_id; DROP TABLE voice_encounters; DROP TABLE voice_templates; DROP TABLE voice_profiles; DELETE FROM settings WHERE key='voice_guest_tracking'; DELETE FROM schema_migrations WHERE version=5; DROP INDEX memories_recent; DROP INDEX memories_importance; DROP TABLE assistant_entries; DROP TABLE tool_jobs; DROP TABLE agent_tasks; DELETE FROM schema_migrations WHERE version>=2; PRAGMA user_version=1;");
    { desktop::SqliteStore store(path); CHECK(store.load(state,settings)); CHECK(store.integrity()); }
    raw_sql(path,"PRAGMA user_version=999;");
    { desktop::SqliteStore store(path); CHECK(store.load(state,settings).code==Error::incompatible); CHECK(!store.save(state,settings)); }
    { std::ofstream file(temp.path/"corrupt.sqlite"); file<<"this is not SQLite"; }
    desktop::SqliteStore corrupt(temp.path/"corrupt.sqlite"); CHECK(corrupt.load(state,settings).code==Error::corrupt);
    const auto incomplete=temp.path/"incomplete.sqlite";
    { desktop::SqliteStore store(incomplete); CHECK(store.save(state,settings)); }
    raw_sql(incomplete,"DELETE FROM needs;");
    desktop::SqliteStore incomplete_store(incomplete); CHECK(incomplete_store.load(state,settings).code==Error::corrupt);
}
void checkpoint_crc_and_recovery() {
    Temp temp; const auto path=temp.path/"flash"/"identity.checkpoint";
    desktop::FileCheckpoint checkpoint(path); auto state=initial(); CreatureState loaded;
    CHECK(checkpoint.load(loaded).code==Error::unavailable); CHECK(checkpoint.save(state)); CHECK(checkpoint.load(loaded)); CHECK(loaded.id==state.id);
    { std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out); file.seekp(30); file.put('!'); }
    CHECK(checkpoint.load(loaded).code==Error::corrupt);
    CHECK(checkpoint.save(state));
    { std::ofstream file(temp.path/"bad.sqlite"); file<<"damaged card"; }
    desktop::SqliteStore broken(temp.path/"bad.sqlite"); test::Clock clock; test::Random random; MockProvider provider;
    Runtime runtime(clock,random,broken,checkpoint,provider); CHECK(runtime.boot()); CHECK(runtime.creature().id==state.id);
    clock.advance(3600'000); runtime.tick(); CHECK(runtime.creature().age_seconds==3600);
    CHECK(runtime.post({EventType::feed})); runtime.tick(); CHECK(runtime.pending_memories()==1);
    CHECK(!runtime.flush(true)); CHECK(checkpoint.load(loaded)); CHECK(loaded.id==state.id);
}
void runtime_offline_restart_faults() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    Text<40> identity;
    {
        Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); identity=runtime.creature().id;
        CHECK(runtime.services().get<hal::Clock>()==&clock);
        runtime.post({EventType::open_app,3}); runtime.post({EventType::feed}); runtime.post({EventType::pet}); runtime.tick();
        CHECK(runtime.app()==AppId::creature); CHECK(runtime.creature().needs.hunger==3); CHECK(runtime.pending_memories()==2);
        const int writes=store.writes;
        clock.advance(10'000); runtime.tick(); CHECK(store.writes==writes);
        runtime.post({EventType::storage,0}); runtime.tick(); clock.advance(3600'000); runtime.tick();
        CHECK(!runtime.flush()); CHECK(runtime.creature().needs.hunger>3);
        for (int i=0;i<30;++i) runtime.post({EventType::pet});
        runtime.tick();
        CHECK(runtime.pending_memories()==16); CHECK(runtime.lost_memories()>0);
        runtime.post({EventType::storage,1}); runtime.tick(); CHECK(runtime.pending_memories()==0); CHECK(store.history.size()==16);
        runtime.post({EventType::brightness,30}); runtime.tick(); CHECK(runtime.flush(true));
    }
    clock.wall+=7200;
    { Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); CHECK(runtime.creature().id==identity);
      CHECK(runtime.creature().age_seconds==10800); CHECK(runtime.settings().brightness==30);
      const auto age=runtime.creature().age_seconds; clock.wall-=999999; clock.advance(60'000); runtime.tick(); CHECK(runtime.creature().age_seconds==age+60);
      CHECK(runtime.flush()); }
}
void runtime_cognition_cancel_and_complete() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    runtime.post({EventType::think}); runtime.tick(); CHECK(!runtime.thinking());
    runtime.post({EventType::connectivity,1}); runtime.post({EventType::think}); runtime.tick(); CHECK(runtime.thinking());
    CHECK(runtime.face() == CreatureFace::thinking);
    runtime.post({EventType::connectivity,0}); runtime.tick(); CHECK(!runtime.thinking());
    CHECK(runtime.face() == CreatureFace::inference_unreachable);
    clock.advance(120'000); runtime.post({EventType::connectivity,1}); runtime.post({EventType::think}); runtime.tick(); CHECK(runtime.thinking());
    for (int i=0;i<20;++i) { clock.advance(10); runtime.tick(); }
    CHECK(!runtime.thinking()); CHECK(runtime.message().starts_with("Sono qui, capo.")); CHECK(valid(runtime.creature()));
    CHECK(runtime.face() == CreatureFace::happy);
    clock.advance(2500); runtime.tick(); CHECK(runtime.face() == CreatureFace::calm);
}
void runtime_faces() {
    test::Clock clock; test::Random random; test::Store store; test::Checkpoint checkpoint; MockProvider provider;
    store.state = initial(); store.state.needs.hunger = 82; store.exists = true;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    const auto send = [&](Event event) { CHECK(runtime.post(event)); runtime.tick(); };
    CHECK(runtime.face() == CreatureFace::hungry);
    send({EventType::pet}); CHECK(runtime.face() == CreatureFace::hungry);
    send({EventType::feed}); CHECK(runtime.face() == CreatureFace::happy);
    send({EventType::charging,1});
    clock.advance(2499); runtime.tick(); CHECK(runtime.face() == CreatureFace::happy);
    clock.advance(1); runtime.tick(); CHECK(runtime.face() == CreatureFace::charging);
    send({EventType::move,100}); CHECK(runtime.face() == CreatureFace::curious);
    send({EventType::rest}); CHECK(runtime.face() == CreatureFace::resting);
    send({EventType::rest}); CHECK(runtime.face() == CreatureFace::charging);
}
void refuse_identity_replacement() {
    Temp temp; { std::ofstream file(temp.path/"bad.sqlite"); file<<"corrupt"; }
    desktop::SqliteStore store(temp.path/"bad.sqlite"); test::Checkpoint checkpoint; test::Clock clock; test::Random random; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(!runtime.boot()); CHECK(!checkpoint.exists);
}
void checkpoint_transfer_to_empty_media() {
    Temp temp; auto state=initial(); CreatureEngine::advance(state,3600'000);
    desktop::FileCheckpoint checkpoint(temp.path/"flash/identity.checkpoint"); CHECK(checkpoint.save(state));
    desktop::SqliteStore store(temp.path/"new-sd/creature.sqlite"); test::Clock clock; test::Random random; MockProvider provider;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot());
    CHECK(runtime.creature().id==state.id); CHECK(near(runtime.creature().needs.hunger,state.needs.hunger));
    CreatureState restored; Settings settings; CHECK(store.load(restored,settings)); CHECK(restored.id==state.id);
}
}
int main() {
    struct Case { const char* name; void (*run)(); };
    const Case cases[]={
        {"bounded events",event_queue},{"event reentrancy",event_reentrancy},{"registry and lifecycle",registry_and_navigation},
        {"deterministic needs",deterministic_needs},{"clock discontinuities",time_discontinuities},
        {"power and scheduler",power_and_scheduler},{"untrusted response validation",response_validation},{"cognitive budgets",cognition_limits},
        {"SQLite persistence pagination export",sqlite_roundtrip_and_backup},{"transaction rollback",sqlite_transaction_rollback},
        {"migrations and corruption",sqlite_migrations_and_corruption},{"checkpoint recovery",checkpoint_crc_and_recovery},
        {"offline restart and storage faults",runtime_offline_restart_faults},{"mock streaming and cancellation",runtime_cognition_cancel_and_complete},
        {"creature face priorities and expiry",runtime_faces},
        {"identity preservation",refuse_identity_replacement},{"minimal checkpoint transfer",checkpoint_transfer_to_empty_media}};
    int failures=0;
    for (const auto& test:cases) {
        try { test.run(); std::cout<<"PASS "<<test.name<<'\n'; }
        catch (const std::exception& error) { ++failures; std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n'; }
    }
    std::cout<<std::size(cases)<<" cases, "<<checks<<" checks, "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
