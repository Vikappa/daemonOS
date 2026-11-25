#include "daemon/platform/desktop.hpp"
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
                        code == SQLITE_BUSY || code == SQLITE_LOCKED ? Error::busy : Error::io, error.what());
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
        if (current > 2 || current < 0) { impl_->ready = Status::fail(Error::incompatible, "database schema is newer than this runtime"); return; }
        exec(impl_->db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA cache_size=-128; PRAGMA temp_store=MEMORY; PRAGMA mmap_size=0; PRAGMA max_page_count=16384;");
        Transaction transaction(impl_->db);
        if (current < 1) exec(impl_->db, migration1);
        if (current < 2) exec(impl_->db, migration2);
        transaction.commit();
        impl_->ready = integrity();
    } catch (const DbError& e) { impl_->ready = db_status(e); }
}
SqliteStore::~SqliteStore() = default;
std::string_view SqliteStore::version() { return sqlite3_libversion(); }
Status SqliteStore::integrity() {
    if (!impl_->ready) return impl_->ready;
    if (!available_) return Status::fail(Error::io, "simulated MicroSD fault");
    try {
        Statement query(impl_->db, "PRAGMA quick_check(1)");
        if (query.row() && query.text(0) == "ok") return {};
        return Status::fail(Error::corrupt, "database integrity check failed");
    } catch (const DbError& e) { return db_status(e); }
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
            Statement m(impl_->db, "INSERT INTO memories(creature_id,timestamp,category,content,provenance,importance,valence,tags,entities,summary) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
            m.text(1,s.id.view()); m.integer(2,memory->timestamp); m.text(3,memory->category.view()); m.text(4,memory->content.view());
            m.text(5,memory->provenance.view()); m.real(6,memory->importance); m.real(7,memory->valence); m.text(8,memory->tags.view());
            m.text(9,memory->entities.view()); m.text(10,memory->summary.view()); m.done();
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
        Statement q(impl_->db, "SELECT id,timestamp,category,content,provenance,importance,valence,recall_count,last_recall,tags,entities,summary "
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
                !m.tags.assign(q.text(9)) || !m.entities.assign(q.text(10)) || !m.summary.assign(q.text(11)))
                return Status::fail(Error::corrupt, "persisted memory exceeds limits");
            m.importance=q.real(5); m.valence=q.real(6); m.recall_count=static_cast<std::uint32_t>(q.integer(7)); m.last_recall=q.integer(8);
            ++page.size;
        }
        return {};
    } catch (const DbError& e) { return db_status(e); }
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
}
