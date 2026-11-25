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
inline constexpr std::size_t context_bytes = 4096;
inline constexpr std::size_t response_bytes = 2048;
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
};
struct MemoryPage { std::array<Memory, budget::memory_page> items{}; std::size_t size{}; };
enum class AppId : std::uint8_t { launcher, clock, settings, creature };
enum class EventType : std::uint8_t {
    feed, rest, pet, move, battery, charging, connectivity, storage, think,
    open_app, back, home, toggle_cognition, brightness, suspend, wake,
    memory_pressure, peripheral, inspect_contract
};
struct Event { EventType type{}; std::int32_t value{}; };
enum class PowerState : std::uint8_t { deep_sleep, ble_standby, interactive, listening, thinking, speaking, charging, standby = ble_standby };
inline std::string_view power_name(PowerState s) {
    constexpr std::array names{"DEEP SLEEP", "STANDBY", "INTERACTIVE", "LISTENING", "THINKING", "SPEAKING", "CHARGING"};
    return names[static_cast<std::size_t>(s)];
}
struct Environment { int battery{85}; bool charging{}; bool connected{}; bool storage{true}; std::uint32_t steps{}; };
struct Settings { int brightness{75}; bool cognition{true}; };
}
