#pragma once
#include "daemon/types.hpp"
#include "daemon/hal/interfaces.hpp"

namespace daemonos {
class CreatureEngine {
public:
    static CreatureState create(hal::Clock& clock, hal::Random& random);
    static void advance(CreatureState& state, std::int64_t elapsed_ms);
    static bool interact(CreatureState& state, EventType event);
    static std::int64_t offline_elapsed(std::int64_t now, std::int64_t then);
};
}
