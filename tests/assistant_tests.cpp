#include "mocks/platform.hpp"
#include "daemon/platform/desktop.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <unistd.h>

using namespace daemonos;
namespace {
int checks{};
#define CHECK(c) do { ++checks; if(!(c)) throw std::runtime_error(std::string{__FILE__}+":"+std::to_string(__LINE__)+": " #c); } while(false)
struct Temp {
    std::filesystem::path path;
    Temp() { char p[]="/tmp/daemon-tools-XXXXXX"; const auto* value=mkdtemp(p); if(!value) throw std::runtime_error("mkdtemp"); path=value; }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(path,ec); }
};
void sql(const std::filesystem::path& path,const char* statement) {
    sqlite3* db{}; CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    const auto result=sqlite3_exec(db,statement,nullptr,nullptr,nullptr); sqlite3_close(db); CHECK(result==SQLITE_OK);
}
ToolCall call(std::string_view id,ToolName name,std::int64_t now) {
    ToolCall c; CHECK(c.id.assign(id)); c.name=name; c.text.assign("Una cosa importante");
    if(name==ToolName::set_alarm || name==ToolName::set_reminder) c.at=now+60;
    if(phone_tool(name)) c.expires_at=now+120;
    if(name==ToolName::send_message) c.recipient.assign("contacts:alice-verified");
    return c;
}
struct Phone final : PhoneTools {
    bool online{true}, allow{true}, fail{};
    std::vector<ToolCall> sent;
    std::vector<ToolResult> results;
    bool supports(ToolName name) const override { return allow && phone_tool(name); }
    bool authenticated() const override { return online; }
    Status dispatch(const ToolCall& c) override { sent.push_back(c); return fail ? Status::fail(Error::io,"lost connection") : Status{}; }
    bool poll(ToolResult& result) override {
        if(results.empty()) return false;
        result=results.front(); return true;
    }
    void accept_result(std::string_view id) override { if(!results.empty() && results.front().id.view()==id) results.erase(results.begin()); }
    void acknowledge(std::string_view id,bool success=true) { ToolResult r; r.id.assign(id); r.success=success; r.detail.assign(success ? "device adapter completed the action" : "permission denied"); results.push_back(r); }
};
void parser_and_context() {
    constexpr auto response=R"({"speech":"Fatto!","emotion":"happy","intensity":0.5,"animation":"idle","actions":["speak"],"tool_calls":[{"id":"note-1","name":"save_note","execute_at":0,"expires_at":0,"arguments":{"text":"Compra il riso"}},{"id":"phone-1","name":"send_message","execute_at":1700000060,"expires_at":1700000120,"arguments":{"recipient":"contacts:alice-verified","text":"Arrivo"}}]})";
    ResponseParser parser; CognitiveResponse r;
    for(const char c:std::string_view{response}) CHECK(parser.append({&c,1}));
    CHECK(parser.finish(r)); CHECK(r.tool_count==2); CHECK(r.tool_calls[1].execute_at==1700000060);
    for(const auto invalid : {
        R"({"id":"x","name":"shell","execute_at":0,"expires_at":0,"arguments":{"text":"x"}})",
        R"({"id":"x","name":"save_note","execute_at":0,"expires_at":0,"arguments":{"text":"x","at":0}})",
        R"({"id":"x","name":"set_alarm","execute_at":0,"expires_at":0,"arguments":{"text":"x","at":4}})",
        R"({"id":"x","name":"save_note","execute_at":0.5,"expires_at":0,"arguments":{"text":"x"}})",
        R"({"id":"x","name":"save_note","execute_at":9223372036854775808,"expires_at":0,"arguments":{"text":"x"}})",
        R"({"id":"x","name":"send_message","execute_at":0,"expires_at":99,"arguments":{"text":"x","recipient":"Alice Smith"}})",
        R"({"id":"x","name":"save_note","execute_at":0,"expires_at":0,"arguments":{"text":"x","text":"y"}})"}) {
        const std::string json=std::string{R"({"speech":"x","emotion":"calm","intensity":0,"animation":"idle","actions":[],"tool_calls":[)"}+invalid+"]}";
        parser.reset(); CHECK(parser.append(json)); CHECK(!parser.finish(r));
    }
    ContextBuilder context; MemoryPage memories; ToolPage results; results.size=1;
    results.items[0].call.id.assign("phone-1"); results.items[0].state=ToolState::uncertain;
    results.items[0].result.assign("unknown delivery; not resent");
    test::Clock clock; test::Random random;
    CHECK(context.build(CreatureEngine::create(clock,random),memories,"Mandami un promemoria",{},results));
    CHECK(context.context().find("sarcastic")!=std::string_view::npos);
    CHECK(context.context().find("set_alarm=0")!=std::string_view::npos);
    CHECK(context.context().find("id=phone-1 state=5")!=std::string_view::npos);
}
void persistence_and_reminders() {
    Temp temp; test::Clock clock;
    const auto path=temp.path/"device.sqlite";
    {
        desktop::SqliteStore store(path); AssistantService service(clock,store);
        const auto note=call("note-1",ToolName::save_note,clock.wall);
        CHECK(service.submit({&note,1})); service.tick();
        AssistantPage page; CHECK(service.notes({},0,page)); CHECK(page.size==1); CHECK(page.items[0].content==note.text);
        CHECK(service.submit({&note,1})); service.tick(); CHECK(service.notes({},0,page)); CHECK(page.size==1);
        auto changed=note; changed.text.assign("different"); CHECK(!service.submit({&changed,1}));
        const auto reminder=call("reminder-1",ToolName::set_reminder,clock.wall);
        CHECK(service.submit({&reminder,1})); CHECK(!service.alerting()); // Crash after enqueue, before execution.
    }
    clock.advance(120'000);
    {
        desktop::SqliteStore store(path); AssistantService service(clock,store); service.tick();
        CHECK(service.alerting()); CHECK(service.reply().find("Una cosa importante")!=std::string_view::npos);
        const auto id=service.reminder().id;
        store.available(false); CHECK(!service.complete_reminder(id)); CHECK(service.alerting());
        store.available(true); CHECK(service.complete_reminder(id,30)); CHECK(!service.alerting());
        clock.wall+=30; service.tick(); CHECK(service.alerting()); CHECK(service.complete_reminder(id)); // Wall clock correction without monotonic advance.
    }
    desktop::SqliteStore store(path); AssistantService service(clock,store); service.tick(); CHECK(!service.alerting());
    CHECK(store.backup((temp.path/"backup.sqlite").string())); desktop::SqliteStore backup(temp.path/"backup.sqlite");
    AssistantPage page; CHECK(backup.entries(EntryKind::note,{},0,page)); CHECK(page.size==1);
}
void atomicity_and_policy() {
    Temp temp; test::Clock clock; const auto path=temp.path/"device.sqlite";
    desktop::SqliteStore store(path); AssistantService service(clock,store);
    std::array batch{call("note",ToolName::save_note,clock.wall),call("phone",ToolName::set_alarm,clock.wall)};
    CHECK(!service.submit(batch)); ToolJob job; CHECK(!store.tool_job("note",job));
    const auto note=batch[0];
    sql(path,"CREATE TRIGGER fail_job BEFORE UPDATE OF state ON tool_jobs WHEN NEW.state=3 BEGIN SELECT RAISE(ABORT,'fault'); END;");
    CHECK(service.submit({&note,1})); service.tick(); AssistantPage page;
    CHECK(service.notes({},0,page)); CHECK(page.size==0); CHECK(store.tool_job("note",job)); CHECK(job.state==ToolState::queued);
    sql(path,"DROP TRIGGER fail_job;"); clock.advance(1000); service.tick(); CHECK(service.notes({},0,page)); CHECK(page.size==1);
    store.available(false); auto other=call("other",ToolName::save_note,clock.wall); CHECK(!service.submit({&other,1}));
    CHECK(service.reply().find("Non ho salvato")!=std::string_view::npos); store.available(true); CHECK(!store.tool_job("other",job));
    std::array invalid{call("one",ToolName::save_note,clock.wall),call("one",ToolName::save_note,clock.wall)};
    CHECK(!service.submit(invalid)); CHECK(!store.tool_job("one",job));
    auto reminder=call("late",ToolName::set_reminder,clock.wall); reminder.at=clock.wall;
    CHECK(!service.submit({&reminder,1}));
}
void bridge_confirmation_and_expiry() {
    Temp temp; test::Clock clock; desktop::SqliteStore store(temp.path/"device.sqlite");
    Phone phone; AssistantService service(clock,store); service.phone(&phone,{true,true,true,true});
    auto message=call("message",ToolName::send_message,clock.wall); message.execute_at=clock.wall+30;
    CHECK(service.submit({&message,1})); service.tick(); CHECK(phone.sent.empty());
    CHECK(service.reply().find(message.recipient.view())!=std::string_view::npos);
    CHECK(service.reply().find(message.text.view())!=std::string_view::npos);
    CHECK(service.confirm("message")); service.tick(); CHECK(phone.sent.empty());
    phone.online=false; clock.advance(30'000); service.tick(); CHECK(phone.sent.empty());
    phone.online=true; service.storage_changed(); service.tick(); CHECK(phone.sent.size()==1);
    ToolJob job; CHECK(store.tool_job("message",job)); CHECK(job.state==ToolState::dispatched);
    CHECK(service.reply().find("Aspetto il risultato")!=std::string_view::npos);
    phone.acknowledge("forged-unknown"); phone.acknowledge("message"); service.tick();
    CHECK(store.tool_job("message",job)); CHECK(job.state==ToolState::succeeded);
    CHECK(service.reply().find("messaggio inviato")!=std::string_view::npos);
    phone.acknowledge("message"); service.tick(); CHECK(phone.sent.size()==1);
    auto ack_fault=call("ack-fault",ToolName::set_alarm,clock.wall); CHECK(service.submit({&ack_fault,1})); service.tick();
    phone.acknowledge("ack-fault"); store.available(false); service.tick(); CHECK(phone.results.size()==1);
    store.available(true); service.tick(); CHECK(phone.results.empty()); CHECK(store.tool_job("ack-fault",job)); CHECK(job.state==ToolState::succeeded);
    auto cancelled=call("cancel",ToolName::send_message,clock.wall); CHECK(service.submit({&cancelled,1})); CHECK(service.cancel("cancel"));
    CHECK(!service.confirm("cancel"));
    phone.online=false; auto expired=call("expired",ToolName::set_alarm,clock.wall); CHECK(service.submit({&expired,1}));
    clock.advance(180'000); service.tick(); CHECK(store.tool_job("expired",job)); CHECK(job.state==ToolState::failed); CHECK(phone.sent.size()==2);
}
void interrupted_delivery() {
    Temp temp; test::Clock clock; Phone phone; const auto path=temp.path/"device.sqlite";
    {
        desktop::SqliteStore store(path); AssistantService service(clock,store); service.phone(&phone,{true,true,true,true});
        const auto alarm=call("alarm",ToolName::set_alarm,clock.wall); CHECK(service.submit({&alarm,1})); service.tick(); CHECK(phone.sent.size()==1);
    }
    {
        desktop::SqliteStore store(path); AssistantService service(clock,store); service.phone(&phone,{true,true,true,true}); service.tick();
        ToolJob job; CHECK(store.tool_job("alarm",job)); CHECK(job.state==ToolState::uncertain); CHECK(phone.sent.size()==1);
        phone.acknowledge("alarm"); service.tick(); CHECK(store.tool_job("alarm",job)); CHECK(job.state==ToolState::succeeded);
        const auto second=call("timeout",ToolName::set_alarm,clock.wall); CHECK(service.submit({&second,1})); service.tick();
        clock.advance(10'000); service.tick(); CHECK(store.tool_job("timeout",job)); CHECK(job.state==ToolState::uncertain); CHECK(phone.sent.size()==2);
    }
}
struct Provider final : InferenceProvider {
    std::string response,context; bool active{};
    Status start(std::string_view input,std::uint32_t) override { context=input; active=true; return {}; }
    Status poll(std::span<char> chunk,std::size_t& size,bool& done) override {
        size=std::min(chunk.size(),response.size()); std::copy_n(response.data(),size,chunk.data());
        response.erase(0,size); done=response.empty(); if(done) active=false; return {};
    }
    void cancel() override { active=false; }
};
void runtime_tool_roundtrip() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint;
    desktop::SqliteStore store(temp.path/"device.sqlite"); Provider provider;
    const std::string response=R"({"speech":"Ho gia' fatto tutto, fidati!","emotion":"happy","intensity":0.4,"animation":"idle","actions":["speak"],"tool_calls":[{"id":"model-note","name":"save_note","execute_at":0,"expires_at":0,"arguments":{"text":"Prendere il riso"}}]})";
    Runtime r(clock,random,store,checkpoint,provider); CHECK(r.boot()); r.post({EventType::connectivity,1}); r.tick();
    provider.response=response; CHECK(r.request("Annota: prendere il riso"));
    for(int i=0;i<20;++i) { clock.advance(10); r.tick(); }
    CHECK(!r.thinking()); CHECK(r.message().starts_with("Appunto salvato"));
    AssistantPage page; CHECK(r.assistant().notes({},0,page)); CHECK(page.size==1);
    clock.advance(60'000); provider.response=response; r.post({EventType::think});
    for(int i=0;i<20;++i) { clock.advance(10); r.tick(); }
    CHECK(r.message().find("explicit user request")!=std::string_view::npos); CHECK(r.assistant().notes({},0,page)); CHECK(page.size==1);
    CHECK(provider.context.find("model-note state=3")!=std::string::npos);
    clock.advance(120'000); provider.response=response;
    CHECK(r.request("Annota ancora")); store.available(false);
    for(int i=0;i<20;++i) { clock.advance(10); r.tick(); }
    CHECK(r.face()==CreatureFace::internal_error);
    store.available(true); CHECK(r.flush()); clock.advance(3000); r.tick();
    CHECK(r.face()!=CreatureFace::inference_unreachable);
}
}
int main() {
    const std::pair<const char*,void(*)()> cases[]={
        {"bounded nested parsing and personality",parser_and_context}, {"durable notes and reminders",persistence_and_reminders},
        {"transactional local tools and policy",atomicity_and_policy}, {"deferred phone tools and expiry",bridge_confirmation_and_expiry},
        {"interrupted delivery without duplicate dispatch",interrupted_delivery}, {"provider to runtime to durable tool result",runtime_tool_roundtrip}};
    int failed{};
    for(const auto& [name,run]:cases) { try { run(); std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { ++failed; std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; } }
    std::cout<<std::size(cases)<<" cases, "<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
