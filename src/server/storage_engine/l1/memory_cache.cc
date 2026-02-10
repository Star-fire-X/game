#include <storage_engine/l1/memory_cache.h>

#include <vector>

namespace mir2::storage_engine::l1 {

MemoryCache::MemoryCache(size_t capacity, uint32_t ttl_seconds)
    : capacity_(capacity),
      ttl_(std::chrono::seconds(ttl_seconds)) {
    cache_.reserve(capacity_);
}

MemoryCache::~MemoryCache() {
    Clear();
}

std::optional<VersionedData> MemoryCache::Get(const std::string& key) {
    // Use shared_lock for concurrent reads - no LRU list modification here.
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Increment access counter atomically for lazy LRU tracking.
        // This avoids exclusive lock acquisition on every read.
        auto& entry = it->second;
        if (IsExpired(entry, now)) {
            lock.unlock();

            std::unique_lock<std::shared_mutex> write_lock(mutex_);
            auto write_it = cache_.find(key);
            if (write_it != cache_.end() && IsExpired(write_it->second, now)) {
                lru_list_.erase(write_it->second.lru_it);
                cache_.erase(write_it);
            }
            stats_.misses.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }

        uint32_t access_count =
            entry.access_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (access_count >= kPromotionThreshold &&
            !entry.promotion_queued.exchange(true, std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> promotion_lock(promotion_mutex_);
            if (promotion_candidates_.size() < capacity_) {
                promotion_candidates_.push_back(key);
            } else {
                entry.promotion_queued.store(false, std::memory_order_relaxed);
            }
        }
        stats_.hits.fetch_add(1, std::memory_order_relaxed);
        return entry.data;
    }

    stats_.misses.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
}

void MemoryCache::Set(const std::string& key, const VersionedData& data) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();

    // Promote hot entries before modifying the cache.
    PromoteHotEntries();

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.data = data;
        it->second.access_count.store(0, std::memory_order_relaxed);
        it->second.expires_at = ComputeExpiryTime(now);
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
    } else {
        lru_list_.push_front(key);
        cache_.emplace(
            key, CacheEntry(data, lru_list_.begin(), ComputeExpiryTime(now)));
    }
    stats_.writes.fetch_add(1, std::memory_order_relaxed);

    // Evict LRU entries if over capacity.
    while (cache_.size() > capacity_ && !lru_list_.empty()) {
        const auto& lru_key = lru_list_.back();
        cache_.erase(lru_key);
        lru_list_.pop_back();
    }
}

bool MemoryCache::Delete(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    lru_list_.erase(it->second.lru_it);
    cache_.erase(it);
    return true;
}

void MemoryCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
    lru_list_.clear();
    {
        std::lock_guard<std::mutex> promotion_lock(promotion_mutex_);
        promotion_candidates_.clear();
    }
}

size_t MemoryCache::GetSize() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_.size();
}

void MemoryCache::PromoteHotEntries() {
    // Called under exclusive lock during Set operations.
    // Process only a bounded batch to avoid O(n) scans on every write.
    std::vector<std::string> batch;
    batch.reserve(kPromotionBatchSize);
    {
        std::lock_guard<std::mutex> promotion_lock(promotion_mutex_);
        while (!promotion_candidates_.empty() &&
               batch.size() < kPromotionBatchSize) {
            batch.push_back(std::move(promotion_candidates_.front()));
            promotion_candidates_.pop_front();
        }
    }

    for (const auto& key : batch) {
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            continue;
        }

        auto& entry = it->second;
        entry.promotion_queued.store(false, std::memory_order_relaxed);

        uint32_t count = entry.access_count.load(std::memory_order_relaxed);
        if (count < kPromotionThreshold) {
            continue;
        }

        // Reset counter and move to front.
        entry.access_count.store(0, std::memory_order_relaxed);
        lru_list_.splice(lru_list_.begin(), lru_list_, entry.lru_it);
    }
}

bool MemoryCache::IsExpired(
    const CacheEntry& entry, std::chrono::steady_clock::time_point now) const {
    if (ttl_.count() == 0) {
        return false;
    }
    return now >= entry.expires_at;
}

std::chrono::steady_clock::time_point MemoryCache::ComputeExpiryTime(
    std::chrono::steady_clock::time_point now) const {
    if (ttl_.count() == 0) {
        return std::chrono::steady_clock::time_point::max();
    }
    return now + ttl_;
}

}  // namespace mir2::storage_engine::l1
