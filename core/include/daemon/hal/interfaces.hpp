#pragma once
#include "daemon/types.hpp"
#include <span>

namespace daemonos::hal {
struct Clock {
    virtual ~Clock() = default;
    virtual std::int64_t monotonic_ms() const = 0;
    virtual std::int64_t wall_seconds() const = 0;
};
struct Random { virtual ~Random() = default; virtual std::uint32_t next() = 0; };
struct Display {
    virtual ~Display() = default;
    virtual Status flush(int x, int y, int width, int height, std::span<const std::uint32_t> pixels) = 0;
    virtual void brightness(int percent) = 0;
};
struct TouchPoint { int x{}; int y{}; bool pressed{}; };
struct Touch { virtual ~Touch() = default; virtual TouchPoint read() const = 0; };
struct AudioInput {
    virtual ~AudioInput() = default;
    virtual Status start(int sample_rate) = 0;
    virtual std::size_t read(std::span<std::int16_t> samples) = 0;
    virtual void stop() = 0;
};
struct AudioOutput {
    virtual ~AudioOutput() = default;
    virtual Status write(std::span<const std::int16_t> samples) = 0;
    virtual void cancel() = 0;
};
struct Storage {
    virtual ~Storage() = default;
    virtual Status read(std::string_view key, std::span<std::byte> target, std::size_t& size) = 0;
    virtual Status write_atomic(std::string_view key, std::span<const std::byte> data) = 0;
};
struct Transport {
    virtual ~Transport() = default;
    virtual bool connected() const = 0;
    virtual Status send(std::span<const std::byte> frame) = 0;
};
struct Bluetooth : Transport { virtual Status disconnect() = 0; };
struct Wifi : Transport { virtual Status set_enabled(bool enabled) = 0; };
struct Rtc { virtual ~Rtc() = default; virtual Status set_alarm(std::int64_t epoch) = 0; };
struct Motion { float ax{}; float ay{}; float az{1}; std::uint32_t steps{}; };
struct Inertial { virtual ~Inertial() = default; virtual Motion sample() const = 0; };
struct Position { double latitude{}; double longitude{}; bool fix{}; };
struct Gps { virtual ~Gps() = default; virtual Position sample() const = 0; };
struct Nfc { virtual ~Nfc() = default; virtual Status poll(std::span<std::byte> uid) = 0; };
struct Lora : Transport {};
struct Haptic { virtual ~Haptic() = default; virtual Status pulse(std::uint32_t ms) = 0; };
struct Battery { virtual ~Battery() = default; virtual int percent() const = 0; virtual bool charging() const = 0; };
struct Power { virtual ~Power() = default; virtual Status transition(PowerState state) = 0; };
}
