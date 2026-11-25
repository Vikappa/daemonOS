#include "daemon/platform/desktop.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unistd.h>

namespace daemonos::desktop {
VirtualClock::VirtualClock(std::int64_t epoch) : origin_(std::chrono::steady_clock::now()), wall_origin_(epoch) {}
std::int64_t VirtualClock::monotonic_ms() const {
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - origin_).count();
    return virtual_ms_ + static_cast<std::int64_t>(elapsed * speed_);
}
std::int64_t VirtualClock::wall_seconds() const { return wall_origin_ + monotonic_ms() / 1000; }
void VirtualClock::synchronize() { virtual_ms_ = monotonic_ms(); origin_ = std::chrono::steady_clock::now(); }
void VirtualClock::advance(std::int64_t milliseconds) {
    synchronize(); virtual_ms_ += std::clamp<std::int64_t>(milliseconds, 0, budget::max_offline_seconds * 1000);
}
void VirtualClock::speed(double multiplier) { synchronize(); speed_ = std::isfinite(multiplier) ? std::clamp(multiplier, 0.0, 3600.0) : 1.0; }
void VirtualClock::set_wall(std::int64_t epoch) { synchronize(); wall_origin_ = std::max<std::int64_t>(0, epoch) - virtual_ms_ / 1000; }
Random::Random() : generator_(std::random_device{}()) {}
namespace {
std::uint32_t crc32(std::string_view data) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto c : data) {
        crc ^= static_cast<std::uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
Status atomic_write(const std::filesystem::path& path, std::string_view data) {
    std::error_code error;
    const auto parent = path.parent_path().empty() ? std::filesystem::path{"."} : path.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error) return Status::fail(Error::io, error.message());
    auto temporary = path.string() + ".tmp.XXXXXX";
    int fd = ::mkstemp(temporary.data());
    if (fd < 0) return Status::fail(Error::io, std::strerror(errno));
    std::size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        const auto written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { ok = false; break; }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (ok && ::rename(temporary.c_str(), path.c_str()) != 0) ok = false;
    if (ok) {
        fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (fd < 0) ok = false;
        else { if (::fsync(fd) != 0) ok = false; ::close(fd); }
    }
    if (!ok) { ::unlink(temporary.c_str()); return Status::fail(Error::io, "atomic checkpoint write failed"); }
    return {};
}
}
Status FileCheckpoint::save(const CreatureState& s) {
    if (!valid(s)) return Status::fail(Error::invalid, "invalid checkpoint state");
    std::ostringstream payload;
    payload.imbue(std::locale::classic());
    const auto& n = s.needs; const auto& p = s.personality;
    payload << std::setprecision(17) << "DAEMON-CHECKPOINT 1\n" << std::quoted(std::string{s.id.view()}) << ' '
        << std::quoted(std::string{s.name.view()}) << '\n' << s.born_at << ' ' << s.last_wall << ' '
        << s.age_seconds << ' ' << s.pending_ms << ' ' << s.revision << ' ' << s.sleeping << '\n'
        << n.hunger << ' ' << n.energy << ' ' << n.health << ' ' << n.happiness << ' ' << n.affection << ' '
        << n.boredom << ' ' << n.stress << ' ' << n.curiosity << '\n'
        << p.curiosity << ' ' << p.sociability << ' ' << p.resilience << '\n';
    const auto body = payload.str();
    return atomic_write(path_, body + "CRC32 " + std::to_string(crc32(body)) + "\n");
}
Status FileCheckpoint::load(CreatureState& state) {
    std::error_code error;
    const auto exists = std::filesystem::exists(path_, error);
    if (error) return Status::fail(Error::io, error.message());
    if (!exists) return Status::fail(Error::unavailable, "no identity checkpoint");
    const auto size = std::filesystem::file_size(path_, error);
    if (error || size > 1024) return Status::fail(Error::corrupt, "checkpoint size invalid");
    std::ifstream file(path_, std::ios::binary);
    std::string data(static_cast<std::size_t>(size), '\0');
    if (!file.read(data.data(), static_cast<std::streamsize>(data.size()))) return Status::fail(Error::io, "checkpoint read failed");
    const auto split = data.rfind("CRC32 ");
    if (split == std::string::npos) return Status::fail(Error::corrupt, "checkpoint checksum absent");
    std::uint32_t checksum{};
    std::istringstream trailer(data.substr(split + 6));
    if (!(trailer >> checksum) || checksum != crc32(std::string_view{data}.substr(0, split))) return Status::fail(Error::corrupt, "checkpoint checksum mismatch");
    std::istringstream input(data.substr(0, split)); input.imbue(std::locale::classic());
    CreatureState s; std::string magic, id, name; unsigned version{};
    auto& n = s.needs; auto& p = s.personality;
    if (!(input >> magic >> version >> std::quoted(id) >> std::quoted(name) >> s.born_at >> s.last_wall >> s.age_seconds
          >> s.pending_ms >> s.revision >> s.sleeping >> n.hunger >> n.energy >> n.health >> n.happiness >> n.affection
          >> n.boredom >> n.stress >> n.curiosity >> p.curiosity >> p.sociability >> p.resilience) ||
        magic != "DAEMON-CHECKPOINT" || version != 1 || !s.id.assign(id) || !s.name.assign(name) || !valid(s))
        return Status::fail(Error::corrupt, "checkpoint values invalid");
    input >> std::ws;
    if (!input.eof()) return Status::fail(Error::corrupt, "checkpoint trailing content");
    state = s; return {};
}
}
