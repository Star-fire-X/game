#ifndef MIR2_SERVER_CACHE_ROCKSDB_CACHE_H_
#define MIR2_SERVER_CACHE_ROCKSDB_CACHE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cache_types.h"
#include "rocksdb/db.h"
#include "rocksdb/utilities/db_ttl.h"

namespace mir2::cache {

// L2 本地持久化缓存：基于 RocksDB + TTL
// 特性：
// - DBWithTTL：自动过期机制（默认 1 小时）
// - Bloom Filter：快速过滤不存在的 key（10 bits/key, 1% 假阳性）
// - Block Cache：热数据常驻内存（256MB）
// - PinnableSlice：零拷贝读取
// - WriteBatch：原子批量写入
class RocksDBCache {
public:
    // RocksDB 配置结构体
    struct Config {
        std::string db_path = "./data/rocksdb";
        size_t write_buffer_size = 64 * 1024 * 1024;  // 64MB
        size_t block_cache_size = 256 * 1024 * 1024;  // 256MB
        int32_t ttl_seconds = 3600;                    // 1 小时
        double bloom_filter_bits_per_key = 10.0;       // 10 bits/key
    };

    // 构造函数
    explicit RocksDBCache(const Config& config);

    // 析构函数（正确释放资源）
    ~RocksDBCache();

    // 初始化数据库
    // 返回 true 表示成功，false 表示失败
    bool Initialize();

    // 从数据库读取数据（PinnableSlice 零拷贝）
    std::optional<VersionedData> Get(const std::string& key);

    // 写入数据到数据库（带版本检查）
    bool Set(const std::string& key, const VersionedData& data);

    // 批量写入（WriteBatch 原子操作）
    bool BatchSet(
        const std::vector<std::pair<std::string, VersionedData>>& batch);

    // 删除指定 key
    bool Delete(const std::string& key);

    // 更新最大版本号元数据
    bool UpdateMaxVersion(uint64_t version);

    // 获取最大版本号元数据
    uint64_t GetMaxVersion() const;

    // 检查数据库是否打开
    bool IsOpen() const { return db_ != nullptr; }

    // 获取数据库统计信息
    std::string GetDBStats();

private:
    // 序列化 VersionedData 为字节数组
    std::vector<uint8_t> SerializeVersionedData(
        const VersionedData& data) const;

    // 反序列化字节数组为 VersionedData
    bool DeserializeVersionedData(const rocksdb::Slice& data,
                                  VersionedData& result);

    // 获取用于存储最大版本号的特殊 key
    static std::string GetMaxVersionKey() {
        return "__max_version__";
    }

    // 配置信息
    Config config_;

    // RocksDB 数据库指针（需手动释放）
    rocksdb::DBWithTTL* db_ = nullptr;

    // Block Cache（智能指针自动释放）
    std::shared_ptr<rocksdb::Cache> block_cache_;

    // 最大版本号（在内存中缓存）
    uint64_t max_version_ = 0;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_ROCKSDB_CACHE_H_
