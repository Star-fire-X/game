#ifndef MIR2_STORAGE_ENGINE_L2_ROCKSDB_CACHE_H_
#define MIR2_STORAGE_ENGINE_L2_ROCKSDB_CACHE_H_

#include <atomic>
#include <functional>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <storage_engine/storage_engine.h>

#include "rocksdb/db.h"
#include "rocksdb/statistics.h"
#include "rocksdb/utilities/db_ttl.h"

namespace mir2::storage_engine::l2 {

// L2 local persistent cache based on RocksDB + TTL.
class RocksDBCache {
public:
    struct Config {
        std::string db_path = "./data/rocksdb";
        // Legacy field kept for compatibility with old callers.
        size_t write_buffer_size = 64 * 1024 * 1024;
        // 0 means "unset", fallback to legacy write_buffer_size.
        size_t data_write_buffer_size = 0;
        // 0 means "unset", fallback to max(4MB, data_write_buffer_size / 8).
        size_t meta_write_buffer_size = 0;
        int data_max_write_buffer_number = 4;
        int meta_max_write_buffer_number = 2;
        size_t block_cache_size = 256 * 1024 * 1024;
        uint32_t block_size = 4096;
        int32_t ttl_seconds = 3600;
        double bloom_filter_bits_per_key = 10.0;
        int max_background_jobs = 8;
        int max_background_flushes = 2;
        uint32_t ttl_periodic_compaction_seconds = 21600;
        bool strict_ttl_reads = true;
        bool scan_fill_cache = false;
        bool iter_pin_data = true;
        // Use a read-only no-block-cache DB handle for ForEach scans.
        bool isolate_foreach_scan_reader = false;
        // Enable RocksDB ticker stats (used for block-cache hit/miss observability).
        bool enable_statistics = false;
        uint64_t max_version_persist_step = 64;
    };

    explicit RocksDBCache(const Config& config);
    ~RocksDBCache();

    bool Initialize();

    enum class DataTier {
        kPersistent,
        kTtl,
    };

    std::optional<VersionedData> Get(const std::string& key);
    std::optional<VersionedData> Get(const std::string& key, DataTier tier);

    bool Set(const std::string& key, const VersionedData& data);
    bool Set(const std::string& key, const VersionedData& data, DataTier tier);

    bool BatchSet(
        const std::vector<std::pair<std::string, VersionedData>>& batch);
    bool BatchSet(
        const std::vector<std::pair<std::string, VersionedData>>& batch,
        DataTier tier);

    bool Delete(const std::string& key);
    bool Delete(const std::string& key, DataTier tier);

    /// Synchronous write (WAL fsync). Use for crash-safe commits.
    bool SetSync(const std::string& key, const VersionedData& data);
    bool SetSync(const std::string& key, const VersionedData& data,
                 DataTier tier);

    /// Iterate all user entries (skips internal keys like __max_version__).
    /// Callback returns true to continue, false to stop early.
    /// Returns count of entries visited.
    using IteratorCallback = std::function<bool(const std::string& key, const VersionedData& data)>;
    size_t ForEach(IteratorCallback cb);
    size_t ForEach(IteratorCallback cb, DataTier tier);
    size_t DeleteByPrefix(const std::string& prefix,
                          DataTier tier,
                          size_t batch_size = 1024);

    bool UpdateMaxVersion(uint64_t version);

    uint64_t GetMaxVersion() const;

    bool IsOpen() const { return db_ != nullptr; }

    size_t GetApproximateSizeBytes() const;

    std::string GetDBStats();
    bool GetUInt64Property(const std::string& property, uint64_t* out) const;

    struct OutboxEntry {
        uint64_t outbox_id = 0;
        std::string key;
        VersionedData data;
        Priority priority = Priority::NORMAL;
    };

    bool AppendOutbox(const std::string& key,
                      const VersionedData& data,
                      Priority priority,
                      uint64_t* outbox_id = nullptr);

    bool AckOutbox(uint64_t outbox_id);

    size_t ReplayOutbox(size_t limit,
                        const std::function<bool(const OutboxEntry&)>& cb);

    size_t OutboxDepth() const;

    struct DeadLetterEntry {
        uint64_t dead_letter_id = 0;
        std::string key;
        VersionedData data;
        Priority priority = Priority::NORMAL;
        uint32_t attempts = 0;
        uint64_t durable_outbox_id = 0;
        uint64_t recorded_at_ms = 0;
        std::string error_message;
    };

    bool AppendDeadLetter(const DeadLetterEntry& entry,
                          uint64_t* dead_letter_id = nullptr);

    bool AckDeadLetter(uint64_t dead_letter_id);

    size_t ReplayDeadLetter(size_t limit,
                            const std::function<bool(const DeadLetterEntry&)>& cb);

    size_t DeadLetterDepth() const;

private:
    static constexpr size_t kDefaultCFIndex = 0;
    static constexpr size_t kDataPersistentCFIndex = 1;
    static constexpr size_t kDataTtlCFIndex = 2;
    static constexpr size_t kOutboxCFIndex = 3;
    static constexpr size_t kDeadLetterCFIndex = 4;
    static constexpr size_t kMetaCFIndex = 5;
    static constexpr size_t kColumnFamilyCount = 6;

    bool AssignColumnFamilies(std::vector<rocksdb::ColumnFamilyHandle*> handles);
    void DestroyColumnFamilies();
    bool WriteSchemaVersionMarker();
    bool LoadOutboxNextId();
    bool LoadDeadLetterNextId();
    bool PersistOutboxNextIdLocked(uint64_t next_id);
    bool PersistDeadLetterNextIdLocked(uint64_t next_id);
    rocksdb::ColumnFamilyHandle* ResolveDataColumnFamily(DataTier tier) const;
    rocksdb::ColumnFamilyHandle* ResolvePeerDataColumnFamily(DataTier tier) const;
    std::optional<VersionedData> GetFromColumnFamily(
        const std::string& key, rocksdb::ColumnFamilyHandle* cf);
    bool PutToDataColumnFamily(const std::string& key,
                               const VersionedData& data,
                               DataTier tier,
                               bool sync);
    bool UpdateMaxVersionCache(uint64_t version);

    void MaybePersistMaxVersion(uint64_t version, bool force);

    std::vector<uint8_t> SerializeVersionedData(
        const VersionedData& data) const;

    bool DeserializeVersionedData(const rocksdb::Slice& data,
                                  VersionedData& result);
    std::string MakeOutboxStorageKey(uint64_t outbox_id) const;
    bool ParseOutboxStorageKey(const rocksdb::Slice& storage_key,
                               uint64_t* outbox_id) const;
    std::vector<uint8_t> SerializeOutboxValue(const std::string& key,
                                              const VersionedData& data,
                                              Priority priority) const;
    bool DeserializeOutboxValue(const rocksdb::Slice& value,
                                OutboxEntry* entry);
    std::string MakeDeadLetterStorageKey(uint64_t dead_letter_id) const;
    bool ParseDeadLetterStorageKey(const rocksdb::Slice& storage_key,
                                   uint64_t* dead_letter_id) const;
    std::vector<uint8_t> SerializeDeadLetterValue(
        const DeadLetterEntry& entry) const;
    bool DeserializeDeadLetterValue(const rocksdb::Slice& value,
                                    DeadLetterEntry* entry);
    std::string EncodeUint64ToString(uint64_t value) const;
    bool DecodeUint64FromSlice(const rocksdb::Slice& value,
                               uint64_t* out) const;
    rocksdb::ReadOptions BuildPointReadOptions() const;
    rocksdb::ReadOptions BuildScanReadOptions() const;
    bool IsStrictTtlExpired(const VersionedData& data) const;

    static std::string GetMaxVersionKey() {
        return "__max_version__";
    }
    static std::string GetSchemaVersionKey() {
        return "__storage_schema_version__";
    }
    static std::string GetSchemaVersionValue() {
        return "multi_cf_v1";
    }
    static std::string GetDataPersistentCFName() {
        return "cf_data_persistent";
    }
    static std::string GetDataTtlCFName() {
        return "cf_data_ttl";
    }
    static std::string GetOutboxCFName() {
        return "cf_outbox";
    }
    static std::string GetDeadLetterCFName() {
        return "cf_dead_letter";
    }
    static std::string GetMetaCFName() {
        return "cf_meta";
    }
    static std::string GetOutboxNextIdKey() {
        return "__outbox_next_id__";
    }
    static std::string GetDeadLetterNextIdKey() {
        return "__dead_letter_next_id__";
    }
    static std::string GetOutboxStoragePrefix() {
        return "outbox:";
    }
    static std::string GetDeadLetterStoragePrefix() {
        return "dead_letter:";
    }

    Config config_;
    std::unique_ptr<rocksdb::DBWithTTL> db_;
    std::shared_ptr<rocksdb::Cache> block_cache_;
    std::shared_ptr<rocksdb::Statistics> statistics_;
    std::vector<rocksdb::ColumnFamilyHandle*> cf_handles_;
    rocksdb::ColumnFamilyHandle* default_cf_ = nullptr;
    rocksdb::ColumnFamilyHandle* data_persistent_cf_ = nullptr;
    rocksdb::ColumnFamilyHandle* data_ttl_cf_ = nullptr;
    rocksdb::ColumnFamilyHandle* outbox_cf_ = nullptr;
    rocksdb::ColumnFamilyHandle* dead_letter_cf_ = nullptr;
    rocksdb::ColumnFamilyHandle* meta_cf_ = nullptr;
    mutable std::mutex outbox_mutex_;
    uint64_t outbox_next_id_ = 1;
    mutable std::mutex dead_letter_mutex_;
    uint64_t dead_letter_next_id_ = 1;
    std::atomic<uint64_t> max_version_{0};
    std::atomic<uint64_t> persisted_max_version_{0};
    mutable std::mutex max_version_persist_mutex_;
};

}  // namespace mir2::storage_engine::l2

#endif  // MIR2_STORAGE_ENGINE_L2_ROCKSDB_CACHE_H_
