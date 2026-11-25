#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace daemonos {
enum class Error { none, unavailable, invalid, full, io, corrupt, incompatible, denied, busy, unsupported };
template<std::size_t N> class Text {
public:
    Text() = default;
    explicit Text(std::string_view value) { assign(value); }
    bool assign(std::string_view value) {
        if (value.size() >= N || value.find('\0') != std::string_view::npos) return false;
        data_.fill('\0');
        std::copy(value.begin(), value.end(), data_.begin());
        return true;
    }
    const char* c_str() const { return data_.data(); }
    std::string_view view() const { return {data_.data()}; }
    bool operator==(const Text&) const = default;
private:
    std::array<char, N> data_{};
};
struct Status {
    Error code{Error::none};
    Text<192> message{};
    explicit operator bool() const { return code == Error::none; }
    static Status fail(Error error, std::string_view detail) {
        return {error, Text<192>{detail.substr(0, 191)}};
    }
};
namespace budget {
inline constexpr std::size_t events = 64;
inline constexpr std::size_t subscribers = 16;
inline constexpr std::size_t memory_page = 8;
inline constexpr std::size_t context_bytes = 8192;
inline constexpr std::size_t response_bytes = 4096;
inline constexpr std::size_t assistant_page = 4;
inline constexpr std::size_t assistant_entries = 128;
inline constexpr std::size_t tool_calls = 2;
inline constexpr std::size_t tool_jobs = 64;
inline constexpr std::size_t service_bindings = 16;
inline constexpr std::size_t voice_profiles = 48;
inline constexpr std::size_t voice_dimensions = 192;
inline constexpr std::size_t voice_templates = 3;
inline constexpr std::size_t voice_sessions = 8;
inline constexpr std::size_t agent_tasks = 32;
inline constexpr unsigned agent_turns = 4;
inline constexpr unsigned agent_attempts = 8;
inline constexpr std::int64_t max_reminder_seconds = 365LL * 86400;
inline constexpr std::int64_t max_offline_seconds = 7 * 24 * 3600;
inline constexpr std::int64_t save_interval_ms = 60'000;
inline constexpr std::int64_t checkpoint_interval_ms = 300'000;
}
struct Needs {
    double hunger{25}; // 0 = satiated, 100 = hungry
    double energy{80};
    double health{100};
    double happiness{70};
    double affection{50};
    double boredom{10};
    double stress{5};
    double curiosity{65};
};
struct Personality { double curiosity{0.65}; double sociability{0.6}; double resilience{0.7}; };
struct CreatureState {
    Text<40> id{};
    Text<32> name{std::string_view{"Ember"}};
    std::int64_t born_at{};
    std::int64_t last_wall{};
    std::int64_t age_seconds{};
    std::int64_t pending_ms{};
    std::uint64_t revision{};
    bool sleeping{};
    Needs needs{};
    Personality personality{};
};
inline bool valid(const CreatureState& s) {
    if (s.id.view().empty() || s.name.view().empty() || s.born_at < 0 || s.last_wall < 0 ||
        s.age_seconds < 0 || s.pending_ms < 0 || s.pending_ms >= 60'000 ||
        s.revision > 1'000'000'000'000ULL || s.age_seconds > 100LL * 365 * 86400) return false;
    const auto& n = s.needs;
    for (double v : {n.hunger, n.energy, n.health, n.happiness, n.affection,
                     n.boredom, n.stress, n.curiosity})
        if (!std::isfinite(v) || v < 0 || v > 100) return false;
    for (double v : {s.personality.curiosity, s.personality.sociability, s.personality.resilience})
        if (!std::isfinite(v) || v < 0 || v > 1) return false;
    return true;
}
struct Memory {
    std::int64_t id{};
    std::int64_t timestamp{};
    Text<24> category{std::string_view{"experience"}};
    Text<384> content{};
    Text<32> provenance{std::string_view{"local"}};
    Text<96> tags{};
    Text<96> entities{};
    Text<192> summary{};
    double importance{0.5};
    double valence{};
    std::uint32_t recall_count{};
    std::int64_t last_recall{};
    Text<40> actor_id{};
};
struct MemoryPage { std::array<Memory, budget::memory_page> items{}; std::size_t size{}; };
enum class CreatureFace : std::uint8_t {
    calm, happy, sad, curious, hungry, tired, resting, thinking, charging,
    installing_services, phone_offline, inference_unreachable, internal_error,
    connecting, waiting_response, listening, count
};
enum class MouthPose : std::uint8_t { expression, closed, half, open };
enum class LinkState : std::uint8_t { offline, connecting, ready };
enum class InferencePhase : std::uint8_t { connecting, waiting_response, generating };
inline std::string_view face_name(CreatureFace face) {
    constexpr std::array names{"calm","happy","sad","curious","hungry","tired","resting","thinking","charging",
        "installing_services","phone_offline","inference_unreachable","internal_error","connecting","waiting_response","listening"};
    const auto i=static_cast<std::size_t>(face);
    return i<names.size() ? names[i] : "unknown";
}
inline std::string_view face_label(CreatureFace face) {
    constexpr std::array names{"Calmo","Felice","Triste","Curioso","Affamato","Stanco","A riposo","Pensieroso","In carica",
        "Rigenerazione servizi","Telefono offline","Inferenza irraggiungibile","Errore interno","Connessione","Attesa risposta","In ascolto"};
    const auto i=static_cast<std::size_t>(face);
    return i<names.size() ? names[i] : "";
}
enum class EntryKind : std::uint8_t { note, reminder };
enum class EntryChange : std::uint8_t { erase, complete, reschedule };
struct AssistantEntry {
    std::int64_t id{}, created_at{}, due_at{};
    EntryKind kind{EntryKind::note};
    bool done{};
    Text<256> content{};
    Text<40> actor_id{};
};
struct AssistantPage { std::array<AssistantEntry, budget::assistant_page> items{}; std::size_t size{}; };
enum class ToolName : std::uint8_t {
    save_note, set_reminder, set_alarm, send_message, schedule_agent_task,
    web_search, email_send, email_search, email_read, notification_reply, count
};
using ToolMask = std::uint32_t;
constexpr ToolMask tool_bit(ToolName name) {
    return name < ToolName::count ? ToolMask{1} << static_cast<unsigned>(name) : 0;
}
inline constexpr ToolMask all_tools = (ToolMask{1} << static_cast<unsigned>(ToolName::count)) - 1;
inline constexpr ToolMask bridge_tools = all_tools & ~(tool_bit(ToolName::save_note) |
    tool_bit(ToolName::set_reminder) | tool_bit(ToolName::schedule_agent_task));
enum class ToolState : std::uint8_t { queued, needs_confirmation, dispatched, succeeded, failed, uncertain, cancelled };
struct ToolCall {
    Text<40> id{};
    ToolName name{ToolName::save_note};
    std::int64_t execute_at{}, expires_at{}, at{}; // UTC Unix seconds; zero execute_at means now.
    Text<256> text{};
    Text<64> recipient{}; // Opaque bridge-resolved recipient reference, never a guessed display name.
    Text<32> binding{}; // Configured service binding, not an Android package name.
    Text<96> subject{};
    Text<96> reference{};
    bool operator==(const ToolCall&) const = default;
};
struct ResultSource { Text<64> title{}; Text<192> url{}; };
enum class ServiceError : std::uint8_t { none, unavailable, denied, not_found, unsupported, timeout, rate_limited, unknown };
struct ToolResult {
    Text<40> id{};
    bool success{};
    Text<192> detail{};
    Text<768> data{};
    std::array<ResultSource, 2> sources{};
    std::size_t source_count{};
    ServiceError error{ServiceError::none};
};
struct ToolJob {
    ToolCall call{};
    ToolState state{ToolState::queued};
    Text<192> result{};
    ToolResult output{};
    ToolMask grants{};
    Text<40> actor_id{}, approved_by{};
    std::uint64_t approval_revision{};
};
struct ToolPage { std::array<ToolJob, budget::tool_calls> items{}; std::size_t size{}; };
inline bool phone_tool(ToolName name) { return (bridge_tools & tool_bit(name)) != 0; }
inline bool message_tool(ToolName name) {
    return name==ToolName::send_message || name==ToolName::email_send || name==ToolName::notification_reply;
}
inline bool external_effect(ToolName name) { return message_tool(name) || name==ToolName::set_alarm; }
inline std::string_view tool_name(ToolName name) {
    switch (name) {
    case ToolName::save_note: return "save_note";
    case ToolName::set_reminder: return "set_reminder";
    case ToolName::set_alarm: return "set_phone_alarm";
    case ToolName::send_message: return "message.send";
    case ToolName::schedule_agent_task: return "schedule_agent_task";
    case ToolName::web_search: return "web.search";
    case ToolName::email_send: return "email.send";
    case ToolName::email_search: return "email.search";
    case ToolName::email_read: return "email.read";
    case ToolName::notification_reply: return "notification.reply";
    case ToolName::count: break;
    }
    return {};
}
inline bool valid_identifier(std::string_view value) {
    if(value.empty()) return false;
    for(const auto c:value)
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c==':' || c=='.')) return false;
    return true;
}
struct ServiceBinding {
    Text<32> id{};
    Text<64> label{};
    ToolMask operations{};
};
enum class AgentTaskState : std::uint8_t { scheduled, inferring, waiting_tools, completed, failed, cancelled };
struct AgentTaskRequest {
    Text<40> id{};
    Text<384> instruction{};
    std::int64_t execute_at{}, expires_at{};
    ToolMask grants{tool_bit(ToolName::save_note) | tool_bit(ToolName::set_reminder)};
    bool wake_on_complete{true};
    Text<40> actor_id{};
    bool operator==(const AgentTaskRequest&) const = default;
};
struct AgentTask {
    AgentTaskRequest request{};
    std::int64_t serial{};
    AgentTaskState state{AgentTaskState::scheduled};
    unsigned turn{}, attempts{};
    Text<512> speech{};
    bool notice_pending{};
};
struct AgentTaskPage { std::array<AgentTask, budget::assistant_page> items{}; std::size_t size{}; };
enum class AppId : std::uint8_t { launcher, clock, settings, creature };

enum class EventType : std::uint8_t {
    feed, rest, pet, move, battery, charging, connectivity, storage, think,
    open_app, back, home, toggle_cognition, brightness, suspend, wake,
    services_install, phone_link, inference_link, internal_fault, listen_start, listen_stop,
    speech_start, speech_level, speech_stop, memory_pressure, peripheral, inspect_contract
};
struct Event { EventType type{}; std::int32_t value{}; std::uint32_t token{}; };
// Power aliases preserve the existing bridge/simulator spelling.
enum class PowerState : std::uint8_t { deep_sleep, standby, ble_standby=standby, interactive, listening, thinking, speaking, charging };
inline std::string_view power_name(PowerState s) {
    constexpr std::array names{"DEEP SLEEP", "STANDBY", "INTERACTIVE", "LISTENING", "THINKING", "SPEAKING", "CHARGING"};
    return names[static_cast<std::size_t>(s)];
}
struct Environment { int battery{85}; bool charging{}; bool connected{}; bool storage{true}; std::uint32_t steps{}; };
struct Settings { int brightness{75}; bool cognition{true}; };
}
