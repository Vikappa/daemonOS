#pragma once
#include "daemon/hal/interfaces.hpp"
#include "daemon/services/store.hpp"
#include "daemon/services/resources.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>

namespace daemonos::desktop {
class Allocator final : public MemoryAllocator {
public: void* allocate(std::size_t bytes,MemoryPool pool) override; void release(void* memory) override;
};
class VirtualClock final : public hal::Clock {
public:
    explicit VirtualClock(std::int64_t epoch);
    std::int64_t monotonic_ms() const override;
    std::int64_t wall_seconds() const override;
    void advance(std::int64_t milliseconds);
    void speed(double multiplier);
    double speed() const { return speed_; }
    void set_wall(std::int64_t epoch);
private:
    void synchronize();
    std::chrono::steady_clock::time_point origin_;
    std::int64_t virtual_ms_{};
    std::int64_t wall_origin_{};
    double speed_{1};
};
class Random final : public hal::Random {
public:
    Random();
    explicit Random(std::uint32_t seed) : generator_(seed) {}
    std::uint32_t next() override { return static_cast<std::uint32_t>(generator_()); }
private: std::mt19937 generator_;
};
class SqliteStore final : public StateStore {
public:
    std::string_view backend_name() const override { return "SQLite / local authority"; }
    explicit SqliteStore(const std::filesystem::path& path);
    ~SqliteStore() override;
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;
    Status load(CreatureState& state, Settings& settings) override;
    Status save(const CreatureState& state, const Settings& settings, const Memory* memory = nullptr) override;
    Status memories(std::string_view query, std::size_t offset, MemoryPage& page) override;
    Status backup(std::string_view destination) override;
    void available(bool value) override { available_ = value; }
    Status integrity();
    Status trim() override;
    static std::string_view version();
    static Status configure_heap(void* memory,std::size_t size);
    static Status shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool available_{true};
};
class FileCheckpoint final : public CheckpointStore {
public:
    explicit FileCheckpoint(std::filesystem::path path) : path_(std::move(path)) {}
    Status load(CreatureState& state) override;
    Status save(const CreatureState& state) override;
private: std::filesystem::path path_;
};
}
