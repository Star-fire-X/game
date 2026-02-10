#ifndef MIR2_STORAGE_ENGINE_L2_ROCKSDB_CACHE_H_
#define MIR2_STORAGE_ENGINE_L2_ROCKSDB_CACHE_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <storage_engine/storage_engine.h>

#include "rocksdb/db.h"
#include "rocksdb/utilities/db_ttl.h"

namespace mir2::storage_engine::l2 {

// L2 local persistent cache based on RocksDB + TTL.
class RocksDBCache {
public:
    struct Config {
        std::string db_path = "./data/rocksdb";
        size_t write_buffer_size = 64 * 1024 * 1024;
        size_t block_cache_size = 256 * 1024 * 1024;
        int32_t ttl_seconds = 3600;
        double bloom_filter_bits_per_key = 10.0;
        uint64_t max_version_persist_step = 64;
    };

    explicit RocksDBCache(const Config& config);
    ~RocksDBCache();

    bool Initialize();

    std::optional<VersionedData> Get(const std::string& key);

    bool Set(const std::string& key, const VersionedData& data);

    bool BatchSet(
        const std::vector<std::pair<std::string, VersionedData>>& batch);

    bool Delete(const std::string& key);

    /// Synchronous write (WAL fsync). Use for crash-safe commits.
    bool SetSync(const std::string& key, const VersionedData& data);

    /// Iterate all user entries (skips internal keys like __max_version__).
    /// Callback returns true to continue, false to stop early.
    /// Returns count of entries visited.
    using IteratorCallback = std::function<bool(const std::string& key, const VersionedData& data)>;
    size_t ForEach(IteratorCallback cb);

    bool UpdateMaxVersion(uint64_t version);

    uint64_t GetMaxVersion() const;

    bool IsOpen() const { return db_ != nullptr; }

    size_t GetApproximateSizeBytes() const;

    std::string GetDBStats();

private:
    void MaybePersistMaxVersion(uint64_t version, bool force);

    std::vector<uint8_t> SerializeVersionedData(
        const VersionedData& data) const;

    bool DeserializeVersionedData(const rocksdb::Slice& data,
                                  VersionedData& result);

    static std::string GetMaxVersionKey() {
        return "__max_version__";
    }

    Config config_;
    std::unique_ptr<rocksdb::DBWithTTL> db_;
    std::shared_ptr<rocksdb::Cache> block_cache_;
    std::atomic<uint64_t> max_version_{0};
    std::atomic<uint64_t> persisted_max_version_{0};
    mutable std::mutex max_version_persist_mutex_;
};

}  // namespace mir2::storage_engine::l2

#endif  // MIR2_STORAGE_ENGINE_L2_ROCKSDB_CACHE_H_
