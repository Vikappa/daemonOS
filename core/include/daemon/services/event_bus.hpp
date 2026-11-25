#pragma once
#include "daemon/types.hpp"
#include <utility>

namespace daemonos {
// Single owner thread. Posting is bounded; callbacks may post or unsubscribe.
class EventBus {
public:
    using Callback = void (*)(void*, const Event&);
    class Subscription {
    public:
        Subscription() = default;
        Subscription(EventBus& bus, std::size_t slot) : bus_(&bus), slot_(slot) {}
        ~Subscription() { reset(); }
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept : bus_(std::exchange(other.bus_, nullptr)), slot_(other.slot_) {}
        Subscription& operator=(Subscription&& other) noexcept {
            if (this != &other) { reset(); bus_ = std::exchange(other.bus_, nullptr); slot_ = other.slot_; }
            return *this;
        }
        void reset() { if (bus_) { bus_->slots_[slot_].callback = nullptr; bus_ = nullptr; } }
        explicit operator bool() const { return bus_ != nullptr; }
    private:
        EventBus* bus_{};
        std::size_t slot_{};
    };
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    Subscription subscribe(Callback callback, void* context) {
        if (!callback) return {};
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (!slots_[i].callback) { slots_[i] = {callback, context, ++generation_}; return {*this, i}; }
        return {};
    }
    bool post(Event event) {
        if (size_ == queue_.size()) { ++dropped_; return false; }
        queue_[(head_ + size_) % queue_.size()] = event; ++size_; return true;
    }
    std::size_t dispatch(std::size_t limit = budget::events) {
        if (dispatching_) return 0;
        dispatching_ = true;
        std::size_t count = 0;
        while (size_ && count < limit) {
            const auto event = queue_[head_];
            head_ = (head_ + 1) % queue_.size(); --size_; ++count;
            const auto generation = generation_;
            for (auto& slot : slots_)
                if (slot.callback && slot.generation <= generation) slot.callback(slot.context, event);
        }
        dispatching_ = false;
        return count;
    }
    std::size_t dropped() const { return dropped_; }
    std::size_t pending() const { return size_; }
private:
    struct Slot { Callback callback{}; void* context{}; std::uint64_t generation{}; };
    std::array<Event, budget::events> queue_{};
    std::array<Slot, budget::subscribers> slots_{};
    std::size_t head_{}, size_{}, dropped_{};
    std::uint64_t generation_{};
    bool dispatching_{};
};
}
