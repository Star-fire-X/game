#ifndef MIR2_SERVER_CACHE_LOCAL_LRU_CACHE_H_
#define MIR2_SERVER_CACHE_LOCAL_LRU_CACHE_H_

#include <optional>
#include <shared_mutex>
#include <string>

#include "cache_types.h"
#include "tsl/lru_map.h"

namespace mir2::cache {

// L1 内存缓存层：基于 tsl::lru_map，LRU 淘汰策略
// 容量：1000 条目，约 20MB
// 性能目标：Get <1μs, Set <1μs（含锁开销）
class LocalLRUCache {
public:
    // 配置结构体
    struct Config {
        size_t capacity = 1000;  // 默认容量 1000 条目
    };

    // 构造函数（接受配置或直接指定容量）
    explicit LocalLRUCache(size_t capacity = 1000);
    explicit LocalLRUCache(const Config& config)
        : LocalLRUCache(config.capacity) {}

    // 析构函数
    ~LocalLRUCache();

    // 从缓存读取数据（shared_lock 并发读）
    // 返回 std::optional，未找到时返回 std::nullopt
    std::optional<VersionedData> Get(const std::string& key);

    // 写入数据到缓存（unique_lock 独占写）
    void Set(const std::string& key, const VersionedData& data);

    // 删除指定 key
    bool Delete(const std::string& key);

    // 清空所有数据
    void Clear();

    // 获取缓存统计信息
    CacheStats GetStats() const;

    // 获取当前缓存大小（条目数）
    size_t GetSize() const;

    // 获取容量上限
    size_t GetCapacity() const { return capacity_; }

private:
    // 使用 tsl::lru_map 存储键值对
    // lru_map 自动处理 LRU 淘汰
    tsl::lru_map<std::string, VersionedData> cache_;

    // 读写锁：多读单写
    mutable std::shared_mutex mutex_;

    // 统计信息
    CacheStats stats_;

    // 缓存容量上限
    size_t capacity_;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_LOCAL_LRU_CACHE_H_
