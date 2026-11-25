#pragma once
#include "daemon/types.hpp"
#include <span>
#include <utility>

namespace daemonos {
enum class MemoryPool : std::uint8_t { internal, external, count };
enum class MemoryCategory : std::uint8_t { runtime, graphics, audio, assets, database, cognitive, count };
struct MemoryBudget {
    std::array<std::size_t, 2> pools{128 * 1024, 6 * 1024 * 1024};
    std::array<std::size_t, 6> categories{96 * 1024, 768 * 1024, 64 * 1024, 128 * 1024, 192 * 1024, 32 * 1024};
};
struct MemoryAllocator {
    virtual ~MemoryAllocator() = default;
    virtual void* allocate(std::size_t bytes, MemoryPool pool) = 0;
    virtual void release(void* memory) = 0;
};
// Single owner thread. SDK allocations remain outside these limits with reserved headroom.
class ResourceManager {
public:
    class Lease {
    public:
        Lease() noexcept : owner_(nullptr),category_(MemoryCategory::runtime),pool_(MemoryPool::internal),bytes_(0),data_(nullptr) {}
        ~Lease() { reset(); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : owner_(std::exchange(other.owner_,nullptr)),category_(other.category_),pool_(other.pool_),bytes_(other.bytes_),data_(std::exchange(other.data_,nullptr)) {}
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                reset(); owner_ = std::exchange(other.owner_, nullptr); category_ = other.category_;
                pool_ = other.pool_; bytes_ = other.bytes_; data_ = std::exchange(other.data_, nullptr);
            }
            return *this;
        }
        void reset();
        explicit operator bool() const { return owner_ != nullptr; }
        std::span<std::byte> bytes() const { return {static_cast<std::byte*>(data_), data_ ? bytes_ : 0}; }
        void* data() const { return data_; }
    private:
        friend class ResourceManager;
        ResourceManager* owner_{};
        MemoryCategory category_{};
        MemoryPool pool_{};
        std::size_t bytes_{};
        void* data_{};
    };
    explicit ResourceManager(MemoryBudget limits = {}, MemoryAllocator* allocator = nullptr) : limits_(limits), allocator_(allocator) {}
    Lease reserve(MemoryCategory category, MemoryPool pool, std::size_t bytes);
    Lease allocate(MemoryCategory category, MemoryPool pool, std::size_t bytes);
    void pressure(bool active) { pressure_ = active; }
    bool pressured() const { return pressure_; }
    std::size_t used(MemoryPool pool) const { return pools_[static_cast<std::size_t>(pool)]; }
    std::size_t used(MemoryCategory category) const { return categories_[static_cast<std::size_t>(category)]; }
    std::size_t peak(MemoryPool pool) const { return peaks_[static_cast<std::size_t>(pool)]; }
    std::size_t rejected() const { return rejected_; }
    const MemoryBudget& limits() const { return limits_; }
private:
    MemoryBudget limits_{};
    MemoryAllocator* allocator_{};
    std::array<std::size_t, 2> pools_{}, peaks_{};
    std::array<std::size_t, 6> categories_{};
    std::size_t rejected_{};
    bool pressure_{};
};
}
