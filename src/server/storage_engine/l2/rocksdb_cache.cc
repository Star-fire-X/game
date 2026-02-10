#include <storage_engine/l2/rocksdb_cache.h>

#include <algorithm>
#include <cstring>
#include <thread>

#include <spdlog/spdlog.h>

#include "rocksdb/filter_policy.h"
#include "rocksdb/table.h"
#include "rocksdb/utilities/db_ttl.h"

namespace mir2::storage_engine::l2 {

RocksDBCache::RocksDBCache(const Config& config) : config_(config) {}

RocksDBCache::~RocksDBCache() {
    MaybePersistMaxVersion(max_version_.load(std::memory_order_acquire), true);
    db_.reset();
    block_cache_.reset();
}

bool RocksDBCache::Initialize() {
    auto logger = spdlog::get("mir2");

    try {
        rocksdb::Options options;
        options.create_if_missing = true;

        block_cache_ =
            std::shared_ptr<rocksdb::Cache>(rocksdb::NewLRUCache(
                config_.block_cache_size));

        rocksdb::BlockBasedTableOptions table_options;
        table_options.filter_policy.reset(
            rocksdb::NewBloomFilterPolicy(
                config_.bloom_filter_bits_per_key, false));

        table_options.block_cache = block_cache_;
        table_options.cache_index_and_filter_blocks = true;
        table_options.pin_l0_filter_and_index_blocks_in_cache = true;
        table_options.block_size = 4096;

        options.table_factory.reset(
            rocksdb::NewBlockBasedTableFactory(table_options));

        options.compression = rocksdb::kLZ4Compression;
        options.write_buffer_size = config_.write_buffer_size;
        options.max_write_buffer_number = 2;

        options.IncreaseParallelism(std::thread::hardware_concurrency());
        options.OptimizeLevelStyleCompaction(64 * 1024 * 1024);

        rocksdb::DBWithTTL* raw_db = nullptr;
        rocksdb::Status status = rocksdb::DBWithTTL::Open(
            options, config_.db_path, &raw_db, config_.ttl_seconds);

        if (!status.ok()) {
            if (logger) {
                logger->error("Failed to open RocksDB: {}", status.ToString());
            }
            return false;
        }

        db_.reset(raw_db);

        if (logger) {
            logger->info(
                "RocksDB initialized: path={}, ttl={}s, "
                "block_cache={}MB",
                config_.db_path, config_.ttl_seconds,
                config_.block_cache_size / (1024 * 1024));
        }

        std::string max_version_str;
        status = db_->Get(rocksdb::ReadOptions(), GetMaxVersionKey(),
                          &max_version_str);
        if (status.ok() && max_version_str.size() == sizeof(uint64_t)) {
            uint64_t loaded_max_version = 0;
            std::memcpy(&loaded_max_version, max_version_str.data(),
                        sizeof(uint64_t));
            max_version_.store(loaded_max_version,
                               std::memory_order_relaxed);
            persisted_max_version_.store(loaded_max_version,
                                         std::memory_order_relaxed);
        }

        return true;
    } catch (const std::exception& e) {
        if (logger) {
            logger->error("Exception during RocksDB initialization: {}",
                         e.what());
        }
        return false;
    }
}

std::optional<VersionedData> RocksDBCache::Get(const std::string& key) {
    if (!db_) {
        return std::nullopt;
    }

    try {
        rocksdb::PinnableSlice pinnable_val;
        rocksdb::Status status = db_->Get(rocksdb::ReadOptions(),
                                          db_->DefaultColumnFamily(), key,
                                          &pinnable_val);

        if (!status.ok()) {
            if (!status.IsNotFound()) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->debug("RocksDB Get failed for key {}: {}",
                                 key, status.ToString());
                }
            }
            return std::nullopt;
        }

        VersionedData result;
        if (DeserializeVersionedData(rocksdb::Slice(pinnable_val), result)) {
            return result;
        }

        return std::nullopt;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB Get: {}", e.what());
        }
        return std::nullopt;
    }
}

bool RocksDBCache::Set(const std::string& key, const VersionedData& data) {
    if (!db_) {
        return false;
    }

    try {
        auto serialized = SerializeVersionedData(data);

        rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), key,
                                          rocksdb::Slice(
                                              reinterpret_cast<const char*>(
                                                  serialized.data()),
                                              serialized.size()));

        if (!status.ok()) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->error("RocksDB Put failed for key {}: {}", key,
                             status.ToString());
            }
            return false;
        }

        uint64_t current_max = max_version_.load(std::memory_order_relaxed);
        while (data.version > current_max) {
            if (max_version_.compare_exchange_weak(
                    current_max, data.version,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                MaybePersistMaxVersion(data.version, false);
                break;
            }
        }

        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB Set: {}", e.what());
        }
        return false;
    }
}

bool RocksDBCache::BatchSet(
    const std::vector<std::pair<std::string, VersionedData>>& batch) {
    if (!db_ || batch.empty()) {
        return batch.empty();
    }

    try {
        rocksdb::WriteBatch write_batch;
        uint64_t batch_max_version = 0;
        bool has_batch_max_version = false;

        for (const auto& [key, data] : batch) {
            auto serialized = SerializeVersionedData(data);
            write_batch.Put(key, rocksdb::Slice(
                                     reinterpret_cast<const char*>(
                                         serialized.data()),
                                     serialized.size()));

            if (!has_batch_max_version || data.version > batch_max_version) {
                batch_max_version = data.version;
                has_batch_max_version = true;
            }
        }

        rocksdb::Status status =
            db_->Write(rocksdb::WriteOptions(), &write_batch);

        if (!status.ok()) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->error("RocksDB BatchSet failed: {}", status.ToString());
            }
            return false;
        }

        if (has_batch_max_version) {
            uint64_t current_max =
                max_version_.load(std::memory_order_relaxed);
            while (batch_max_version > current_max) {
                if (max_version_.compare_exchange_weak(
                        current_max, batch_max_version,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                    MaybePersistMaxVersion(batch_max_version, false);
                    break;
                }
            }
        }

        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB BatchSet: {}", e.what());
        }
        return false;
    }
}

bool RocksDBCache::SetSync(const std::string& key, const VersionedData& data) {
    if (!db_) {
        return false;
    }

    try {
        auto serialized = SerializeVersionedData(data);

        rocksdb::WriteOptions opts;
        opts.sync = true;
        rocksdb::Status status = db_->Put(opts, key,
                                          rocksdb::Slice(
                                              reinterpret_cast<const char*>(
                                                  serialized.data()),
                                              serialized.size()));

        if (!status.ok()) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->error("RocksDB SetSync failed for key {}: {}", key,
                             status.ToString());
            }
            return false;
        }

        uint64_t current_max = max_version_.load(std::memory_order_relaxed);
        while (data.version > current_max) {
            if (max_version_.compare_exchange_weak(
                    current_max, data.version,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                MaybePersistMaxVersion(data.version, false);
                break;
            }
        }

        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB SetSync: {}", e.what());
        }
        return false;
    }
}

size_t RocksDBCache::ForEach(IteratorCallback cb) {
    if (!db_ || !cb) {
        return 0;
    }

    size_t count = 0;
    try {
        std::unique_ptr<rocksdb::Iterator> it(
            db_->NewIterator(rocksdb::ReadOptions()));

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            std::string key = it->key().ToString();

            // Skip internal keys.
            if (key == GetMaxVersionKey()) {
                continue;
            }

            VersionedData data;
            if (!DeserializeVersionedData(it->value(), data)) {
                continue;
            }

            ++count;
            if (!cb(key, data)) {
                break;
            }
        }
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB ForEach: {}", e.what());
        }
    }

    return count;
}

bool RocksDBCache::Delete(const std::string& key) {
    if (!db_) {
        return false;
    }

    rocksdb::Status status =
        db_->Delete(rocksdb::WriteOptions(), key);
    return status.ok();
}

bool RocksDBCache::UpdateMaxVersion(uint64_t version) {
    if (!db_) {
        return false;
    }

    try {
        std::string version_str(sizeof(uint64_t), '\0');
        std::memcpy(version_str.data(), &version, sizeof(uint64_t));

        rocksdb::Status status = db_->Put(rocksdb::WriteOptions(),
                                          GetMaxVersionKey(), version_str);
        if (status.ok()) {
            persisted_max_version_.store(version, std::memory_order_release);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in UpdateMaxVersion: {}", e.what());
        }
        return false;
    }
}

void RocksDBCache::MaybePersistMaxVersion(uint64_t version, bool force) {
    if (!db_) {
        return;
    }

    const uint64_t persist_step = std::max<uint64_t>(
        1, config_.max_version_persist_step);

    uint64_t persisted =
        persisted_max_version_.load(std::memory_order_acquire);
    if (version <= persisted) {
        return;
    }
    if (!force && (version - persisted) < persist_step) {
        return;
    }

    std::lock_guard<std::mutex> lock(max_version_persist_mutex_);

    persisted = persisted_max_version_.load(std::memory_order_acquire);
    if (version <= persisted) {
        return;
    }
    if (!force && (version - persisted) < persist_step) {
        return;
    }

    if (!UpdateMaxVersion(version)) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("Failed to persist max version {}", version);
        }
    }
}

uint64_t RocksDBCache::GetMaxVersion() const {
    return max_version_.load(std::memory_order_acquire);
}

size_t RocksDBCache::GetApproximateSizeBytes() const {
    if (!db_) {
        return 0;
    }

    uint64_t live_data_size = 0;
    if (db_->GetIntProperty("rocksdb.estimate-live-data-size",
                            &live_data_size)) {
        return static_cast<size_t>(live_data_size);
    }

    std::string value;
    if (db_->GetProperty("rocksdb.estimate-live-data-size", &value)) {
        try {
            return static_cast<size_t>(std::stoull(value));
        } catch (const std::exception&) {
            return 0;
        }
    }
    return 0;
}

std::string RocksDBCache::GetDBStats() {
    if (!db_) {
        return "Database not initialized";
    }

    std::string stats;
    db_->GetProperty("rocksdb.stats", &stats);
    return stats;
}

std::vector<uint8_t> RocksDBCache::SerializeVersionedData(
    const VersionedData& data) const {
    std::vector<uint8_t> result;

    constexpr size_t header_size = 8 + 8 + 4;
    size_t data_size = data.data.size();
    size_t total_size = header_size + data_size;

    result.reserve(total_size);

    uint64_t version = data.version;
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&version),
                  reinterpret_cast<const uint8_t*>(&version) +
                      sizeof(uint64_t));

    uint64_t timestamp = data.timestamp_ms;
    result.insert(result.end(),
                  reinterpret_cast<const uint8_t*>(&timestamp),
                  reinterpret_cast<const uint8_t*>(&timestamp) +
                      sizeof(uint64_t));

    uint32_t size_u32 = static_cast<uint32_t>(data_size);
    result.insert(result.end(),
                  reinterpret_cast<const uint8_t*>(&size_u32),
                  reinterpret_cast<const uint8_t*>(&size_u32) +
                      sizeof(uint32_t));

    if (!data.data.empty()) {
        result.insert(result.end(), data.data.begin(), data.data.end());
    }

    return result;
}

bool RocksDBCache::DeserializeVersionedData(const rocksdb::Slice& data,
                                            VersionedData& result) {
    constexpr size_t header_size = 8 + 8 + 4;
    if (data.size() < header_size) {
        return false;
    }

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());

    std::memcpy(&result.version, ptr, sizeof(uint64_t));
    ptr += sizeof(uint64_t);

    std::memcpy(&result.timestamp_ms, ptr, sizeof(uint64_t));
    ptr += sizeof(uint64_t);

    uint32_t size_u32 = 0;
    std::memcpy(&size_u32, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    size_t data_size = static_cast<size_t>(size_u32);
    if (header_size + data_size != data.size()) {
        return false;
    }

    if (data_size > 0) {
        result.data.assign(ptr, ptr + data_size);
    } else {
        result.data.clear();
    }

    return true;
}

}  // namespace mir2::storage_engine::l2
