#pragma once
#include "daemon/framework/runtime.hpp"
#include <vector>

namespace daemonos::test {
struct Clock final : hal::Clock {
    std::int64_t mono{}, wall{1'700'000'000};
    std::int64_t monotonic_ms() const override { return mono; }
    std::int64_t wall_seconds() const override { return wall; }
    void advance(std::int64_t ms) { mono+=ms; wall+=ms/1000; }
};
struct Random final : hal::Random { std::uint32_t value{42}; std::uint32_t next() override { return value++; } };
struct Checkpoint final : CheckpointStore {
    CreatureState state{}; bool exists{}, fail{}; int writes{};
    Status load(CreatureState& target) override {
        if (!exists) return Status::fail(Error::unavailable,"empty");
        target=state; return {};
    }
    Status save(const CreatureState& value) override {
        if (fail) return Status::fail(Error::io,"fault");
        ++writes; exists=true; state=value; return {};
    }
};
struct Store final : StateStore {
    CreatureState state{}; Settings settings{}; bool exists{}, fault{}; int writes{};
    std::vector<Memory> history;
    Status load(CreatureState& target, Settings& config) override {
        if (fault) return Status::fail(Error::io,"fault");
        if (!exists) return Status::fail(Error::unavailable,"empty");
        target=state; config=settings; return {};
    }
    Status save(const CreatureState& value,const Settings& config,const Memory* memory=nullptr) override {
        if (fault) return Status::fail(Error::io,"fault");
        state=value; settings=config; exists=true; ++writes;
        if (memory) history.push_back(*memory);
        return {};
    }
    Status memories(std::string_view,std::size_t,MemoryPage& page) override { page.size=0; return {}; }
    Status backup(std::string_view) override { return {}; }
    void available(bool value) override { fault=!value; }
};
}
