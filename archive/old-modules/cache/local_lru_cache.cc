#include "local_lru_cache.h"

#include <spdlog/spdlog.h>

namespace mir2::cache {

LocalLRUCache::LocalLRUCache(size_t capacity)
    : capacity_(capacity) {
    cache_.reserve(capacity_);
    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info("LocalLRUCache initialized with capacity: {}", capacity);
    }
}

LocalLRUCache::~LocalLRUCache() {
    Clear();
}

std::optional<VersionedData> LocalLRUCache::Get(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 命中，移动到 LRU 队列头部
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
        stats_.hits++;
        return it->second.data;
    }

    // 未命中
    stats_.misses++;
    return std::nullopt;
}

void LocalLRUCache::Set(const std::string& key, const VersionedData& data) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.data = data;
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
    } else {
        lru_list_.push_front(key);
        cache_.emplace(key, CacheEntry{data, lru_list_.begin()});
    }
    stats_.writes++;

    // 容量溢出时淘汰最老的条目
    if (cache_.size() > capacity_) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->debug("LRU cache eviction triggered: size={}", cache_.size());
        }
        const auto& lru_key = lru_list_.back();
        cache_.erase(lru_key);
        lru_list_.pop_back();
    }
}

bool LocalLRUCache::Delete(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    lru_list_.erase(it->second.lru_it);
    cache_.erase(it);
    return true;
}

void LocalLRUCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
    lru_list_.clear();
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
