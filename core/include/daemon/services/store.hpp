#pragma once
#include "daemon/types.hpp"

namespace daemonos {
// All calls are serialized on the runtime thread. load returns unavailable if empty.
struct StateStore {
    virtual ~StateStore() = default;
    virtual std::string_view backend_name() const { return "Local state"; }
    virtual Status load(CreatureState& state, Settings& settings) = 0;
    virtual Status save(const CreatureState& state, const Settings& settings, const Memory* memory = nullptr) = 0;
    virtual Status memories(std::string_view query, std::size_t offset, MemoryPage& page) = 0;
    virtual Status backup(std::string_view destination) = 0;
    virtual void available(bool value) = 0;
    virtual Status trim() { return Status::fail(Error::unsupported,"cache trimming unavailable"); }
};
struct CheckpointStore {
    virtual ~CheckpointStore() = default;
    virtual Status load(CreatureState& state) = 0;
    virtual Status save(const CreatureState& state) = 0;
};
}
