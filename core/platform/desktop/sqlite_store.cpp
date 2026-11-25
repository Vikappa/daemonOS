#include "daemon/platform/desktop.hpp"
#include "daemon/services/assistant.hpp"
#include "daemon/services/agent_scheduler.hpp"
#include <sqlite3.h>
#include <limits>
#include <stdexcept>

namespace daemonos::desktop {
namespace {
class DbError : public std::runtime_error {
public: DbError(int error_code, const char* message) : std::runtime_error(message), code(error_code) {} int code;
};
Status db_status(const DbError& error) {
    const int code = error.code & 0xff;
    return Status::fail(code == SQLITE_CORRUPT || code == SQLITE_NOTADB ? Error::corrupt :
                        code == SQLITE_BUSY || code == SQLITE_LOCKED || code == SQLITE_INTERRUPT ? Error::busy :
                        code == SQLITE_FULL ? Error::full : Error::io, error.what());
}
void check(sqlite3* db, int result) { if (result != SQLITE_OK) throw DbError(result, sqlite3_errmsg(db)); }
void exec(sqlite3* db, const char* sql) { check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) { check(db, sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr)); }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void text(int column, std::string_view value) { check(db_, sqlite3_bind_text(stmt_, column, value.empty() ? "" : value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)); }
    void integer(int column, std::int64_t value) { check(db_, sqlite3_bind_int64(stmt_, column, value)); }
    void real(int column, double value) { check(db_, sqlite3_bind_double(stmt_, column, value)); }
    void blob(int column,std::span<const std::byte> value) { check(db_,sqlite3_bind_blob(stmt_,column,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT)); }
    std::span<const std::byte> blob(int column) const { return {static_cast<const std::byte*>(sqlite3_column_blob(stmt_,column)),static_cast<std::size_t>(sqlite3_column_bytes(stmt_,column))}; }
    bool row() { const int result = sqlite3_step(stmt_); if (result == SQLITE_ROW) return true; if (result == SQLITE_DONE) return false; throw DbError(result, sqlite3_errmsg(db_)); }
    void done() { if (row()) throw DbError(SQLITE_MISUSE, "unexpected query result"); }
    std::int64_t integer(int column) const { return sqlite3_column_int64(stmt_, column); }
    double real(int column) const { return sqlite3_column_double(stmt_, column); }
    bool is_null(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }
    std::string_view text(int column) const {
        const auto* data = sqlite3_column_text(stmt_, column);
        return data ? std::string_view{reinterpret_cast<const char*>(data), static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column))} : std::string_view{};
    }
private: sqlite3* db_{}; sqlite3_stmt* stmt_{};
};
class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) { exec(db_, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db_, "COMMIT"); committed_ = true; }
private: sqlite3* db_; bool committed_{};
};
class QueryBudget {
public:
    explicit QueryBudget(sqlite3* db):db_(db) {
        sqlite3_progress_handler(db_,1000,+[](void* self)->int { return --static_cast<QueryBudget*>(self)->remaining_<=0 ? 1 : 0; },this);
    }
    ~QueryBudget() { sqlite3_progress_handler(db_,0,nullptr,nullptr); }
private: sqlite3* db_; int remaining_{100}; // At most ~100,000 VM instructions.
};
constexpr const char* migration1 = R"SQL(
CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL);
CREATE TABLE creature(id TEXT PRIMARY KEY CHECK(length(id) BETWEEN 1 AND 39),
 slot INTEGER NOT NULL UNIQUE CHECK(slot=1), name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 31),
 born_at INTEGER NOT NULL, last_wall INTEGER NOT NULL, age_seconds INTEGER NOT NULL,
 pending_ms INTEGER NOT NULL CHECK(pending_ms BETWEEN 0 AND 59999), revision INTEGER NOT NULL, sleeping INTEGER NOT NULL CHECK(sleeping IN (0,1)));
CREATE TABLE personality(creature_id TEXT PRIMARY KEY REFERENCES creature(id), curiosity REAL NOT NULL, sociability REAL NOT NULL, resilience REAL NOT NULL);
CREATE TABLE needs(creature_id TEXT PRIMARY KEY REFERENCES creature(id), hunger REAL NOT NULL, energy REAL NOT NULL,
 health REAL NOT NULL, happiness REAL NOT NULL, affection REAL NOT NULL, boredom REAL NOT NULL, stress REAL NOT NULL, curiosity REAL NOT NULL);
CREATE TABLE memories(id INTEGER PRIMARY KEY, creature_id TEXT NOT NULL REFERENCES creature(id), timestamp INTEGER NOT NULL,
 category TEXT NOT NULL, content TEXT NOT NULL CHECK(length(content) <= 383), provenance TEXT NOT NULL,
 importance REAL NOT NULL CHECK(importance BETWEEN 0 AND 1), valence REAL NOT NULL CHECK(valence BETWEEN -1 AND 1),
 recall_count INTEGER NOT NULL DEFAULT 0, last_recall INTEGER NOT NULL DEFAULT 0, tags TEXT NOT NULL DEFAULT '',
 entities TEXT NOT NULL DEFAULT '', summary TEXT NOT NULL DEFAULT '');
CREATE TABLE memory_links(source INTEGER REFERENCES memories(id), target INTEGER REFERENCES memories(id), PRIMARY KEY(source,target));
CREATE TABLE conversations(id INTEGER PRIMARY KEY, creature_id TEXT REFERENCES creature(id), timestamp INTEGER NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL);
CREATE TABLE relationships(id INTEGER PRIMARY KEY, creature_id TEXT REFERENCES creature(id), entity TEXT NOT NULL, affinity REAL NOT NULL DEFAULT 0);
CREATE TABLE events(id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL);
CREATE TABLE intentions(id INTEGER PRIMARY KEY, creature_id TEXT REFERENCES creature(id), due_at INTEGER, description TEXT NOT NULL, status TEXT NOT NULL);
CREATE TABLE knowledge(id INTEGER PRIMARY KEY, creature_id TEXT REFERENCES creature(id), subject TEXT NOT NULL, content TEXT NOT NULL, provenance TEXT NOT NULL);
CREATE TABLE settings(key TEXT PRIMARY KEY, value INTEGER NOT NULL);
INSERT INTO schema_migrations VALUES(1,unixepoch());
PRAGMA user_version=1;
)SQL";
constexpr const char* migration2 = R"SQL(
CREATE INDEX memories_recent ON memories(timestamp DESC,id DESC);
CREATE INDEX memories_importance ON memories(importance DESC,id DESC);
INSERT INTO schema_migrations VALUES(2,unixepoch());
PRAGMA user_version=2;
)SQL";
constexpr const char* migration3 = R"SQL(
CREATE TABLE assistant_entries(id INTEGER PRIMARY KEY, kind INTEGER NOT NULL CHECK(kind IN (0,1)),
 created_at INTEGER NOT NULL CHECK(created_at>=0), due_at INTEGER NOT NULL CHECK(due_at>=0),
 content TEXT NOT NULL CHECK(length(CAST(content AS BLOB)) BETWEEN 1 AND 255), done INTEGER NOT NULL DEFAULT 0 CHECK(done IN (0,1)),
 CHECK((kind=0 AND due_at=0) OR (kind=1 AND due_at>0)));
CREATE INDEX assistant_due ON assistant_entries(kind,done,due_at,id);
CREATE TABLE tool_jobs(id TEXT PRIMARY KEY, name INTEGER NOT NULL CHECK(name BETWEEN 0 AND 3),
 execute_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, at INTEGER NOT NULL,
 content TEXT NOT NULL, recipient TEXT NOT NULL, state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 6),
 result TEXT NOT NULL DEFAULT '', updated_serial INTEGER NOT NULL DEFAULT 0);
CREATE INDEX tool_jobs_due ON tool_jobs(state,execute_at);
INSERT INTO schema_migrations VALUES(3,unixepoch());
PRAGMA user_version=3;
)SQL";
constexpr const char* migration4 = R"SQL(
CREATE TABLE agent_tasks(serial INTEGER PRIMARY KEY, id TEXT NOT NULL UNIQUE,
 instruction TEXT NOT NULL CHECK(length(CAST(instruction AS BLOB)) BETWEEN 1 AND 383),
 execute_at INTEGER NOT NULL CHECK(execute_at>=0), expires_at INTEGER NOT NULL CHECK(expires_at>execute_at),
 grants INTEGER NOT NULL, wake INTEGER NOT NULL CHECK(wake IN (0,1)), state INTEGER NOT NULL DEFAULT 0 CHECK(state BETWEEN 0 AND 5),
 turn INTEGER NOT NULL DEFAULT 0 CHECK(turn BETWEEN 0 AND 4), attempts INTEGER NOT NULL DEFAULT 0 CHECK(attempts BETWEEN 0 AND 8),
 speech TEXT NOT NULL DEFAULT '', notice INTEGER NOT NULL DEFAULT 0 CHECK(notice IN (0,1)));
CREATE INDEX agent_tasks_due ON agent_tasks(state,execute_at,expires_at);
ALTER TABLE tool_jobs RENAME TO tool_jobs_v3;
CREATE TABLE tool_jobs(id TEXT PRIMARY KEY, name INTEGER NOT NULL CHECK(name BETWEEN 0 AND 9),
 execute_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, at INTEGER NOT NULL,
 content TEXT NOT NULL, recipient TEXT NOT NULL, state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 6),
 result TEXT NOT NULL DEFAULT '', updated_serial INTEGER NOT NULL DEFAULT 0,
 binding TEXT NOT NULL DEFAULT '', subject TEXT NOT NULL DEFAULT '', reference TEXT NOT NULL DEFAULT '',
 data TEXT NOT NULL DEFAULT '', source_count INTEGER NOT NULL DEFAULT 0 CHECK(source_count BETWEEN 0 AND 2),
 source1_title TEXT NOT NULL DEFAULT '', source1_url TEXT NOT NULL DEFAULT '',
 source2_title TEXT NOT NULL DEFAULT '', source2_url TEXT NOT NULL DEFAULT '',
 error INTEGER NOT NULL DEFAULT 0 CHECK(error BETWEEN 0 AND 7), grants INTEGER NOT NULL DEFAULT 0,
 task_id TEXT REFERENCES agent_tasks(id), task_turn INTEGER NOT NULL DEFAULT 0);
INSERT INTO tool_jobs(id,name,execute_at,expires_at,at,content,recipient,state,result,updated_serial)
 SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,updated_serial FROM tool_jobs_v3;
DROP TABLE tool_jobs_v3;
CREATE INDEX tool_jobs_due ON tool_jobs(state,execute_at);
CREATE INDEX tool_jobs_task ON tool_jobs(task_id,task_turn);
INSERT INTO schema_migrations VALUES(4,unixepoch());
PRAGMA user_version=4;
)SQL";
constexpr const char* migration5 = R"SQL(
ALTER TABLE memories ADD COLUMN actor_id TEXT NOT NULL DEFAULT '';
ALTER TABLE assistant_entries ADD COLUMN actor_id TEXT NOT NULL DEFAULT '';
ALTER TABLE tool_jobs ADD COLUMN actor_id TEXT NOT NULL DEFAULT '';
ALTER TABLE tool_jobs ADD COLUMN approved_by TEXT NOT NULL DEFAULT '';
ALTER TABLE tool_jobs ADD COLUMN approval_revision INTEGER NOT NULL DEFAULT 0;
ALTER TABLE agent_tasks ADD COLUMN actor_id TEXT NOT NULL DEFAULT '';
CREATE TABLE voice_profiles(serial INTEGER PRIMARY KEY, id TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
 role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 2), enabled INTEGER NOT NULL CHECK(enabled IN (0,1)),
 name_confirmed INTEGER NOT NULL CHECK(name_confirmed IN (0,1)), grants INTEGER NOT NULL, voice_grants INTEGER NOT NULL,
 revision INTEGER NOT NULL CHECK(revision BETWEEN 1 AND 1000000000),
 binding1 TEXT NOT NULL DEFAULT '', binding2 TEXT NOT NULL DEFAULT '', binding3 TEXT NOT NULL DEFAULT '', binding4 TEXT NOT NULL DEFAULT '');
CREATE UNIQUE INDEX voice_single_owner ON voice_profiles(role) WHERE role=0;
CREATE TABLE voice_templates(profile_id TEXT NOT NULL REFERENCES voice_profiles(id), slot INTEGER NOT NULL CHECK(slot BETWEEN 0 AND 2),
 encoder TEXT NOT NULL, scheme TEXT NOT NULL, sealed BLOB NOT NULL CHECK(length(sealed) BETWEEN 1 AND 1024), PRIMARY KEY(profile_id,slot));
CREATE TABLE voice_encounters(serial INTEGER PRIMARY KEY, profile_id TEXT NOT NULL REFERENCES voice_profiles(id),
 capture_id TEXT NOT NULL UNIQUE, context_id TEXT NOT NULL, timestamp INTEGER NOT NULL CHECK(timestamp>=0),
 similarity REAL NOT NULL CHECK(similarity BETWEEN -1 AND 1), claimed_name TEXT NOT NULL);
CREATE INDEX voice_timeline ON voice_encounters(profile_id,timestamp DESC,serial DESC);
CREATE INDEX memories_actor ON memories(actor_id,id);
CREATE INDEX entries_actor ON assistant_entries(actor_id,id);
CREATE INDEX tools_actor ON tool_jobs(actor_id,updated_serial);
INSERT INTO settings(key,value) VALUES('voice_guest_tracking',0);
INSERT INTO schema_migrations VALUES(5,unixepoch());
PRAGMA user_version=5;
)SQL";
Status read_entry(Statement& q, AssistantEntry& output) {
    AssistantEntry e;
    const auto kind = q.integer(1), done = q.integer(5);
    if (q.integer(0) <= 0 || kind < 0 || kind > 1 || done < 0 || done > 1 ||
        q.integer(2) < 0 || (kind==0 ? q.integer(3)!=0 : q.integer(3)<=0) || q.text(4).empty() || !e.content.assign(q.text(4)))
        return Status::fail(Error::corrupt, "invalid persisted assistant entry");
    e.id=q.integer(0); e.kind=static_cast<EntryKind>(kind); e.created_at=q.integer(2); e.due_at=q.integer(3); e.done=done!=0;
    if(!e.actor_id.assign(q.text(6))) return Status::fail(Error::corrupt,"invalid entry actor");
    output=e; return {};
}
std::int64_t insert_entry(sqlite3* db, const AssistantEntry& e) {
    Statement count(db, "SELECT count(*) FROM assistant_entries"); count.row();
    if (count.integer(0) >= static_cast<std::int64_t>(budget::assistant_entries)) throw DbError(SQLITE_FULL, "assistant entry limit reached");
    Statement q(db, "INSERT INTO assistant_entries(kind,created_at,due_at,content,done,actor_id) VALUES(?1,?2,?3,?4,0,?5)");
    q.integer(1,static_cast<std::int64_t>(e.kind)); q.integer(2,e.created_at); q.integer(3,e.due_at); q.text(4,e.content.view()); q.text(5,e.actor_id.view()); q.done();
    return sqlite3_last_insert_rowid(db);
}
Status read_tool(Statement& q, ToolJob& output) {
    ToolJob j;
    const auto name=q.integer(1), state=q.integer(7);
    if (name < 0 || name >= static_cast<std::int64_t>(ToolName::count) || state < 0 || state > 6 || !j.call.id.assign(q.text(0)) ||
        !j.call.text.assign(q.text(5)) || !j.call.recipient.assign(q.text(6)) || !j.result.assign(q.text(8)))
        return Status::fail(Error::corrupt, "invalid persisted tool job");
    j.call.name=static_cast<ToolName>(name); j.call.execute_at=q.integer(2); j.call.expires_at=q.integer(3); j.call.at=q.integer(4);
    j.state=static_cast<ToolState>(state);
    j.output.id=j.call.id; j.output.success=j.state==ToolState::succeeded; j.output.detail=j.result;
    const auto count=q.integer(13), error=q.integer(18), grants=q.integer(19);
    if(count<0 || count>2 || error<0 || error>7 || grants<0 || grants>all_tools ||
        !j.call.binding.assign(q.text(9)) || !j.call.subject.assign(q.text(10)) || !j.call.reference.assign(q.text(11)) ||
        !j.output.data.assign(q.text(12)) || !j.output.sources[0].title.assign(q.text(14)) || !j.output.sources[0].url.assign(q.text(15)) ||
        !j.output.sources[1].title.assign(q.text(16)) || !j.output.sources[1].url.assign(q.text(17)))
        return Status::fail(Error::corrupt,"persisted service result exceeds limits");
    j.grants=static_cast<ToolMask>(grants); j.output.source_count=static_cast<std::size_t>(count); j.output.error=static_cast<ServiceError>(error);
    if(!validate_tool(j.call) || !validate_service_result(j.output)) return Status::fail(Error::corrupt,"invalid persisted service data");
    if(!j.actor_id.assign(q.text(20)) || !j.approved_by.assign(q.text(21)) || q.integer(22)<0) return Status::fail(Error::corrupt,"invalid job actor");
    j.approval_revision=static_cast<std::uint64_t>(q.integer(22));
    output=j; return {};
}
Status read_task(Statement& q, AgentTask& task) {
    AgentTask t;
    const auto state=q.integer(7), turn=q.integer(8), attempts=q.integer(9), grants=q.integer(5);
    if(q.integer(0)<=0 || state<0 || state>5 || turn<0 || turn>budget::agent_turns || attempts<0 || attempts>budget::agent_attempts ||
        grants<0 || grants>all_tools || !t.request.id.assign(q.text(1)) ||
        !t.request.instruction.assign(q.text(2)) || !t.speech.assign(q.text(10)) || q.integer(6)<0 || q.integer(6)>1 || q.integer(11)<0 || q.integer(11)>1)
        return Status::fail(Error::corrupt,"invalid persisted agent task");
    t.serial=q.integer(0); t.request.execute_at=q.integer(3); t.request.expires_at=q.integer(4); t.request.grants=static_cast<ToolMask>(grants);
    t.request.wake_on_complete=q.integer(6)!=0; t.state=static_cast<AgentTaskState>(state); t.turn=static_cast<unsigned>(turn);
    t.attempts=static_cast<unsigned>(attempts); t.notice_pending=q.integer(11)!=0;
    if(!validate_agent_task(t.request)) return Status::fail(Error::corrupt,"invalid persisted agent request");
    if(!t.request.actor_id.assign(q.text(12))) return Status::fail(Error::corrupt,"invalid task actor");
    task=t; return {};
}
Status insert_task(sqlite3* db,const AgentTaskRequest& request) {
    if(auto status=validate_agent_task(request); !status) return status;
    Statement existing(db,"SELECT serial,id,instruction,execute_at,expires_at,grants,wake,state,turn,attempts,speech,notice,actor_id FROM agent_tasks WHERE id=?1"); existing.text(1,request.id.view());
    if(existing.row()) {
        AgentTask task; if(auto status=read_task(existing,task); !status) return status;
        return task.request==request ? Status{} : Status::fail(Error::denied,"task id reused with different request");
    }
    Statement count(db,"SELECT count(*) FROM agent_tasks"); count.row();
    if(count.integer(0)>=static_cast<std::int64_t>(budget::agent_tasks)) return Status::fail(Error::full,"agent task ledger full");
    Statement q(db,"INSERT INTO agent_tasks(id,instruction,execute_at,expires_at,grants,wake,actor_id) VALUES(?1,?2,?3,?4,?5,?6,?7)");
    q.text(1,request.id.view()); q.text(2,request.instruction.view()); q.integer(3,request.execute_at); q.integer(4,request.expires_at);
    q.integer(5,request.grants); q.integer(6,request.wake_on_complete); q.text(7,request.actor_id.view()); q.done(); return {};
}
Status insert_tool(sqlite3* db,const ToolCall& c,bool confirm,ToolMask grants,std::string_view task_id={},unsigned turn=0,std::string_view actor={}) {
    if(auto status=validate_tool(c); !status) return status;
    Statement existing(db,"SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs WHERE id=?1"); existing.text(1,c.id.view());
    if(existing.row()) {
        ToolJob job; if(auto status=read_tool(existing,job); !status) return status;
        return job.call==c && job.actor_id.view()==actor && job.grants==grants ? Status{} : Status::fail(Error::denied,"tool id reused with different arguments");
    }
    Statement count(db,"SELECT count(*) FROM tool_jobs"); count.row();
    if(count.integer(0)>=static_cast<std::int64_t>(budget::tool_jobs)) return Status::fail(Error::full,"tool ledger full; explicit archival required");
    Statement q(db,"INSERT INTO tool_jobs(id,name,execute_at,expires_at,at,content,recipient,state,updated_serial,binding,subject,reference,grants,task_id,task_turn,actor_id) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,(SELECT coalesce(max(updated_serial),0)+1 FROM tool_jobs),?9,?10,?11,?12,?13,?14,?15)");
    q.text(1,c.id.view()); q.integer(2,static_cast<std::int64_t>(c.name)); q.integer(3,c.execute_at); q.integer(4,c.expires_at);
    q.integer(5,c.at); q.text(6,c.text.view()); q.text(7,c.recipient.view());
    q.integer(8,static_cast<std::int64_t>(confirm && message_tool(c.name) ? ToolState::needs_confirmation : ToolState::queued));
    q.text(9,c.binding.view()); q.text(10,c.subject.view()); q.text(11,c.reference.view()); q.integer(12,grants);
    // Unbound optional parameter stays SQL NULL, preserving the foreign key contract.
    if(!task_id.empty()) q.text(13,task_id);
    q.integer(14,turn); q.text(15,actor); q.done(); return {};
}
}
struct SqliteStore::Impl {
    sqlite3* db{};
    Status ready{};
    std::filesystem::path path{};
    ~Impl() { if (db) sqlite3_close(db); }
};
SqliteStore::SqliteStore(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    impl_->path = path;
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) { impl_->ready = Status::fail(Error::io, error.message()); return; }
    try {
        check(impl_->db, sqlite3_open_v2(path.c_str(), &impl_->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr));
        sqlite3_busy_timeout(impl_->db, 100);
        sqlite3_limit(impl_->db, SQLITE_LIMIT_LENGTH, 1 << 20);
        sqlite3_limit(impl_->db, SQLITE_LIMIT_SQL_LENGTH, 64 * 1024);
        exec(impl_->db, "PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF;");
        std::int64_t current{};
        {
            Statement version(impl_->db, "PRAGMA user_version");
            if (!version.row()) throw DbError(SQLITE_CORRUPT, "missing schema version");
            current = version.integer(0);
        }
        if (current > 5 || current < 0) { impl_->ready = Status::fail(Error::incompatible, "database schema is newer than this runtime"); return; }
        exec(impl_->db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA cache_size=-128; PRAGMA temp_store=MEMORY; PRAGMA mmap_size=0; PRAGMA max_page_count=16384;");
        Transaction transaction(impl_->db);
        if (current < 1) exec(impl_->db, migration1);
        if (current < 2) exec(impl_->db, migration2);
        if (current < 3) exec(impl_->db, migration3);
        if (current < 4) exec(impl_->db, migration4);
        if (current < 5) exec(impl_->db, migration5);
        transaction.commit();
        impl_->ready = integrity();
    } catch (const DbError& e) { impl_->ready = db_status(e); }
}
SqliteStore::~SqliteStore() = default;
std::string_view SqliteStore::version() { return sqlite3_libversion(); }
Status SqliteStore::configure_heap(void* memory,std::size_t size) {
    if (!memory || size>static_cast<std::size_t>(std::numeric_limits<int>::max())) return Status::fail(Error::invalid,"invalid SQLite heap");
    return sqlite3_config(SQLITE_CONFIG_HEAP,memory,static_cast<int>(size),32)==SQLITE_OK ? Status{} : Status::fail(Error::busy,"SQLite heap must be configured before initialization");
}
Status SqliteStore::shutdown() { return sqlite3_shutdown()==SQLITE_OK ? Status{} : Status::fail(Error::busy,"SQLite connections still active"); }
Status SqliteStore::integrity() {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "simulated MicroSD fault");
    try {
        Statement query(impl_->db, "PRAGMA quick_check(1)");
        if (query.row() && query.text(0) == "ok") return {};
        return Status::fail(Error::corrupt, "database integrity check failed");
    } catch (const DbError& e) { return db_status(e); }
}
Status SqliteStore::trim() {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io,"storage unavailable");
    return sqlite3_db_release_memory(impl_->db)==SQLITE_OK ? Status{} : Status::fail(Error::io,"SQLite cache release failed");
}
Status SqliteStore::load(CreatureState& state, Settings& settings) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "simulated MicroSD absent");
    try {
        Statement q(impl_->db, "SELECT c.id,c.name,c.born_at,c.last_wall,c.age_seconds,c.pending_ms,c.revision,c.sleeping,"
            "n.hunger,n.energy,n.health,n.happiness,n.affection,n.boredom,n.stress,n.curiosity,p.curiosity,p.sociability,p.resilience "
            "FROM creature c LEFT JOIN needs n ON c.id=n.creature_id LEFT JOIN personality p ON c.id=p.creature_id WHERE c.slot=1");
        if (!q.row()) return Status::fail(Error::unavailable, "no creature in database");
        if (q.is_null(8) || q.is_null(16)) return Status::fail(Error::corrupt, "missing creature needs or personality");
        CreatureState s;
        if (!s.id.assign(q.text(0)) || !s.name.assign(q.text(1))) return Status::fail(Error::corrupt, "identity length invalid");
        s.born_at = q.integer(2); s.last_wall = q.integer(3); s.age_seconds = q.integer(4); s.pending_ms = q.integer(5);
        s.revision = static_cast<std::uint64_t>(q.integer(6)); s.sleeping = q.integer(7) != 0;
        s.needs = {q.real(8), q.real(9), q.real(10), q.real(11), q.real(12), q.real(13), q.real(14), q.real(15)};
        s.personality = {q.real(16), q.real(17), q.real(18)};
        if (!valid(s)) return Status::fail(Error::corrupt, "invalid persisted creature values");
        Settings loaded;
        Statement config(impl_->db, "SELECT key,value FROM settings LIMIT 16");
        while (config.row()) {
            if (config.text(0) == "brightness") loaded.brightness = static_cast<int>(std::clamp<std::int64_t>(config.integer(1), 10, 100));
            else if (config.text(0) == "cognition") loaded.cognition = config.integer(1) != 0;
        }
        state = s; settings = loaded; return {};
    } catch (const DbError& e) { return db_status(e); }
}
Status SqliteStore::save(const CreatureState& s, const Settings& settings, const Memory* memory) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "simulated MicroSD write failure");
    if (!valid(s) || settings.brightness < 10 || settings.brightness > 100) return Status::fail(Error::invalid, "invalid state snapshot");
    if (memory && (memory->content.view().empty() || !std::isfinite(memory->importance) || memory->importance < 0 || memory->importance > 1 ||
                   !std::isfinite(memory->valence) || memory->valence < -1 || memory->valence > 1)) return Status::fail(Error::invalid, "invalid memory proposal");
    try {
        Transaction transaction(impl_->db);
        Statement c(impl_->db, "INSERT INTO creature VALUES(?1,1,?2,?3,?4,?5,?6,?7,?8) ON CONFLICT(id) DO UPDATE SET "
            "name=excluded.name,born_at=excluded.born_at,last_wall=excluded.last_wall,age_seconds=excluded.age_seconds,"
            "pending_ms=excluded.pending_ms,revision=excluded.revision,sleeping=excluded.sleeping");
        c.text(1, s.id.view()); c.text(2, s.name.view()); c.integer(3,s.born_at); c.integer(4,s.last_wall);
        c.integer(5,s.age_seconds); c.integer(6,s.pending_ms); c.integer(7,static_cast<std::int64_t>(s.revision)); c.integer(8,s.sleeping); c.done();
        const auto& n = s.needs;
        Statement needs(impl_->db, "INSERT INTO needs VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) ON CONFLICT(creature_id) DO UPDATE SET "
            "hunger=excluded.hunger,energy=excluded.energy,health=excluded.health,happiness=excluded.happiness,affection=excluded.affection,"
            "boredom=excluded.boredom,stress=excluded.stress,curiosity=excluded.curiosity");
        needs.text(1,s.id.view()); needs.real(2,n.hunger); needs.real(3,n.energy); needs.real(4,n.health); needs.real(5,n.happiness);
        needs.real(6,n.affection); needs.real(7,n.boredom); needs.real(8,n.stress); needs.real(9,n.curiosity); needs.done();
        Statement p(impl_->db, "INSERT INTO personality VALUES(?1,?2,?3,?4) ON CONFLICT(creature_id) DO UPDATE SET "
            "curiosity=excluded.curiosity,sociability=excluded.sociability,resilience=excluded.resilience");
        p.text(1,s.id.view()); p.real(2,s.personality.curiosity); p.real(3,s.personality.sociability); p.real(4,s.personality.resilience); p.done();
        Statement brightness(impl_->db, "INSERT INTO settings VALUES('brightness',?1) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        brightness.integer(1,settings.brightness); brightness.done();
        Statement cognition(impl_->db, "INSERT INTO settings VALUES('cognition',?1) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        cognition.integer(1,settings.cognition); cognition.done();
        if (memory) {
            Statement m(impl_->db, "INSERT INTO memories(creature_id,timestamp,category,content,provenance,importance,valence,tags,entities,summary,actor_id) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
            m.text(1,s.id.view()); m.integer(2,memory->timestamp); m.text(3,memory->category.view()); m.text(4,memory->content.view());
            m.text(5,memory->provenance.view()); m.real(6,memory->importance); m.real(7,memory->valence); m.text(8,memory->tags.view());
            m.text(9,memory->entities.view()); m.text(10,memory->summary.view()); m.text(11,memory->actor_id.view()); m.done();
        }
        transaction.commit(); return {};
    } catch (const DbError& e) { return db_status(e); }
}
Status SqliteStore::memories(std::string_view query, std::size_t offset, MemoryPage& page) {
    page.size = 0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "simulated MicroSD read failure");
    if (query.size() > 96 || offset > 10000) return Status::fail(Error::invalid, "memory query budget exceeded");
    try {
        QueryBudget budget(impl_->db);
        Statement q(impl_->db, "SELECT id,timestamp,category,content,provenance,importance,valence,recall_count,last_recall,tags,entities,summary,actor_id "
            "FROM memories WHERE ?1='' OR instr(lower(content),lower(?1))>0 OR instr(lower(tags),lower(?1))>0 OR instr(lower(entities),lower(?1))>0 "
            "ORDER BY (importance*0.6 + 0.4/(1.0 + max(0,?2-timestamp)/86400.0)) DESC,timestamp DESC,id DESC LIMIT 8 OFFSET ?3");
        q.text(1,query); q.integer(2,0); // Rank relative to latest local memory, independent of host wall clock.
        // Re-prepare time anchor through the current creature timestamp.
        Statement now(impl_->db, "SELECT last_wall FROM creature WHERE slot=1");
        if (now.row()) q.integer(2,now.integer(0));
        q.integer(3,static_cast<std::int64_t>(offset));
        while (q.row()) {
            auto& m = page.items[page.size]; m = {};
            m.id=q.integer(0); m.timestamp=q.integer(1);
            if (!m.category.assign(q.text(2)) || !m.content.assign(q.text(3)) || !m.provenance.assign(q.text(4)) ||
                !m.actor_id.assign(q.text(12)) || !m.tags.assign(q.text(9)) || !m.entities.assign(q.text(10)) || !m.summary.assign(q.text(11))) {
                page.size=0; return Status::fail(Error::corrupt, "persisted memory exceeds limits");
            }
            m.importance=q.real(5); m.valence=q.real(6); m.recall_count=static_cast<std::uint32_t>(q.integer(7)); m.last_recall=q.integer(8);
            ++page.size;
        }
        return {};
    } catch (const DbError& e) { page.size=0; return db_status(e); }
}
Status SqliteStore::create_entry(AssistantEntry& e) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "assistant storage unavailable");
    if (e.id || e.content.view().empty() || e.created_at < 0 || e.done ||
        (e.kind == EntryKind::note ? e.due_at != 0 : e.kind != EntryKind::reminder || e.due_at <= e.created_at))
        return Status::fail(Error::invalid, "invalid assistant entry");
    try {
        Transaction tx(impl_->db); const auto id=insert_entry(impl_->db,e); tx.commit(); e.id=id; return {};
    } catch (const DbError& error) { return db_status(error); }
}
Status SqliteStore::entries(EntryKind kind, std::string_view query, std::size_t offset, AssistantPage& page) {
    page.size=0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "assistant storage unavailable");
    if ((kind != EntryKind::note && kind != EntryKind::reminder) || query.size()>96 || query.find('\0')!=std::string_view::npos || offset>budget::assistant_entries)
        return Status::fail(Error::invalid, "assistant query budget");
    try {
        Statement q(impl_->db, "SELECT id,kind,created_at,due_at,content,done,actor_id FROM assistant_entries WHERE kind=?1 AND (?2='' OR instr(lower(content),lower(?2))>0) ORDER BY id DESC LIMIT 4 OFFSET ?3");
        q.integer(1,static_cast<std::int64_t>(kind)); q.text(2,query); q.integer(3,static_cast<std::int64_t>(offset));
        while (q.row()) { if (auto s=read_entry(q,page.items[page.size]); !s) { page.size=0; return s; } ++page.size; }
        return {};
    } catch (const DbError& error) { page.size=0; return db_status(error); }
}
Status SqliteStore::change_entry(std::int64_t id, EntryChange change, std::int64_t due_at) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "assistant storage unavailable");
    if (id<=0 || (change != EntryChange::erase && change != EntryChange::complete && change != EntryChange::reschedule) ||
        (change == EntryChange::reschedule && due_at<=0)) return Status::fail(Error::invalid, "invalid entry change");
    try {
        Statement q(impl_->db, change == EntryChange::erase ? "DELETE FROM assistant_entries WHERE id=?1" :
            change == EntryChange::complete ? "UPDATE assistant_entries SET done=1 WHERE id=?1 AND kind=1 AND done=0" :
            "UPDATE assistant_entries SET due_at=?2 WHERE id=?1 AND kind=1 AND done=0 AND ?2>created_at");
        q.integer(1,id); if (change == EntryChange::reschedule) q.integer(2,due_at); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::unavailable, "entry missing or already completed");
    } catch (const DbError& error) { return db_status(error); }
}
Status SqliteStore::next_reminder(AssistantEntry& entry) {
    entry={};
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "assistant storage unavailable");
    try {
        Statement q(impl_->db, "SELECT id,kind,created_at,due_at,content,done,actor_id FROM assistant_entries WHERE kind=1 AND done=0 ORDER BY due_at,id LIMIT 1");
        return q.row() ? read_entry(q,entry) : Status{};
    } catch (const DbError& error) { return db_status(error); }
}
Status SqliteStore::tool_job(std::string_view id, ToolJob& job) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "tool storage unavailable");
    if (id.empty() || id.size()>=40) return Status::fail(Error::invalid, "invalid tool id");
    try {
        Statement q(impl_->db, "SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs WHERE id=?1"); q.text(1,id);
        return q.row() ? read_tool(q,job) : Status::fail(Error::unavailable, "unknown tool id");
    } catch (const DbError& error) { return db_status(error); }
}
Status SqliteStore::next_tool_time(std::int64_t& at) {
    at=-1;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io,"tool storage unavailable");
    try {
        Statement q(impl_->db,"SELECT min(CASE WHEN state=0 THEN execute_at WHEN state=2 THEN 0 ELSE expires_at END) FROM tool_jobs WHERE state IN (0,1,2)");
        if(q.row() && !q.is_null(0)) at=q.integer(0); // -1 means no pending work; 0 means immediate.
        return {};
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::tool_results(ToolPage& page) {
    page.size=0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io,"tool storage unavailable");
    try {
        Statement q(impl_->db,"SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs ORDER BY updated_serial DESC,rowid DESC LIMIT 2");
        while(q.row()) { if(auto s=read_tool(q,page.items[page.size]); !s) { page.size=0; return s; } ++page.size; }
        return {};
    } catch(const DbError& error) { page.size=0; return db_status(error); }
}
Status SqliteStore::enqueue_tools(std::span<const ToolCall> calls, bool confirm_messages, ToolMask grants) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "tool storage unavailable");
    if (calls.empty() || calls.size()>budget::tool_calls || (grants & ~all_tools)) return Status::fail(Error::invalid, "tool call budget or grant mask");
    for (std::size_t i=0;i<calls.size();++i) {
        if (auto s=validate_tool(calls[i]); !s) return s;
        for (std::size_t j=0;j<i;++j) if (calls[i].id==calls[j].id) return Status::fail(Error::invalid, "duplicate tool id in batch");
    }
    try {
        Transaction tx(impl_->db);
        for(const auto& c:calls) if(auto status=insert_tool(impl_->db,c,confirm_messages,grants); !status) return status;
        tx.commit(); return {};
    } catch (const DbError& error) { return db_status(error); }
}
Status SqliteStore::tool_jobs(std::int64_t now, ToolPage& page) {
    page.size=0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "tool storage unavailable");
    try {
        Statement q(impl_->db,"SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs WHERE state=2 OR (state=0 AND execute_at<=?1) OR (state=1 AND expires_at>0 AND expires_at<=?1) ORDER BY (state=2) DESC,execute_at,id LIMIT 2");
        q.integer(1,now);
        while(q.row()) { if (auto s=read_tool(q,page.items[page.size]); !s) { page.size=0; return s; } ++page.size; }
        return {};
    } catch (const DbError& error) { page.size=0; return db_status(error); }
}
Status SqliteStore::update_tool(std::string_view id, ToolState expected, ToolState next, std::string_view result) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "tool storage unavailable");
    const bool transition = expected==ToolState::queued ? (next==ToolState::dispatched || next==ToolState::failed || next==ToolState::cancelled) :
        expected==ToolState::needs_confirmation ? (next==ToolState::queued || next==ToolState::failed || next==ToolState::cancelled) :
        expected==ToolState::dispatched ? (next==ToolState::succeeded || next==ToolState::failed || next==ToolState::uncertain) :
        expected==ToolState::uncertain && (next==ToolState::succeeded || next==ToolState::failed);
    if (!transition || result.size()>=192 || result.find('\0')!=std::string_view::npos) return Status::fail(Error::invalid,"invalid tool transition/result");
    try {
        Statement q(impl_->db,"UPDATE tool_jobs SET state=?3,result=?4,updated_serial=(SELECT coalesce(max(updated_serial),0)+1 FROM tool_jobs) WHERE id=?1 AND state=?2");
        q.text(1,id); q.integer(2,static_cast<std::int64_t>(expected)); q.integer(3,static_cast<std::int64_t>(next)); q.text(4,result); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::busy,"tool state changed or missing");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::execute_local_tool(std::string_view id, std::int64_t now) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io,"tool storage unavailable");
    try {
        Transaction tx(impl_->db); ToolJob job;
        if (auto s=tool_job(id,job); !s) return s;
        if (job.state!=ToolState::queued || phone_tool(job.call.name) || now<0 || job.call.execute_at>now ||
            (job.call.expires_at && job.call.expires_at<=now))
            return Status::fail(Error::denied,"local tool is not eligible");
        if(job.call.name==ToolName::schedule_agent_task) {
            AgentTaskRequest request; request.id=job.call.id; request.instruction.assign(job.call.text.view());
            request.actor_id=job.actor_id; request.execute_at=job.call.at; request.expires_at=job.call.expires_at; request.grants=job.grants & ~tool_bit(ToolName::schedule_agent_task);
            if(auto status=insert_task(impl_->db,request); !status) return status;
        } else {
            AssistantEntry e; e.actor_id=job.actor_id; e.created_at=now; e.content=job.call.text;
            e.kind=job.call.name==ToolName::save_note ? EntryKind::note : EntryKind::reminder; e.due_at=job.call.at;
            (void)insert_entry(impl_->db,e);
        }
        Statement q(impl_->db,"UPDATE tool_jobs SET state=3,result='local entry committed',updated_serial=(SELECT coalesce(max(updated_serial),0)+1 FROM tool_jobs) WHERE id=?1 AND state=0"); q.text(1,id); q.done();
        tx.commit(); return {};
    } catch (const DbError& error) { return db_status(error); }
}
Status SqliteStore::finish_tool(const ToolResult& result, ToolState expected) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"tool storage unavailable");
    if(expected!=ToolState::dispatched && expected!=ToolState::uncertain) return Status::fail(Error::denied,"tool not dispatched");
    if(auto status=validate_service_result(result); !status) return status;
    try {
        Statement q(impl_->db,"UPDATE tool_jobs SET state=?3,result=?4,data=?5,source_count=?6,source1_title=?7,source1_url=?8,source2_title=?9,source2_url=?10,error=?11,updated_serial=(SELECT coalesce(max(updated_serial),0)+1 FROM tool_jobs) WHERE id=?1 AND state=?2");
        q.text(1,result.id.view()); q.integer(2,static_cast<std::int64_t>(expected)); q.integer(3,static_cast<std::int64_t>(result.success ? ToolState::succeeded : ToolState::failed));
        q.text(4,result.detail.view()); q.text(5,result.data.view()); q.integer(6,static_cast<std::int64_t>(result.source_count));
        q.text(7,result.source_count>0 ? result.sources[0].title.view() : std::string_view{});
        q.text(8,result.source_count>0 ? result.sources[0].url.view() : std::string_view{});
        q.text(9,result.source_count>1 ? result.sources[1].title.view() : std::string_view{});
        q.text(10,result.source_count>1 ? result.sources[1].url.view() : std::string_view{});
        q.integer(11,static_cast<std::int64_t>(result.error)); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::busy,"tool state changed before result commit");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::schedule_agent_task(const AgentTaskRequest& request) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Transaction tx(impl_->db); if(auto status=insert_task(impl_->db,request); !status) return status;
        tx.commit(); return {};
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::agent_task(std::string_view id, AgentTask& task) {
    task={};
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    if(!valid_identifier(id) || id.size()>=40) return Status::fail(Error::invalid,"invalid task id");
    try {
        Statement q(impl_->db,"SELECT serial,id,instruction,execute_at,expires_at,grants,wake,state,turn,attempts,speech,notice,actor_id FROM agent_tasks WHERE id=?1"); q.text(1,id);
        return q.row() ? read_task(q,task) : Status::fail(Error::unavailable,"no agent task");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::agent_tasks(std::size_t offset, AgentTaskPage& page) {
    page.size=0;
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    if(offset>budget::agent_tasks) return Status::fail(Error::invalid,"task page offset exceeds budget");
    try {
        Statement q(impl_->db,"SELECT serial,id,instruction,execute_at,expires_at,grants,wake,state,turn,attempts,speech,notice,actor_id FROM agent_tasks ORDER BY serial DESC LIMIT 4 OFFSET ?1");
        q.integer(1,static_cast<std::int64_t>(offset));
        while(q.row()) { if(auto status=read_task(q,page.items[page.size]); !status) { page.size=0; return status; } ++page.size; }
        return {};
    } catch(const DbError& error) { page.size=0; return db_status(error); }
}
Status SqliteStore::ready_agent_task(std::int64_t now, AgentTask& task) {
    task={};
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"SELECT serial,id,instruction,execute_at,expires_at,grants,wake,state,turn,attempts,speech,notice,actor_id FROM agent_tasks a WHERE state IN (0,2) AND (expires_at<=?1 OR (execute_at<=?1 AND NOT EXISTS(SELECT 1 FROM tool_jobs j WHERE j.task_id=a.id AND j.state IN (0,1,2)))) ORDER BY expires_at,execute_at,serial LIMIT 1"); q.integer(1,now);
        return q.row() ? read_task(q,task) : Status::fail(Error::unavailable,"no ready agent task");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::begin_agent_turn(std::string_view id, std::int64_t now) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"UPDATE agent_tasks SET state=1,attempts=attempts+1 WHERE id=?1 AND state IN (0,2) AND execute_at<=?2 AND expires_at>?2 AND turn<4 AND attempts<8 AND NOT EXISTS(SELECT 1 FROM tool_jobs j WHERE j.task_id=agent_tasks.id AND j.state IN (0,1,2))");
        q.text(1,id); q.integer(2,now); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::busy,"task not ready or budget exhausted");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::commit_agent_turn(std::string_view id, unsigned turn, std::span<const ToolCall> calls, std::string_view speech, std::int64_t now) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    if(calls.size()>budget::tool_calls || speech.size()>=512 || speech.find('\0')!=std::string_view::npos ||
        (calls.empty() && speech.empty()) || (!calls.empty() && turn+1>=budget::agent_turns)) return Status::fail(Error::invalid,"agent turn budget");
    try {
        Transaction tx(impl_->db); AgentTask task;
        if(auto status=agent_task(id,task); !status) return status;
        if(task.state!=AgentTaskState::inferring || task.turn!=turn || task.request.expires_at<=now)
            return Status::fail(Error::busy,"agent turn changed or expired");
        for(const auto& c:calls) {
            if(!(task.request.grants & tool_bit(c.name)) || c.name==ToolName::schedule_agent_task ||
                c.execute_at>=task.request.expires_at || !c.expires_at || c.expires_at>task.request.expires_at)
                return Status::fail(Error::denied,"tool exceeds task capabilities or deadline");
            if(auto status=insert_tool(impl_->db,c,true,task.request.grants,id,turn+1,task.request.actor_id.view()); !status) return status;
            if(external_effect(c.name)) {
                // The same effect in this task is a result lookup, even if the model invents a new ID.
                Statement prior(impl_->db,"SELECT id,state FROM tool_jobs WHERE task_id=?1 AND id<>?2 AND name=?3 AND content=?4 AND recipient=?5 AND binding=?6 AND subject=?7 AND reference=?8 AND at=?9 AND execute_at=?10 ORDER BY rowid LIMIT 1");
                prior.text(1,id); prior.text(2,c.id.view()); prior.integer(3,static_cast<std::int64_t>(c.name)); prior.text(4,c.text.view());
                prior.text(5,c.recipient.view()); prior.text(6,c.binding.view()); prior.text(7,c.subject.view()); prior.text(8,c.reference.view()); prior.integer(9,c.at); prior.integer(10,c.execute_at);
                if(prior.row()) {
                    if(prior.integer(1)<=2) {
                        Statement duplicate(impl_->db,"UPDATE tool_jobs SET state=6,result='duplicate action already pending in this task' WHERE id=?1"); duplicate.text(1,c.id.view()); duplicate.done();
                    } else {
                        Statement copy(impl_->db,"UPDATE tool_jobs SET (state,result,data,source_count,source1_title,source1_url,source2_title,source2_url,error)=(SELECT state,result,data,source_count,source1_title,source1_url,source2_title,source2_url,error FROM tool_jobs WHERE id=?2) WHERE id=?1");
                        copy.text(1,c.id.view()); copy.text(2,prior.text(0)); copy.done();
                    }
                }
            }
        }
        Statement update(impl_->db,"UPDATE agent_tasks SET state=?3,turn=turn+1,speech=?4,notice=?5 WHERE id=?1 AND state=1 AND turn=?2");
        update.text(1,id); update.integer(2,turn); update.integer(3,static_cast<std::int64_t>(calls.empty() ? AgentTaskState::completed : AgentTaskState::waiting_tools));
        update.text(4,calls.empty() ? speech : std::string_view{}); update.integer(5,calls.empty()); update.done();
        if(sqlite3_changes(impl_->db)!=1) return Status::fail(Error::busy,"agent turn changed during commit");
        tx.commit(); return {};
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::retry_agent_task(std::string_view id) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"UPDATE agent_tasks SET state=0 WHERE id=?1 AND state=1"); q.text(1,id); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::busy,"task not inferring");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::stop_agent_task(std::string_view id, AgentTaskState state, std::string_view speech) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    if((state!=AgentTaskState::failed && state!=AgentTaskState::cancelled) || speech.empty() || speech.size()>=512 || speech.find('\0')!=std::string_view::npos)
        return Status::fail(Error::invalid,"invalid task stop state");
    try {
        Transaction tx(impl_->db);
        Statement q(impl_->db,"UPDATE agent_tasks SET state=?2,speech=?3,notice=?4 WHERE id=?1 AND state IN (0,1,2)");
        q.text(1,id); q.integer(2,static_cast<std::int64_t>(state)); q.text(3,speech); q.integer(4,state==AgentTaskState::failed); q.done();
        if(sqlite3_changes(impl_->db)!=1) return Status::fail(Error::busy,"task missing or already terminal");
        Statement jobs(impl_->db,"UPDATE tool_jobs SET state=CASE WHEN state=2 THEN 5 ELSE 6 END,result='parent task stopped',updated_serial=(SELECT coalesce(max(updated_serial),0)+1 FROM tool_jobs) WHERE task_id=?1 AND state IN (0,1,2)"); jobs.text(1,id); jobs.done();
        tx.commit(); return {};
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::recover_agent_tasks() {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try { exec(impl_->db,"UPDATE agent_tasks SET state=0 WHERE state=1"); return {}; }
    catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::agent_tool_results(std::string_view id, unsigned turn, ToolPage& page) {
    page.size=0;
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs WHERE task_id=?1 AND task_turn=?2 ORDER BY rowid LIMIT 2");
        q.text(1,id); q.integer(2,turn);
        while(q.row()) { if(auto status=read_tool(q,page.items[page.size]); !status) { page.size=0; return status; } ++page.size; }
        return {};
    } catch(const DbError& error) { page.size=0; return db_status(error); }
}
Status SqliteStore::agent_notice(AgentTask& task) {
    task={};
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"SELECT serial,id,instruction,execute_at,expires_at,grants,wake,state,turn,attempts,speech,notice,actor_id FROM agent_tasks WHERE notice=1 AND state IN (3,4) ORDER BY serial LIMIT 1");
        return q.row() ? read_task(q,task) : Status::fail(Error::unavailable,"no pending task result");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::acknowledge_agent_notice(std::string_view id) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"UPDATE agent_tasks SET notice=0 WHERE id=?1 AND notice=1 AND state IN (3,4)"); q.text(1,id); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::unavailable,"no pending task notice");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::tool_confirmation(ToolJob& job) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"tool storage unavailable");
    try {
        Statement q(impl_->db,"SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs WHERE state=1 ORDER BY rowid LIMIT 1");
        return q.row() ? read_tool(q,job) : Status::fail(Error::unavailable,"no pending confirmation");
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::next_agent_deadline(std::int64_t& at) {
    at=-1;
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"task storage unavailable");
    try {
        Statement q(impl_->db,"SELECT min(CASE WHEN state=0 THEN execute_at ELSE expires_at END) FROM agent_tasks WHERE state IN (0,1,2)");
        if(q.row() && !q.is_null(0)) at=q.integer(0);
        return {};
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::backup(std::string_view destination) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "storage unavailable for backup");
    if (destination.empty() || destination.size() > 1024 || destination.find('\0') != std::string_view::npos) return Status::fail(Error::invalid, "invalid backup path");
    std::error_code error;
    const std::filesystem::path path{destination};
    if (std::filesystem::exists(path, error)) return Status::fail(Error::denied, "backup destination already exists");
    if (error || std::filesystem::absolute(path, error).lexically_normal() == std::filesystem::absolute(impl_->path, error).lexically_normal())
        return Status::fail(Error::invalid, "backup destination aliases live database");
    // VACUUM INTO is SQLite's consistent export API; it never raw-copies a live file.
    try {
        Statement export_db(impl_->db, "VACUUM INTO ?1"); export_db.text(1,destination); export_db.done(); return {};
    } catch (const DbError& e) { return db_status(e); }
}
namespace {
constexpr const char* profile_query=R"SQL(SELECT p.serial,p.id,p.name,p.role,p.enabled,p.name_confirmed,p.grants,p.voice_grants,p.revision,
 p.binding1,p.binding2,p.binding3,p.binding4,
 coalesce(min(e.timestamp),0),coalesce(max(e.timestamp),0),count(e.serial),
 count(DISTINCT CAST(e.timestamp/86400 AS TEXT)||':'||e.context_id),count(DISTINCT e.context_id)
 FROM voice_profiles p LEFT JOIN voice_encounters e ON e.profile_id=p.id
 WHERE (?1='' OR p.id=?1) AND (?2<0 OR p.role=?2)
 GROUP BY p.serial ORDER BY p.serial LIMIT 4 OFFSET ?3)SQL";
Status read_profile(Statement& q,VoiceProfile& p) {
    p={}; const auto role=q.integer(3), grants=q.integer(6), voice=q.integer(7), revision=q.integer(8);
    if(!p.id.assign(q.text(1)) || !p.name.assign(q.text(2)) || role<0 || role>2 || grants<0 || grants>all_tools || voice<0 || voice>all_tools ||
        revision<=0 || revision>1'000'000'000 || q.integer(4)<0 || q.integer(4)>1 || q.integer(5)<0 || q.integer(5)>1)
        return Status::fail(Error::corrupt,"invalid voice profile");
    p.serial=q.integer(0); p.role=static_cast<UserRole>(role); p.enabled=q.integer(4)!=0; p.name_confirmed=q.integer(5)!=0;
    p.grants=static_cast<ToolMask>(grants); p.voice_grants=static_cast<ToolMask>(voice); p.revision=static_cast<std::uint64_t>(revision);
    for(int i=0;i<4;++i) if(!q.text(9+i).empty()) {
        if(!p.bindings[p.binding_count++].assign(q.text(9+i))) return Status::fail(Error::corrupt,"identity binding budget");
    }
    p.first_seen=q.integer(13); p.last_seen=q.integer(14); p.observations=static_cast<std::size_t>(q.integer(15));
    p.visits=static_cast<std::size_t>(q.integer(16)); p.contexts=static_cast<std::size_t>(q.integer(17));
    return validate_voice_profile(p) ? Status{} : Status::fail(Error::corrupt,"invalid persisted identity policy");
}
void bind_profile(Statement& q,const VoiceProfile& p) {
    q.text(1,p.id.view()); q.text(2,p.name.view()); q.integer(3,static_cast<std::int64_t>(p.role)); q.integer(4,p.enabled);
    q.integer(5,p.name_confirmed); q.integer(6,p.grants); q.integer(7,p.voice_grants); q.integer(8,static_cast<std::int64_t>(p.revision));
    for(std::size_t i=0;i<4;++i) q.text(9+static_cast<int>(i),i<p.binding_count ? p.bindings[i].view() : std::string_view{});
}
Status insert_profile(sqlite3* db,const VoiceProfile& p) {
    if(auto s=validate_voice_profile(p); !s) return s;
    Statement count(db,"SELECT count(*) FROM voice_profiles"); count.row();
    if(count.integer(0)>=static_cast<std::int64_t>(budget::voice_profiles)) return Status::fail(Error::full,"voice directory full");
    Statement q(db,"INSERT INTO voice_profiles(id,name,role,enabled,name_confirmed,grants,voice_grants,revision,binding1,binding2,binding3,binding4) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
    bind_profile(q,p); q.done(); return {};
}
Status insert_template(sqlite3* db,const VoiceTemplate& t) {
    if(!valid_identifier(t.profile_id.view()) || !valid_identifier(t.encoder_id.view()) || !valid_identifier(t.sealed.scheme.view()) ||
        t.slot>=budget::voice_templates || !t.sealed.size || t.sealed.size>t.sealed.bytes.size()) return Status::fail(Error::invalid,"invalid sealed voice template");
    Statement q(db,"INSERT INTO voice_templates(profile_id,slot,encoder,scheme,sealed) VALUES(?1,?2,?3,?4,?5) ON CONFLICT(profile_id,slot) DO UPDATE SET encoder=excluded.encoder,scheme=excluded.scheme,sealed=excluded.sealed");
    q.text(1,t.profile_id.view()); q.integer(2,t.slot); q.text(3,t.encoder_id.view()); q.text(4,t.sealed.scheme.view());
    q.blob(5,{t.sealed.bytes.data(),t.sealed.size}); q.done(); return {};
}
}
Status SqliteStore::identity_profile(std::string_view id,VoiceProfile& p) {
    Text<40> key; if(!key.assign(id)) return Status::fail(Error::invalid,"identity id budget");
    id=key.view(); p={}; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(!valid_identifier(id)) return Status::fail(Error::invalid,"invalid identity id");
    try {
        Statement q(impl_->db,profile_query); q.text(1,id); q.integer(2,-1); q.integer(3,0);
        return q.row() ? read_profile(q,p) : Status::fail(Error::unavailable,"unknown identity");
    } catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_owner(VoiceProfile& p) {
    p={}; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    try {
        Statement q(impl_->db,profile_query); q.text(1,{}); q.integer(2,0); q.integer(3,0); return q.row() ? read_profile(q,p) : Status::fail(Error::unavailable,"owner not initialized");
    } catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_profiles(std::size_t offset,VoiceProfilePage& page) {
    page={}; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(offset>budget::voice_profiles) return Status::fail(Error::invalid,"voice page budget");
    try {
        Statement q(impl_->db,profile_query); q.text(1,{}); q.integer(2,-1); q.integer(3,static_cast<std::int64_t>(offset));
        while(q.row()) { if(auto s=read_profile(q,page.items[page.size]); !s) { page={}; return s; } ++page.size; } return {};
    } catch(const DbError& e) { page={}; return db_status(e); }
}
Status SqliteStore::identity_bootstrap(const VoiceProfile& p) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(p.role!=UserRole::owner || !p.enabled || !p.name_confirmed) return Status::fail(Error::denied,"invalid bootstrap owner");
    try {
        Transaction tx(impl_->db); Statement count(impl_->db,"SELECT count(*) FROM voice_profiles WHERE role=0"); count.row();
        if(count.integer(0)) return Status::fail(Error::denied,"owner already exists");
        if(auto s=insert_profile(impl_->db,p); !s) return s;
        for(const auto* sql:{"UPDATE memories SET actor_id=?1 WHERE actor_id=''", "UPDATE assistant_entries SET actor_id=?1 WHERE actor_id=''",
                             "UPDATE tool_jobs SET actor_id=?1 WHERE actor_id=''", "UPDATE agent_tasks SET actor_id=?1 WHERE actor_id=''"}) {
            Statement q(impl_->db,sql); q.text(1,p.id.view()); q.done();
        }
        tx.commit(); return {};
    } catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_update(const VoiceProfile& p,std::uint64_t expected) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(auto s=validate_voice_profile(p); !s) return s;
    try {
        Transaction tx(impl_->db);
        if(!expected) { if(p.role==UserRole::owner) return Status::fail(Error::denied,"use explicit owner bootstrap"); if(auto s=insert_profile(impl_->db,p); !s) return s; }
        else {
            Statement q(impl_->db,"UPDATE voice_profiles SET name=?2,role=?3,enabled=?4,name_confirmed=?5,grants=?6,voice_grants=?7,revision=?8,binding1=?9,binding2=?10,binding3=?11,binding4=?12 WHERE id=?1 AND revision=?13 AND (role<>0 OR (?3=0 AND ?4=1))");
            bind_profile(q,p); q.integer(13,static_cast<std::int64_t>(expected)); q.done();
            if(sqlite3_changes(impl_->db)!=1) return Status::fail(Error::busy,"profile changed or owner protected");
        }
        tx.commit(); return {};
    } catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_enroll(const VoiceTemplate& t) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    try {
        Transaction tx(impl_->db); if(auto s=insert_template(impl_->db,t); !s) return s;
        Statement q(impl_->db,"UPDATE voice_profiles SET revision=revision+1 WHERE id=?1"); q.text(1,t.profile_id.view()); q.done();
        tx.commit(); return {};
    } catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_template(std::string_view id,unsigned slot,VoiceTemplate& t) {
    t={}; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    try {
        Statement q(impl_->db,"SELECT encoder,scheme,sealed FROM voice_templates WHERE profile_id=?1 AND slot=?2"); q.text(1,id); q.integer(2,slot);
        if(!q.row()) return Status::fail(Error::unavailable,"no voice template");
        const auto bytes=q.blob(2);
        if(!t.profile_id.assign(id) || !t.encoder_id.assign(q.text(0)) || !t.sealed.scheme.assign(q.text(1)) || bytes.empty() || bytes.size()>t.sealed.bytes.size())
            return Status::fail(Error::corrupt,"invalid sealed voice template");
        t.slot=slot; t.sealed.size=bytes.size(); std::copy(bytes.begin(),bytes.end(),t.sealed.bytes.begin()); return {};
    } catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_observe(const VoiceEncounter& e,const VoiceProfile* guest,const VoiceTemplate* t) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(!valid_identifier(e.profile_id.view()) || !valid_identifier(e.capture_id.view()) || !valid_identifier(e.context_id.view()) ||
        e.timestamp<0 || !std::isfinite(e.similarity) || e.similarity < -1 || e.similarity>1 ||
        ((guest==nullptr)!=(t==nullptr)) || (guest && (guest->role!=UserRole::guest || guest->id!=e.profile_id || t->profile_id!=e.profile_id)))
        return Status::fail(Error::invalid,"invalid voice encounter");
    try {
        Transaction tx(impl_->db); Statement count(impl_->db,"SELECT count(*) FROM voice_encounters"); count.row();
        if(count.integer(0)>=2048) return Status::fail(Error::full,"voice encounter history full; explicit archival required");
        if(guest) { if(auto s=insert_profile(impl_->db,*guest); !s) return s; if(auto s=insert_template(impl_->db,*t); !s) return s; }
        Statement q(impl_->db,"INSERT INTO voice_encounters(profile_id,capture_id,context_id,timestamp,similarity,claimed_name) VALUES(?1,?2,?3,?4,?5,?6)");
        q.text(1,e.profile_id.view()); q.text(2,e.capture_id.view()); q.text(3,e.context_id.view()); q.integer(4,e.timestamp); q.real(5,e.similarity); q.text(6,e.claimed_name.view()); q.done();
        tx.commit(); return {};
    } catch(const DbError& error) { return db_status(error); }
}
Status SqliteStore::identity_encounters(std::string_view id,std::size_t offset,VoiceEncounterPage& page) {
    page={}; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(offset>2048) return Status::fail(Error::invalid,"voice timeline page budget");
    try {
        Statement q(impl_->db,"SELECT profile_id,capture_id,context_id,timestamp,similarity,claimed_name FROM voice_encounters WHERE profile_id=?1 ORDER BY timestamp DESC,serial DESC LIMIT 8 OFFSET ?2");
        q.text(1,id); q.integer(2,static_cast<std::int64_t>(offset));
        while(q.row()) {
            auto& e=page.items[page.size];
            if(!e.profile_id.assign(q.text(0)) || !e.capture_id.assign(q.text(1)) || !e.context_id.assign(q.text(2)) || !e.claimed_name.assign(q.text(5)))
                return Status::fail(Error::corrupt,"voice encounter budget");
            e.timestamp=q.integer(3); e.similarity=static_cast<float>(q.real(4)); ++page.size;
        } return {};
    } catch(const DbError& e) { page={}; return db_status(e); }
}
Status SqliteStore::identity_capture_seen(std::string_view capture,bool& seen) {
    seen=false; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    try { Statement q(impl_->db,"SELECT 1 FROM voice_encounters WHERE capture_id=?1"); q.text(1,capture); seen=q.row(); return {}; }
    catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_set_guest_tracking(bool enabled) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    try { Statement q(impl_->db,"UPDATE settings SET value=?1 WHERE key='voice_guest_tracking'"); q.integer(1,enabled); q.done(); return {}; }
    catch(const DbError& e) { return db_status(e); }
}
Status SqliteStore::identity_guest_tracking(bool& enabled) {
    enabled=false; if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    try {
        Statement q(impl_->db,"SELECT value FROM settings WHERE key='voice_guest_tracking'");
        if(!q.row() || q.integer(0)<0 || q.integer(0)>1) return Status::fail(Error::corrupt,"invalid voice tracking setting");
        enabled=q.integer(0)!=0; return {};
    } catch(const DbError& e) { return db_status(e); }
}

Status SqliteStore::memories_for(std::string_view actor, std::string_view query, std::size_t offset, MemoryPage& page) {
    page.size = 0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "simulated MicroSD read failure");
    if (query.size() > 96 || offset > 10000) return Status::fail(Error::invalid, "memory query budget exceeded");
    try {
        QueryBudget work_budget(impl_->db);
        Statement q(impl_->db, "SELECT id,timestamp,category,content,provenance,importance,valence,recall_count,last_recall,tags,entities,summary,actor_id "
            "FROM memories WHERE actor_id=?4 AND (?1='' OR instr(lower(content),lower(?1))>0 OR instr(lower(tags),lower(?1))>0 OR instr(lower(entities),lower(?1))>0) "
            "ORDER BY (importance*0.6 + 0.4/(1.0 + max(0,?2-timestamp)/86400.0)) DESC,timestamp DESC,id DESC LIMIT 8 OFFSET ?3");
        q.text(4,actor); q.text(1,query); q.integer(2,0); // Rank relative to latest local memory, independent of host wall clock.
        // Re-prepare time anchor through the current creature timestamp.
        Statement now(impl_->db, "SELECT last_wall FROM creature WHERE slot=1");
        if (now.row()) q.integer(2,now.integer(0));
        q.integer(3,static_cast<std::int64_t>(offset));
        while (q.row()) {
            auto& m = page.items[page.size]; m = {};
            m.id=q.integer(0); m.timestamp=q.integer(1);
            if (!m.category.assign(q.text(2)) || !m.content.assign(q.text(3)) || !m.provenance.assign(q.text(4)) ||
                !m.actor_id.assign(q.text(12)) || !m.tags.assign(q.text(9)) || !m.entities.assign(q.text(10)) || !m.summary.assign(q.text(11)))
                return Status::fail(Error::corrupt, "persisted memory exceeds limits");
            m.importance=q.real(5); m.valence=q.real(6); m.recall_count=static_cast<std::uint32_t>(q.integer(7)); m.last_recall=q.integer(8);
            ++page.size;
        }
        return {};
    } catch (const DbError& e) { page={}; return db_status(e); }
}

Status SqliteStore::entries_for(std::string_view actor, EntryKind kind, std::string_view query, std::size_t offset, AssistantPage& page) {
    page.size=0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "assistant storage unavailable");
    if ((kind != EntryKind::note && kind != EntryKind::reminder) || query.size()>96 || query.find('\0')!=std::string_view::npos || offset>budget::assistant_entries)
        return Status::fail(Error::invalid, "assistant query budget");
    try {
        Statement q(impl_->db, "SELECT id,kind,created_at,due_at,content,done,actor_id FROM assistant_entries WHERE actor_id=?4 AND kind=?1 AND (?2='' OR instr(lower(content),lower(?2))>0) ORDER BY id DESC LIMIT 4 OFFSET ?3");
        q.text(4,actor); q.integer(1,static_cast<std::int64_t>(kind)); q.text(2,query); q.integer(3,static_cast<std::int64_t>(offset));
        while (q.row()) { if (auto s=read_entry(q,page.items[page.size]); !s) { page.size=0; return s; } ++page.size; }
        return {};
    } catch (const DbError& error) { page.size=0; return db_status(error); }
}

Status SqliteStore::tool_results_for(std::string_view actor, ToolPage& page) {
    page.size=0;
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io,"tool storage unavailable");
    try {
        Statement q(impl_->db,"SELECT id,name,execute_at,expires_at,at,content,recipient,state,result,binding,subject,reference,data,source_count,source1_title,source1_url,source2_title,source2_url,error,grants,actor_id,approved_by,approval_revision FROM tool_jobs WHERE actor_id=?1 ORDER BY updated_serial DESC,rowid DESC LIMIT 2");
        q.text(1,actor);
        while(q.row()) { if(auto s=read_tool(q,page.items[page.size]); !s) { page.size=0; return s; } ++page.size; }
        return {};
    } catch(const DbError& error) { page.size=0; return db_status(error); }
}

Status SqliteStore::enqueue_for(std::string_view actor, std::span<const ToolCall> calls, ToolMask grants) {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "tool storage unavailable");
    if (calls.empty() || calls.size()>budget::tool_calls || (grants & ~all_tools)) return Status::fail(Error::invalid, "tool call budget or grant mask");
    for (std::size_t i=0;i<calls.size();++i) {
        if (auto s=validate_tool(calls[i]); !s) return s;
        for (std::size_t j=0;j<i;++j) if (calls[i].id==calls[j].id) return Status::fail(Error::invalid, "duplicate tool id in batch");
    }
    try {
        Transaction tx(impl_->db);
        for(const auto& c:calls) if(auto status=insert_tool(impl_->db,c,true,grants,{},0,actor); !status) return status;
        tx.commit(); return {};
    } catch (const DbError& error) { return db_status(error); }
}

Status SqliteStore::approve_tool(std::string_view id,std::string_view owner,std::uint64_t revision) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"approval storage unavailable");
    if(!valid_identifier(owner) || !revision || revision>1'000'000'000) return Status::fail(Error::invalid,"invalid owner approval");
    try {
        Statement q(impl_->db,"UPDATE tool_jobs SET state=0,approved_by=?2,approval_revision=?3,result='owner confirmed immutable payload' WHERE id=?1 AND state=1");
        q.text(1,id); q.text(2,owner); q.integer(3,static_cast<std::int64_t>(revision)); q.done();
        return sqlite3_changes(impl_->db)==1 ? Status{} : Status::fail(Error::busy,"approval job changed");
    } catch(const DbError& e) { return db_status(e); }
}

Status SqliteStore::identity_forget(std::string_view id) {
    if(!impl_->ready) return impl_->ready;
    if(!available_) return Status::fail(Error::io,"identity storage unavailable");
    if(!valid_identifier(id)) return Status::fail(Error::invalid,"invalid identity id");
    try {
        Transaction tx(impl_->db);
        Statement profile(impl_->db,"UPDATE voice_profiles SET revision=revision+1 WHERE id=?1"); profile.text(1,id); profile.done();
        if(sqlite3_changes(impl_->db)!=1) return Status::fail(Error::unavailable,"unknown identity");
        for(const auto* sql:{"DELETE FROM voice_templates WHERE profile_id=?1", "DELETE FROM voice_encounters WHERE profile_id=?1"}) {
            Statement q(impl_->db,sql); q.text(1,id); q.done();
        }
        Statement guest(impl_->db,"DELETE FROM voice_profiles WHERE id=?1 AND role=2"); guest.text(1,id); guest.done();
        tx.commit(); return {};
    } catch(const DbError& e) { return db_status(e); }
}

}
