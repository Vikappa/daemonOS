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
    Temp() { char p[]="/tmp/daemon-agent-XXXXXX"; const auto* value=mkdtemp(p); if(!value) throw std::runtime_error("mkdtemp"); path=value; }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(path,ec); }
};
void sql(const std::filesystem::path& path,const char* statement) {
    sqlite3* db{}; CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    const auto status=sqlite3_exec(db,statement,nullptr,nullptr,nullptr); sqlite3_close(db); CHECK(status==SQLITE_OK);
}
std::string response(std::string_view calls="",std::string_view speech="Risultati pronti, capo. Anche oggi ho fatto io il turno di memoria.") {
    return std::string{"{\"speech\":\""}+std::string{speech}+"\",\"emotion\":\"happy\",\"intensity\":0.5,\"animation\":\"idle\",\"actions\":[\"speak\"],\"tool_calls\":["+std::string{calls}+"]}";
}
std::string web_call(std::int64_t expires) {
    return "{\"id\":\"search\",\"name\":\"web.search\",\"execute_at\":0,\"expires_at\":"+std::to_string(expires)+",\"arguments\":{\"text\":\"notizie di prova\",\"binding\":\"search.primary\"}}";
}
std::string email_call(std::int64_t expires) {
    return "{\"id\":\"mail\",\"name\":\"email.send\",\"execute_at\":0,\"expires_at\":"+std::to_string(expires)+",\"arguments\":{\"text\":\"Arrivo presto\",\"recipient\":\"contacts:alice\",\"subject\":\"Aggiornamento\",\"binding\":\"mail.primary\"}}";
}
struct Provider final : InferenceProvider {
    std::vector<std::string> replies, contexts;
    std::size_t index{}, offset{}; bool active{}, hold{};
    Status start(std::string_view context,std::uint32_t) override {
        if(active) return Status::fail(Error::busy,"provider busy");
        if(index>=replies.size()) return Status::fail(Error::unavailable,"no scripted reply");
        contexts.emplace_back(context); offset=0; active=true; return {};
    }
    Status poll(std::span<char> chunk,std::size_t& size,bool& done) override {
        size=0; done=false;
        if(hold) return {};
        size=std::min(chunk.size(),replies[index].size()-offset);
        std::copy_n(replies[index].data()+offset,size,chunk.data()); offset+=size;
        done=offset==replies[index].size(); if(done) { active=false; ++index; }
        return {};
    }
    void cancel() override { active=false; }
};
struct Bridge final : PhoneTools {
    BridgeServiceCatalog catalog;
    bool online{true}, automatic{true};
    std::vector<ToolCall> sent;
    std::vector<ToolResult> results;
    Bridge() {
        ServiceBinding search; search.id.assign("search.primary"); search.label.assign("Configured search API"); search.operations=tool_bit(ToolName::web_search); CHECK(catalog.add(search));
        ServiceBinding mail; mail.id.assign("mail.primary"); mail.label.assign("Configured mail API");
        mail.operations=tool_bit(ToolName::email_send)|tool_bit(ToolName::email_search)|tool_bit(ToolName::email_read); CHECK(catalog.add(mail));
        ServiceBinding message; message.id.assign("chat.primary"); message.label.assign("Configured messenger");
        message.operations=tool_bit(ToolName::send_message)|tool_bit(ToolName::notification_reply); CHECK(catalog.add(message));
    }
    bool supports(ToolName name) const override { return catalog.supports(name,{}); }
    std::span<const ServiceBinding> services() const override { return catalog.bindings(); }
    bool authenticated() const override { return online; }
    Status dispatch(const ToolCall& call) override {
        if(!supports_binding(call.name,call.binding.view())) return Status::fail(Error::denied,"unknown binding");
        sent.push_back(call);
        if(automatic) {
            ToolResult result; result.id=call.id; result.success=true; result.detail.assign("Configured service completed");
            result.data.assign("Risultato verificabile della ricerca: il gatto ha occupato la tastiera.");
            result.source_count=1; result.sources[0].title.assign("Test source"); result.sources[0].url.assign("https://example.test/news");
            results.push_back(result);
        }
        return {};
    }
    bool poll(ToolResult& result) override { if(results.empty()) return false; result=results.front(); return true; }
    void accept_result(std::string_view id) override { if(!results.empty() && results.front().id.view()==id) results.erase(results.begin()); }
};
ToolPolicy policy() {
    ToolPolicy p; p.services=tool_bit(ToolName::web_search)|tool_bit(ToolName::email_send)|tool_bit(ToolName::email_search)|tool_bit(ToolName::email_read)|tool_bit(ToolName::send_message)|tool_bit(ToolName::notification_reply); return p;
}
AgentTaskRequest task(std::string_view id,std::int64_t now,ToolMask grants=tool_bit(ToolName::web_search)) {
    AgentTaskRequest request; request.id.assign(id); request.instruction.assign("Cerca le notizie e poi raccontami i risultati con le fonti.");
    request.execute_at=now+60; request.expires_at=now+600; request.grants=grants; return request;
}
void pump(Runtime& runtime,test::Clock& clock,int seconds) {
    for(int i=0;i<seconds;++i) { clock.advance(1000); runtime.tick(); }
}
void scheduled_research_roundtrip() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint;
    desktop::SqliteStore store(temp.path/"device.sqlite"); Provider provider; Bridge bridge;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
    runtime.post({EventType::connectivity,1}); runtime.post({EventType::rest}); runtime.tick(); CHECK(runtime.creature().sleeping);
    const auto request=task("morning",clock.wall); provider.replies={response(web_call(request.expires_at)),response()};
    CHECK(runtime.schedule_agent_task(request)); std::int64_t wake=-1; CHECK(runtime.next_wake_at(wake)); CHECK(wake==request.execute_at);
    pump(runtime,clock,59); CHECK(provider.contexts.empty()); CHECK(runtime.creature().sleeping);
    pump(runtime,clock,20); CHECK(provider.contexts.size()==2); CHECK(bridge.sent.size()==1);
    AgentTask saved; CHECK(runtime.agent_scheduler().get("morning",saved)); CHECK(saved.state==AgentTaskState::completed); CHECK(saved.turn==2);
    CHECK(runtime.resources().used(MemoryCategory::cognitive)==0); CHECK(runtime.power_manager().active_locks()==0);
    CHECK(saved.notice_pending); CHECK(runtime.agent_result().request.id.view()=="morning"); CHECK(!runtime.creature().sleeping);
    CHECK(runtime.message()==saved.speech.view()); CHECK(provider.contexts[1].find("https://example.test/news")!=std::string::npos);
    CHECK(provider.contexts[1].find("il gatto ha occupato")!=std::string::npos);
    CHECK(provider.contexts[1].find("schedule_agent_task bit=16 enabled=0")!=std::string::npos);
    CHECK(runtime.acknowledge_agent_result("morning")); CHECK(runtime.agent_scheduler().get("morning",saved)); CHECK(!saved.notice_pending);
    CHECK(runtime.next_wake_at(wake)); CHECK(wake==-1);
}
void schedule_from_structured_response() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint;
    desktop::SqliteStore store(temp.path/"device.sqlite"); Provider provider; Bridge bridge;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
    runtime.post({EventType::connectivity,1}); runtime.tick();
    const auto at=clock.wall+120, expires=at+600;
    provider.replies={response("{\"id\":\"planned\",\"name\":\"schedule_agent_task\",\"execute_at\":0,\"expires_at\":"+std::to_string(expires)+",\"arguments\":{\"text\":\"Cerca notizie\",\"at\":"+std::to_string(at)+"}}"),response(web_call(expires)),response()};
    CHECK(runtime.request("Tra due minuti cerca le notizie e raccontamele")); pump(runtime,clock,10);
    AgentTask saved; CHECK(runtime.agent_scheduler().get("planned",saved)); CHECK(saved.state==AgentTaskState::scheduled);
    CHECK(saved.request.grants & tool_bit(ToolName::web_search)); CHECK(!(saved.request.grants & tool_bit(ToolName::schedule_agent_task)));
    pump(runtime,clock,140); CHECK(runtime.agent_scheduler().get("planned",saved)); CHECK(saved.state==AgentTaskState::completed);
    CHECK(bridge.sent.size()==1); CHECK(provider.contexts.size()==3);
}
void restart_and_durable_notice() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint; Bridge bridge; bridge.automatic=false;
    const auto path=temp.path/"device.sqlite"; const auto request=task("restart",clock.wall);
    {
        desktop::SqliteStore store(path); Provider provider; provider.replies={response(web_call(request.expires_at))};
        Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
        runtime.post({EventType::connectivity,1}); runtime.tick(); CHECK(runtime.schedule_agent_task(request)); pump(runtime,clock,70);
        AgentTask saved; CHECK(runtime.agent_scheduler().get("restart",saved)); CHECK(saved.state==AgentTaskState::waiting_tools); CHECK(bridge.sent.size()==1);
    }
    {
        desktop::SqliteStore store(path); Provider provider; provider.replies={response({},"Il servizio non ha confermato l'esito; non invento risultati.")};
        Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
        runtime.post({EventType::connectivity,1}); runtime.tick(); pump(runtime,clock,10);
        AgentTask saved; CHECK(runtime.agent_scheduler().get("restart",saved)); CHECK(saved.state==AgentTaskState::completed); CHECK(bridge.sent.size()==1);
        CHECK(provider.contexts.front().find("state=5")!=std::string::npos);
    }
    {
        desktop::SqliteStore store(path); Provider provider;
        Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.tick();
        CHECK(runtime.agent_result().request.id.view()=="restart"); CHECK(runtime.message().find("non invento")!=std::string_view::npos);
        CHECK(provider.contexts.empty()); CHECK(runtime.acknowledge_agent_result("restart"));
    }
}
void recover_uncommitted_inference() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint; Bridge bridge;
    const auto path=temp.path/"device.sqlite"; const auto request=task("interrupted",clock.wall);
    {
        desktop::SqliteStore store(path); Provider provider; provider.replies={response()}; provider.hold=true;
        Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
        runtime.post({EventType::connectivity,1}); runtime.tick(); CHECK(runtime.schedule_agent_task(request)); pump(runtime,clock,61);
        AgentTask saved; CHECK(runtime.agent_scheduler().get("interrupted",saved)); CHECK(saved.state==AgentTaskState::inferring); CHECK(saved.attempts==1);
    }
    {
        desktop::SqliteStore store(path); Provider provider; provider.replies={response()};
        Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
        runtime.post({EventType::connectivity,1}); runtime.tick(); pump(runtime,clock,10);
        AgentTask saved; CHECK(runtime.agent_scheduler().get("interrupted",saved)); CHECK(saved.state==AgentTaskState::completed); CHECK(saved.attempts==2); CHECK(saved.turn==1);
    }
}
void transactional_turn_and_grants() {
    Temp temp; test::Clock clock; const auto path=temp.path/"device.sqlite";
    desktop::SqliteStore store(path); AgentScheduler scheduler(clock,store);
    const auto request=task("atomic",clock.wall);
    CHECK(!scheduler.schedule(request,0)); CHECK(scheduler.schedule(request,all_tools)); CHECK(scheduler.schedule(request,all_tools));
    auto conflict=request; conflict.instruction.assign("Different instruction"); CHECK(!scheduler.schedule(conflict,all_tools));
    clock.advance(60'000); AgentTask selected; CHECK(scheduler.next(selected)); CHECK(scheduler.begin("atomic"));
    ToolCall search; search.id.assign("model"); search.name=ToolName::web_search; search.text.assign("test"); search.expires_at=request.expires_at;
    sql(path,"CREATE TRIGGER deny_turn BEFORE UPDATE OF turn ON agent_tasks WHEN NEW.turn>OLD.turn BEGIN SELECT RAISE(ABORT,'fault'); END;");
    CHECK(!scheduler.commit(selected,{&search,1},"")); ToolPage jobs; CHECK(store.tool_results(jobs)); CHECK(jobs.size==0);
    AgentTask saved; CHECK(scheduler.get("atomic",saved)); CHECK(saved.state==AgentTaskState::inferring); CHECK(saved.turn==0);
    sql(path,"DROP TRIGGER deny_turn;");
    auto unauthorized=search; unauthorized.name=ToolName::email_search; CHECK(!scheduler.commit(selected,{&unauthorized,1},""));
    CHECK(scheduler.commit(selected,{&search,1},"")); CHECK(store.tool_results(jobs)); CHECK(jobs.size==1); CHECK(jobs.items[0].call.id.view().starts_with("task-"));
    CHECK(!scheduler.commit(selected,{&search,1},"")); CHECK(store.tool_results(jobs)); CHECK(jobs.size==1);
}
void offline_expiry_and_cancelled_message() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint;
    desktop::SqliteStore store(temp.path/"device.sqlite"); Provider provider; Bridge bridge;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
    auto expired=task("expired",clock.wall); expired.expires_at=expired.execute_at+10;
    CHECK(runtime.schedule_agent_task(expired)); pump(runtime,clock,80);
    AgentTask saved; CHECK(runtime.agent_scheduler().get("expired",saved)); CHECK(saved.state==AgentTaskState::failed); CHECK(provider.contexts.empty());
    CHECK(runtime.acknowledge_agent_result("expired"));
    auto mail=task("mail",clock.wall,tool_bit(ToolName::email_send)); provider.replies={response(email_call(mail.expires_at)),response()};
    runtime.post({EventType::connectivity,1}); runtime.tick(); CHECK(runtime.schedule_agent_task(mail)); pump(runtime,clock,70);
    CHECK(runtime.agent_scheduler().get("mail",saved)); CHECK(saved.state==AgentTaskState::waiting_tools); CHECK(bridge.sent.empty());
    ToolPage jobs; CHECK(store.agent_tool_results("mail",1,jobs)); CHECK(jobs.size==1); CHECK(jobs.items[0].state==ToolState::needs_confirmation);
    CHECK(runtime.agent_scheduler().cancel("mail")); CHECK(!runtime.assistant().confirm(jobs.items[0].call.id.view())); pump(runtime,clock,10);
    CHECK(bridge.sent.empty()); CHECK(provider.contexts.size()==1); CHECK(runtime.agent_scheduler().get("mail",saved)); CHECK(saved.state==AgentTaskState::cancelled);
}
void bounded_agent_loop_and_service_bindings() {
    Temp temp; test::Clock clock; test::Random random; test::Checkpoint checkpoint;
    desktop::SqliteStore store(temp.path/"device.sqlite"); Provider provider; Bridge bridge;
    Runtime runtime(clock,random,store,checkpoint,provider); CHECK(runtime.boot()); runtime.assistant().phone(&bridge,policy());
    const auto request=task("loop",clock.wall); provider.replies.assign(4,response(web_call(request.expires_at)));
    runtime.post({EventType::connectivity,1}); runtime.tick(); CHECK(runtime.schedule_agent_task(request)); pump(runtime,clock,100);
    AgentTask saved; CHECK(runtime.agent_scheduler().get("loop",saved)); CHECK(saved.state==AgentTaskState::failed); CHECK(provider.contexts.size()==4); CHECK(bridge.sent.size()==3);
    ToolCall wrong; wrong.id.assign("wrong"); wrong.name=ToolName::web_search; wrong.text.assign("test"); wrong.binding.assign("mail.primary"); wrong.expires_at=clock.wall+60;
    CHECK(!runtime.assistant().submit({&wrong,1}));
    ServiceBinding duplicate=bridge.services().front(); CHECK(!bridge.catalog.add(duplicate));
    ServiceBinding invalid; invalid.id.assign("other"); invalid.label.assign("bad"); invalid.operations=tool_bit(ToolName::save_note); CHECK(!bridge.catalog.add(invalid));
    ToolResult bad; bad.id.assign("result"); bad.success=true; bad.source_count=1; bad.sources[0].url.assign("javascript:alert(1)"); CHECK(!validate_service_result(bad));
}
void migrated_ledger_preserves_jobs() {
    Temp temp; const auto path=temp.path/"device.sqlite"; test::Clock clock;
    {
        desktop::SqliteStore store(path); AssistantService assistant(clock,store);
        ToolCall note; note.id.assign("legacy"); note.text.assign("Keep this note"); CHECK(assistant.submit({&note,1})); assistant.tick();
    }
    sql(path,"DROP INDEX memories_actor; DROP INDEX entries_actor; DROP INDEX tools_actor; ALTER TABLE memories DROP COLUMN actor_id; ALTER TABLE assistant_entries DROP COLUMN actor_id; ALTER TABLE tool_jobs DROP COLUMN actor_id; ALTER TABLE tool_jobs DROP COLUMN approved_by; ALTER TABLE tool_jobs DROP COLUMN approval_revision; ALTER TABLE agent_tasks DROP COLUMN actor_id; DROP TABLE voice_encounters; DROP TABLE voice_templates; DROP TABLE voice_profiles; DELETE FROM settings WHERE key='voice_guest_tracking'; DELETE FROM schema_migrations WHERE version=5; ALTER TABLE tool_jobs RENAME TO jobs_v4; CREATE TABLE tool_jobs(id TEXT PRIMARY KEY,name INTEGER NOT NULL CHECK(name BETWEEN 0 AND 3),execute_at INTEGER NOT NULL,expires_at INTEGER NOT NULL,at INTEGER NOT NULL,content TEXT NOT NULL,recipient TEXT NOT NULL,state INTEGER NOT NULL,result TEXT NOT NULL DEFAULT '',updated_serial INTEGER NOT NULL DEFAULT 0); INSERT INTO tool_jobs SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,updated_serial FROM jobs_v4; DROP TABLE jobs_v4; DROP TABLE agent_tasks; CREATE INDEX tool_jobs_due ON tool_jobs(state,execute_at); DELETE FROM schema_migrations WHERE version=4; PRAGMA user_version=3;");
    desktop::SqliteStore store(path); CHECK(store.integrity()); ToolJob job; CHECK(store.tool_job("legacy",job)); CHECK(job.state==ToolState::succeeded);
    AssistantPage notes; CHECK(store.entries(EntryKind::note,{},0,notes)); CHECK(notes.size==1); CHECK(notes.items[0].content.view()=="Keep this note");
    AgentScheduler scheduler(clock,store); CHECK(scheduler.schedule(task("new",clock.wall),all_tools));
}
void duplicate_external_effect_and_confirmation_payload() {
    Temp temp; test::Clock clock; desktop::SqliteStore store(temp.path/"device.sqlite"); Bridge bridge;
    AgentScheduler scheduler(clock,store); AssistantService assistant(clock,store); assistant.phone(&bridge,policy());
    auto request=task("dedup",clock.wall,tool_bit(ToolName::email_send)); CHECK(scheduler.schedule(request,all_tools)); clock.advance(60'000);
    AgentTask selected; CHECK(scheduler.next(selected)); CHECK(scheduler.begin("dedup"));
    ToolCall mail; mail.id.assign("model-id"); mail.name=ToolName::email_send; mail.text.assign("Arrivo"); mail.subject.assign("Orario");
    mail.recipient.assign("contacts:alice"); mail.binding.assign("mail.primary"); mail.expires_at=request.expires_at;
    std::array calls{mail,mail}; calls[1].id.assign("duplicate"); CHECK(scheduler.commit(selected,calls,""));
    ToolPage jobs; CHECK(store.agent_tool_results("dedup",1,jobs)); CHECK(jobs.size==2);
    CHECK(jobs.items[0].state==ToolState::needs_confirmation); CHECK(jobs.items[1].state==ToolState::cancelled);
    ToolJob confirmation; CHECK(assistant.confirmation(confirmation)); CHECK(confirmation.call.subject==mail.subject);
    CHECK(confirmation.call.binding==mail.binding); CHECK(confirmation.call.recipient==mail.recipient);
    CHECK(assistant.confirm(confirmation.call.id.view())); assistant.tick(); assistant.tick(); CHECK(bridge.sent.size()==1);
    CHECK(scheduler.next(selected)); CHECK(scheduler.begin("dedup")); CHECK(scheduler.commit(selected,{&mail,1},""));
    CHECK(store.agent_tool_results("dedup",2,jobs)); CHECK(jobs.size==1); CHECK(jobs.items[0].state==ToolState::succeeded);
    assistant.tick(); CHECK(bridge.sent.size()==1); CHECK(scheduler.next(selected)); CHECK(scheduler.begin("dedup"));
    CHECK(scheduler.commit(selected,{},"Esito recuperato senza ripetere l'invio."));
}
void bounded_service_context_and_mock_truthfulness() {
    test::Clock clock; test::Random random; ContextBuilder builder; MemoryPage memories; ToolPage results;
    for(auto& memory:memories.items) memory.content.assign(std::string(383,'m'));
    memories.size=memories.items.size();
    results.size=results.items.size();
    for(std::size_t i=0;i<results.size;++i) {
        auto& job=results.items[i]; job.call.id.assign(i ? "second" : "first"); job.call.name=ToolName::web_search; job.state=ToolState::succeeded;
        job.result.assign(std::string(191,'r')); job.output.data.assign(std::string(767,'d')); job.output.source_count=2;
        for(auto& source:job.output.sources) { source.title.assign(std::string(63,'t')); source.url.assign("https://example.test/"+std::string(170,'u')); }
    }
    BridgeServiceCatalog catalog;
    for(std::size_t i=0;i<budget::service_bindings;++i) {
        ServiceBinding service; service.id.assign("service-"+std::to_string(i)); service.label.assign("Configured API"); service.operations=bridge_tools; CHECK(catalog.add(service));
    }
    auto request=task("context",clock.wall); AgentTask scheduled; scheduled.request=request;
    auto p=policy(); p.phone_alarms=true; p.phone_messages=true;
    CHECK(builder.build(CreatureEngine::create(clock,random),memories,std::string(383,'x'),p,results,catalog.bindings(),&scheduled));
    CHECK(builder.context().size()<budget::context_bytes); CHECK(builder.context().find("id=second state=3")!=std::string_view::npos);
    MockProvider mock; CHECK(!mock.start(builder.context(),512));
}
}
int main() {
    const std::pair<const char*,void(*)()> cases[]={
        {"scheduled research, result context and wake",scheduled_research_roundtrip}, {"schedule from inference",schedule_from_structured_response},
        {"restart reconciliation and durable result",restart_and_durable_notice}, {"safe recovery of uncommitted inference",recover_uncommitted_inference},
        {"atomic tool turn and immutable grants",transactional_turn_and_grants}, {"offline expiry and cancellation",offline_expiry_and_cancelled_message},
        {"bounded inference and service catalog",bounded_agent_loop_and_service_bindings}, {"schema migration preserves ledger",migrated_ledger_preserves_jobs},
        {"effect deduplication and confirmation interface",duplicate_external_effect_and_confirmation_payload}, {"bounded context and truthful mock",bounded_service_context_and_mock_truthfulness}};
    int failures{};
    for(const auto& [name,run]:cases) { try { run(); std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& error) { ++failures; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; } }
    std::cout<<std::size(cases)<<" cases, "<<checks<<" checks, "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
