#ifndef MIR2_STORAGE_ENGINE_L1_MEMORY_CACHE_H_
#define MIR2_STORAGE_ENGINE_L1_MEMORY_CACHE_H_

#include <chrono>
#include <atomic>
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <storage_engine/storage_engine.h>

namespace mir2::storage_engine::l1 {

// Cache statistics with atomic counters for lock-free updates.
struct CacheStats {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> writes{0};

    CacheStats() = default;

    CacheStats(const CacheStats& other)
        : hits(other.hits.load(std::memory_order_relaxed)),
          misses(other.misses.load(std::memory_order_relaxed)),
          writes(other.writes.load(std::memory_order_relaxed)) {}

    CacheStats& operator=(const CacheStats& other) {
        if (this != &other) {
            hits.store(other.hits.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
            misses.store(other.misses.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
            writes.store(other.writes.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        }
        return *this;
    }

    double GetHitRatio() const {
        uint64_t h = hits.load(std::memory_order_relaxed);
        uint64_t m = misses.load(std::memory_order_relaxed);
        uint64_t total = h + m;
        return total > 0 ? static_cast<double>(h) / total : 0.0;
    }

    void Reset() {
        hits.store(0, std::memory_order_relaxed);
        misses.store(0, std::memory_order_relaxed);
        writes.store(0, std::memory_order_relaxed);
    }
};

// L1 memory cache with lazy LRU eviction.
// Uses shared_lock for reads to maximize concurrency.
// LRU updates are deferred via atomic access counters and applied during writes.
class MemoryCache {
public:
    struct Config {
        size_t capacity = 1000;
        uint32_t ttl_seconds = 0;  // 0 means no TTL expiration.
    };

    explicit MemoryCache(size_t capacity = 1000, uint32_t ttl_seconds = 0);
    explicit MemoryCache(const Config& config)
        : MemoryCache(config.capacity, config.ttl_seconds) {}

    ~MemoryCache();

    // Returns std::nullopt on miss. Uses shared_lock for concurrent reads.
    std::optional<VersionedData> Get(const std::string& key);

    void Set(const std::string& key, const VersionedData& data);

    bool Delete(const std::string& key);

    void Clear();

    CacheStats GetStats() const { return stats_; }

    size_t GetSize() const;

    size_t GetCapacity() const { return capacity_; }

private:
    struct CacheEntry {
        VersionedData data;
        std::list<std::string>::iterator lru_it;
        std::atomic<uint32_t> access_count{0};  // Lazy LRU tracking
        std::atomic<bool> promotion_queued{false};
        std::chrono::steady_clock::time_point expires_at;

        CacheEntry() = default;

        CacheEntry(const VersionedData& data_in,
                   std::list<std::string>::iterator lru_it_in,
                   std::chrono::steady_clock::time_point expires_at_in)
            : data(data_in),
              lru_it(lru_it_in),
              access_count(0),
              expires_at(expires_at_in) {}

        CacheEntry(const CacheEntry& other)
            : data(other.data),
              lru_it(other.lru_it),
              access_count(other.access_count.load(std::memory_order_relaxed)),
              promotion_queued(
                  other.promotion_queued.load(std::memory_order_relaxed)),
              expires_at(other.expires_at) {}

        CacheEntry& operator=(const CacheEntry& other) {
            if (this != &other) {
                data = other.data;
                lru_it = other.lru_it;
                access_count.store(other.access_count.load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
                promotion_queued.store(
                    other.promotion_queued.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
                expires_at = other.expires_at;
            }
            return *this;
        }

        CacheEntry(CacheEntry&& other) noexcept
            : data(std::move(other.data)),
              lru_it(other.lru_it),
              access_count(other.access_count.load(std::memory_order_relaxed)),
              promotion_queued(
                  other.promotion_queued.load(std::memory_order_relaxed)),
              expires_at(other.expires_at) {}

        CacheEntry& operator=(CacheEntry&& other) noexcept {
            if (this != &other) {
                data = std::move(other.data);
                lru_it = other.lru_it;
                access_count.store(other.access_count.load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
                promotion_queued.store(
                    other.promotion_queued.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
                expires_at = other.expires_at;
            }
            return *this;
        }
    };

    // Promote frequently accessed entries to front of LRU list.
    // Called during write operations under exclusive lock.
    void PromoteHotEntries();
    bool IsExpired(const CacheEntry& entry,
                   std::chrono::steady_clock::time_point now) const;
    std::chrono::steady_clock::time_point ComputeExpiryTime(
        std::chrono::steady_clock::time_point now) const;

    std::list<std::string> lru_list_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::deque<std::string> promotion_candidates_;
    mutable std::mutex promotion_mutex_;
    mutable std::shared_mutex mutex_;
    mutable CacheStats stats_;
    size_t capacity_;
    std::chrono::seconds ttl_;

    // Threshold for promoting an entry to LRU front.
    static constexpr uint32_t kPromotionThreshold = 2;
    // Number of queued hot keys processed per write.
    static constexpr size_t kPromotionBatchSize = 16;
};

}  // namespace mir2::storage_engine::l1

#endif  // MIR2_STORAGE_ENGINE_L1_MEMORY_CACHE_H_
