#include "daemon/services/resources.hpp"

namespace daemonos {
void ResourceManager::Lease::reset() {
    if (!owner_) return;
    if (data_ && owner_->allocator_) owner_->allocator_->release(data_);
    owner_->pools_[static_cast<std::size_t>(pool_)] -= bytes_;
    owner_->categories_[static_cast<std::size_t>(category_)] -= bytes_;
    owner_ = nullptr; data_ = nullptr; bytes_ = 0;
}
ResourceManager::Lease ResourceManager::reserve(MemoryCategory category, MemoryPool pool, std::size_t bytes) {
    const auto c = static_cast<std::size_t>(category), p = static_cast<std::size_t>(pool);
    if (c >= categories_.size() || p >= pools_.size() || bytes == 0 ||
        (pressure_ && category != MemoryCategory::runtime) ||
        bytes > limits_.pools[p] - pools_[p] || bytes > limits_.categories[c] - categories_[c]) {
        ++rejected_; return {};
    }
    pools_[p] += bytes; categories_[c] += bytes; peaks_[p] = std::max(peaks_[p], pools_[p]);
    Lease lease; lease.owner_ = this; lease.category_ = category; lease.pool_ = pool; lease.bytes_ = bytes;
    return lease;
}
ResourceManager::Lease ResourceManager::allocate(MemoryCategory category, MemoryPool pool, std::size_t bytes) {
    auto lease = reserve(category, pool, bytes);
    if (!lease) return {};
    if (!allocator_ || !(lease.data_ = allocator_->allocate(bytes, pool))) { ++rejected_; lease.reset(); }
    return lease;
}
}
