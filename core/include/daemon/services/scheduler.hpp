#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace daemonos {
class Scheduler {
public:
    using Callback = void (*)(void*);
    bool schedule(std::uint32_t id, std::int64_t due_ms, Callback callback, void* context) {
        if (!callback) return false;
        for (auto& task : tasks_) if (task.active && task.id == id) return false;
        for (auto& task : tasks_) if (!task.active) { task = {id, due_ms, callback, context, ++generation_, true}; return true; }
        return false;
    }
    void cancel(std::uint32_t id) { for (auto& task : tasks_) if (task.id == id) task.active = false; }
    std::size_t run(std::int64_t now) {
        // Snapshot avoids executing tasks scheduled by callbacks in this same pass.
        auto ready = tasks_;
        std::size_t count = 0;
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            if (ready[i].active && ready[i].due <= now && tasks_[i].active && tasks_[i].generation == ready[i].generation) {
                tasks_[i].active = false; ++count; ready[i].callback(ready[i].context);
            }
        }
        return count;
    }
    std::int64_t next_deadline() const {
        auto result=std::numeric_limits<std::int64_t>::max();
        for (const auto& task:tasks_) if (task.active && task.due<result) result=task.due;
        return result;
    }
private:
    struct Task { std::uint32_t id{}; std::int64_t due{}; Callback callback{}; void* context{}; std::uint64_t generation{}; bool active{}; };
    std::array<Task, 16> tasks_{};
    std::uint64_t generation_{};
};
}
