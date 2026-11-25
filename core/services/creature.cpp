#include "daemon/services/creature.hpp"
#include <cstdio>
#include <limits>

namespace daemonos {
CreatureState CreatureEngine::create(hal::Clock& clock, hal::Random& random) {
    CreatureState state;
    char id[40]{};
    const auto a = random.next(), b = random.next(), c = random.next(), d = random.next();
    std::snprintf(id, sizeof(id), "%08x-%08x-%08x-%08x", a, b, c, d);
    state.id.assign(id);
    state.born_at = std::max<std::int64_t>(0, clock.wall_seconds());
    state.last_wall = state.born_at;
    return state;
}
std::int64_t CreatureEngine::offline_elapsed(std::int64_t now, std::int64_t then) {
    if (now <= then || then < 0) return 0;
    return std::min(now - then, budget::max_offline_seconds);
}
void CreatureEngine::advance(CreatureState& s, std::int64_t elapsed_ms) {
    if (elapsed_ms <= 0) return;
    // Persist the quantum remainder: 60 one-second updates equal one minute update.
    s.pending_ms += std::min(elapsed_ms, budget::max_offline_seconds * 1000);
    while (s.pending_ms >= 60'000) {
        s.pending_ms -= 60'000;
        s.age_seconds += 60;
        auto& n = s.needs;
        const auto clamp = [](double v) { return std::clamp(v, 0.0, 100.0); };
        n.hunger = clamp(n.hunger + (s.sleeping ? 0.045 : 0.085));
        n.energy = clamp(n.energy + (s.sleeping ? 0.4 : -0.055));
        n.boredom = clamp(n.boredom + (s.sleeping ? -0.08 : 0.07));
        n.affection = clamp(n.affection - 0.012);
        n.stress = clamp(n.stress + (s.sleeping ? -0.09 : (n.hunger > 80 ? 0.06 : -0.01)));
        n.happiness = clamp(n.happiness + (s.sleeping ? 0.012 : -0.018) - (n.hunger > 80 ? 0.04 : 0));
        n.health = clamp(n.health + ((n.hunger > 90 || n.energy < 8) ? -0.08 : 0.02));
        n.curiosity = clamp(n.curiosity + (s.sleeping ? -0.008 : 0.004));
        // Local autonomy: rest when exhausted, wake when restored. Never needs an LLM.
        if (!s.sleeping && n.energy < 10) s.sleeping = true;
        else if (s.sleeping && n.energy >= 95) s.sleeping = false;
        ++s.revision;
    }
}
bool CreatureEngine::interact(CreatureState& s, EventType event) {
    auto& n = s.needs;
    switch (event) {
    case EventType::feed:
        n.hunger = std::max(0.0, n.hunger - 22);
        n.happiness = std::min(100.0, n.happiness + 4);
        break;
    case EventType::rest: s.sleeping = !s.sleeping; break;
    case EventType::pet:
        n.affection = std::min(100.0, n.affection + 7);
        n.stress = std::max(0.0, n.stress - 4);
        n.happiness = std::min(100.0, n.happiness + 3);
        break;
    case EventType::move:
        n.boredom = std::max(0.0, n.boredom - 8);
        n.happiness = std::min(100.0, n.happiness + 2);
        break;
    default: return false;
    }
    ++s.revision;
    return true;
}
}
