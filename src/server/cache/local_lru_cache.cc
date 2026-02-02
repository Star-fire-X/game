#include "local_lru_cache.h"

#include <spdlog/spdlog.h>

namespace mir2::cache {

LocalLRUCache::LocalLRUCache(size_t capacity)
    : cache_(capacity), capacity_(capacity) {
    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info("LocalLRUCache initialized with capacity: {}", capacity);
    }
}

LocalLRUCache::~LocalLRUCache() {
    Clear();
}

std::optional<VersionedData> LocalLRUCache::Get(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 命中，统计并返回
        // 注意：tsl::lru_map 的 find 会自动更新 LRU 顺序
        stats_.hits++;
        return it->second;
    }

    // 未命中
    stats_.misses++;
    return std::nullopt;
}

void LocalLRUCache::Set(const std::string& key, const VersionedData& data) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 写入数据，如果 key 已存在会覆盖
    // tsl::lru_map 自动处理 LRU 淘汰
    cache_.insert_or_assign(key, data);
    stats_.writes++;

    // 容量溢出时自动淘汰最老的条目
    if (cache_.size() > capacity_) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->debug("LRU cache eviction triggered: size={}", cache_.size());
        }
        // tsl::lru_map 会自动处理
    }
}

bool LocalLRUCache::Delete(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return cache_.erase(key) > 0;
}

void LocalLRUCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
}

CacheStats LocalLRUCache::GetStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_;
}

size_t LocalLRUCache::GetSize() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_.size();
}

}  // namespace mir2::cache
