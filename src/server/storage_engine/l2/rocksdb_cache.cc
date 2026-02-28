#include <storage_engine/l2/rocksdb_cache.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "rocksdb/filter_policy.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "rocksdb/utilities/db_ttl.h"

namespace mir2::storage_engine::l2 {

RocksDBCache::RocksDBCache(const Config& config) : config_(config) {}

RocksDBCache::~RocksDBCache() {
    MaybePersistMaxVersion(max_version_.load(std::memory_order_acquire), true);
    DestroyColumnFamilies();
    db_.reset();
    block_cache_.reset();
    statistics_.reset();
}

bool RocksDBCache::Initialize() {
    auto logger = spdlog::get("mir2");

    try {
        constexpr size_t kDefaultLegacyWriteBufferSize = 64 * 1024 * 1024;
        const size_t data_write_buffer_size =
            config_.data_write_buffer_size > 0
                ? config_.data_write_buffer_size
                : (config_.write_buffer_size > 0
                       ? config_.write_buffer_size
                       : kDefaultLegacyWriteBufferSize);
        const size_t meta_write_buffer_size =
            config_.meta_write_buffer_size > 0
                ? config_.meta_write_buffer_size
                : std::max<size_t>(4 * 1024 * 1024,
                                   data_write_buffer_size / 8);
        const int data_max_write_buffer_number =
            std::max(2, config_.data_max_write_buffer_number);
        const int meta_max_write_buffer_number =
            std::max(2, config_.meta_max_write_buffer_number);

        rocksdb::Options options;
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
        table_options.block_size = std::max<uint32_t>(1024, config_.block_size);

        options.table_factory.reset(
            rocksdb::NewBlockBasedTableFactory(table_options));

        options.compression = rocksdb::kLZ4Compression;
        options.IncreaseParallelism(
            std::max(1u, std::thread::hardware_concurrency()));
        options.OptimizeLevelStyleCompaction(data_write_buffer_size);
        if (config_.enable_statistics) {
            statistics_ = rocksdb::CreateDBStatistics();
            statistics_->set_stats_level(rocksdb::StatsLevel::kAll);
            options.statistics = statistics_;
        }

        rocksdb::DBOptions db_options(options);
        db_options.create_if_missing = true;
        db_options.create_missing_column_families = true;
        db_options.max_background_jobs = std::max(1, config_.max_background_jobs);
        db_options.max_background_flushes =
            std::max(1, config_.max_background_flushes);

        rocksdb::ColumnFamilyOptions data_cf_options(options);
        data_cf_options.write_buffer_size = data_write_buffer_size;
        data_cf_options.max_write_buffer_number = data_max_write_buffer_number;

        rocksdb::ColumnFamilyOptions ttl_cf_options(data_cf_options);
        ttl_cf_options.periodic_compaction_seconds =
            static_cast<uint64_t>(config_.ttl_periodic_compaction_seconds);

        rocksdb::ColumnFamilyOptions meta_cf_options(options);
        meta_cf_options.write_buffer_size = meta_write_buffer_size;
        meta_cf_options.max_write_buffer_number = meta_max_write_buffer_number;

        std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptors;
        cf_descriptors.reserve(kColumnFamilyCount);
        cf_descriptors.emplace_back(rocksdb::kDefaultColumnFamilyName,
                                    meta_cf_options);
        cf_descriptors.emplace_back(GetDataPersistentCFName(), data_cf_options);
        cf_descriptors.emplace_back(GetDataTtlCFName(), ttl_cf_options);
        cf_descriptors.emplace_back(GetOutboxCFName(), meta_cf_options);
        cf_descriptors.emplace_back(GetDeadLetterCFName(), meta_cf_options);
        cf_descriptors.emplace_back(GetMetaCFName(), meta_cf_options);

        std::vector<int32_t> ttls{
            config_.ttl_seconds,  // default CF (current active data path)
            0,                    // cf_data_persistent
            config_.ttl_seconds,  // cf_data_ttl
            0,                    // cf_outbox
            0,                    // cf_dead_letter
            0                     // cf_meta
        };

        std::vector<rocksdb::ColumnFamilyHandle*> handles;
        handles.reserve(kColumnFamilyCount);
        rocksdb::DBWithTTL* raw_db = nullptr;
        rocksdb::Status status = rocksdb::DBWithTTL::Open(
            db_options, config_.db_path, cf_descriptors,
            &handles, &raw_db, ttls, false);

        if (!status.ok()) {
            if (raw_db != nullptr) {
                for (auto* handle : handles) {
                    if (handle != nullptr) {
                        raw_db->DestroyColumnFamilyHandle(handle);
                    }
                }
                delete raw_db;
            }
            if (logger) {
                logger->error("Failed to open RocksDB: {}", status.ToString());
            }
            return false;
        }

        db_.reset(raw_db);
        if (!AssignColumnFamilies(std::move(handles))) {
            if (logger) {
                logger->error("Failed to assign RocksDB column family handles");
            }
            DestroyColumnFamilies();
            db_.reset();
            return false;
        }

        if (!WriteSchemaVersionMarker()) {
            if (logger) {
                logger->error("Failed to persist RocksDB schema version marker");
            }
            DestroyColumnFamilies();
            db_.reset();
            return false;
        }
        if (!LoadOutboxNextId()) {
            if (logger) {
                logger->error("Failed to load outbox next id");
            }
            DestroyColumnFamilies();
            db_.reset();
            return false;
        }
        if (!LoadDeadLetterNextId()) {
            if (logger) {
                logger->error("Failed to load dead letter next id");
            }
            DestroyColumnFamilies();
            db_.reset();
            return false;
        }

        if (logger) {
            logger->info(
                "RocksDB initialized: path={}, ttl={}s, cf_count={}, "
                "block_cache={}MB, strict_ttl_reads={}, scan_fill_cache={}, "
                "iter_pin_data={}, isolate_foreach_scan_reader={}, "
                "statistics_enabled={}",
                config_.db_path, config_.ttl_seconds,
                cf_handles_.size(),
                config_.block_cache_size / (1024 * 1024),
                config_.strict_ttl_reads,
                config_.scan_fill_cache,
                config_.iter_pin_data,
                config_.isolate_foreach_scan_reader,
                config_.enable_statistics);
        }

        std::string max_version_str;
        status = db_->Get(rocksdb::ReadOptions(), meta_cf_,
                          GetMaxVersionKey(),
                          &max_version_str);
        if (status.IsNotFound() && default_cf_ != nullptr) {
            // Backward compatibility: migrate pre-multi-CF max_version marker.
            rocksdb::Status legacy_status =
                db_->Get(rocksdb::ReadOptions(), default_cf_,
                         GetMaxVersionKey(), &max_version_str);
            if (legacy_status.ok()) {
                db_->Put(rocksdb::WriteOptions(), meta_cf_,
                         GetMaxVersionKey(), max_version_str);
                db_->Delete(rocksdb::WriteOptions(), default_cf_,
                            GetMaxVersionKey());
                status = legacy_status;
            } else {
                status = legacy_status;
            }
        }
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

bool RocksDBCache::AssignColumnFamilies(
    std::vector<rocksdb::ColumnFamilyHandle*> handles) {
    if (handles.size() != kColumnFamilyCount) {
        return false;
    }

    cf_handles_ = std::move(handles);
    default_cf_ = cf_handles_[kDefaultCFIndex];
    data_persistent_cf_ = cf_handles_[kDataPersistentCFIndex];
    data_ttl_cf_ = cf_handles_[kDataTtlCFIndex];
    outbox_cf_ = cf_handles_[kOutboxCFIndex];
    dead_letter_cf_ = cf_handles_[kDeadLetterCFIndex];
    meta_cf_ = cf_handles_[kMetaCFIndex];
    return default_cf_ != nullptr && data_persistent_cf_ != nullptr &&
           data_ttl_cf_ != nullptr && outbox_cf_ != nullptr &&
           dead_letter_cf_ != nullptr && meta_cf_ != nullptr;
}

void RocksDBCache::DestroyColumnFamilies() {
    if (!db_) {
        cf_handles_.clear();
        default_cf_ = nullptr;
        data_persistent_cf_ = nullptr;
        data_ttl_cf_ = nullptr;
        outbox_cf_ = nullptr;
        dead_letter_cf_ = nullptr;
        meta_cf_ = nullptr;
        return;
    }

    auto logger = spdlog::get("mir2");
    for (auto* handle : cf_handles_) {
        if (!handle) {
            continue;
        }
        rocksdb::Status status = db_->DestroyColumnFamilyHandle(handle);
        if (!status.ok() && logger) {
            logger->warn("DestroyColumnFamilyHandle failed: {}",
                         status.ToString());
        }
    }

    cf_handles_.clear();
    default_cf_ = nullptr;
    data_persistent_cf_ = nullptr;
    data_ttl_cf_ = nullptr;
    outbox_cf_ = nullptr;
    dead_letter_cf_ = nullptr;
    meta_cf_ = nullptr;
}

bool RocksDBCache::WriteSchemaVersionMarker() {
    if (!db_ || !meta_cf_) {
        return false;
    }
    rocksdb::Status status = db_->Put(
        rocksdb::WriteOptions(), meta_cf_,
        GetSchemaVersionKey(), GetSchemaVersionValue());
    return status.ok();
}

bool RocksDBCache::LoadOutboxNextId() {
    if (!db_ || !outbox_cf_ || !meta_cf_) {
        return false;
    }

    uint64_t loaded_next_id = 1;
    std::string raw_next_id;
    rocksdb::Status status = db_->Get(
        rocksdb::ReadOptions(), meta_cf_, GetOutboxNextIdKey(), &raw_next_id);
    if (status.ok()) {
        uint64_t parsed = 0;
        if (!DecodeUint64FromSlice(rocksdb::Slice(raw_next_id), &parsed) ||
            parsed == 0) {
            return false;
        }
        loaded_next_id = parsed;
    } else if (status.IsNotFound()) {
        uint64_t max_seen_id = 0;
        std::unique_ptr<rocksdb::Iterator> it(
            db_->NewIterator(BuildScanReadOptions(), outbox_cf_));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            uint64_t outbox_id = 0;
            if (ParseOutboxStorageKey(it->key(), &outbox_id) &&
                outbox_id > max_seen_id) {
                max_seen_id = outbox_id;
            }
        }
        loaded_next_id = max_seen_id + 1;
        if (!PersistOutboxNextIdLocked(loaded_next_id)) {
            return false;
        }
    } else {
        return false;
    }

    std::lock_guard<std::mutex> lock(outbox_mutex_);
    outbox_next_id_ = loaded_next_id;
    return true;
}

bool RocksDBCache::LoadDeadLetterNextId() {
    if (!db_ || !dead_letter_cf_ || !meta_cf_) {
        return false;
    }

    uint64_t loaded_next_id = 1;
    std::string raw_next_id;
    rocksdb::Status status = db_->Get(
        rocksdb::ReadOptions(), meta_cf_, GetDeadLetterNextIdKey(),
        &raw_next_id);
    if (status.ok()) {
        uint64_t parsed = 0;
        if (!DecodeUint64FromSlice(rocksdb::Slice(raw_next_id), &parsed) ||
            parsed == 0) {
            return false;
        }
        loaded_next_id = parsed;
    } else if (status.IsNotFound()) {
        uint64_t max_seen_id = 0;
        std::unique_ptr<rocksdb::Iterator> it(
            db_->NewIterator(BuildScanReadOptions(), dead_letter_cf_));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            uint64_t dead_letter_id = 0;
            if (ParseDeadLetterStorageKey(it->key(), &dead_letter_id) &&
                dead_letter_id > max_seen_id) {
                max_seen_id = dead_letter_id;
            }
        }
        loaded_next_id = max_seen_id + 1;
        if (!PersistDeadLetterNextIdLocked(loaded_next_id)) {
            return false;
        }
    } else {
        return false;
    }

    std::lock_guard<std::mutex> lock(dead_letter_mutex_);
    dead_letter_next_id_ = loaded_next_id;
    return true;
}

bool RocksDBCache::PersistOutboxNextIdLocked(uint64_t next_id) {
    if (!db_ || !meta_cf_ || next_id == 0) {
        return false;
    }
    rocksdb::Status status = db_->Put(
        rocksdb::WriteOptions(), meta_cf_, GetOutboxNextIdKey(),
        EncodeUint64ToString(next_id));
    return status.ok();
}

bool RocksDBCache::PersistDeadLetterNextIdLocked(uint64_t next_id) {
    if (!db_ || !meta_cf_ || next_id == 0) {
        return false;
    }
    rocksdb::Status status = db_->Put(
        rocksdb::WriteOptions(), meta_cf_, GetDeadLetterNextIdKey(),
        EncodeUint64ToString(next_id));
    return status.ok();
}

rocksdb::ColumnFamilyHandle* RocksDBCache::ResolveDataColumnFamily(
    DataTier tier) const {
    return tier == DataTier::kPersistent ? data_persistent_cf_ : data_ttl_cf_;
}

rocksdb::ColumnFamilyHandle* RocksDBCache::ResolvePeerDataColumnFamily(
    DataTier tier) const {
    return tier == DataTier::kPersistent ? data_ttl_cf_ : data_persistent_cf_;
}

std::optional<VersionedData> RocksDBCache::GetFromColumnFamily(
    const std::string& key,
    rocksdb::ColumnFamilyHandle* cf) {
    if (!db_ || cf == nullptr) {
        return std::nullopt;
    }

    rocksdb::PinnableSlice pinnable_val;
    rocksdb::Status status = db_->Get(BuildPointReadOptions(), cf, key,
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
    if (!DeserializeVersionedData(rocksdb::Slice(pinnable_val), result)) {
        return std::nullopt;
    }
    return result;
}

bool RocksDBCache::PutToDataColumnFamily(const std::string& key,
                                         const VersionedData& data,
                                         DataTier tier,
                                         bool sync) {
    if (!db_) {
        return false;
    }
    auto* target_cf = ResolveDataColumnFamily(tier);
    if (target_cf == nullptr) {
        return false;
    }

    const auto serialized = SerializeVersionedData(data);
    rocksdb::WriteBatch write_batch;
    write_batch.Put(target_cf, key,
                    rocksdb::Slice(
                        reinterpret_cast<const char*>(serialized.data()),
                        serialized.size()));

    if (auto* peer_cf = ResolvePeerDataColumnFamily(tier); peer_cf != nullptr) {
        write_batch.Delete(peer_cf, key);
    }
    if (default_cf_ != nullptr) {
        // Clear legacy/default slot to preserve strict tier placement.
        write_batch.Delete(default_cf_, key);
    }

    rocksdb::WriteOptions opts;
    opts.sync = sync;
    rocksdb::Status status = db_->Write(opts, &write_batch);
    if (!status.ok()) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("RocksDB write failed for key {}: {}", key,
                          status.ToString());
        }
        return false;
    }

    return UpdateMaxVersionCache(data.version);
}

bool RocksDBCache::UpdateMaxVersionCache(uint64_t version) {
    uint64_t current_max = max_version_.load(std::memory_order_relaxed);
    while (version > current_max) {
        if (max_version_.compare_exchange_weak(
                current_max, version,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            MaybePersistMaxVersion(version, false);
            break;
        }
    }
    return true;
}

std::string RocksDBCache::EncodeUint64ToString(uint64_t value) const {
    std::string out(sizeof(uint64_t), '\0');
    std::memcpy(out.data(), &value, sizeof(uint64_t));
    return out;
}

bool RocksDBCache::DecodeUint64FromSlice(const rocksdb::Slice& value,
                                         uint64_t* out) const {
    if (out == nullptr || value.size() != sizeof(uint64_t)) {
        return false;
    }
    std::memcpy(out, value.data(), sizeof(uint64_t));
    return true;
}

rocksdb::ReadOptions RocksDBCache::BuildPointReadOptions() const {
    rocksdb::ReadOptions opts;
    return opts;
}

rocksdb::ReadOptions RocksDBCache::BuildScanReadOptions() const {
    rocksdb::ReadOptions opts;
    opts.fill_cache = config_.scan_fill_cache;
    // Avoid pinning scan iterator data when scan fill-cache is disabled.
    // This keeps long scans from perturbing hot block residency.
    opts.pin_data = config_.scan_fill_cache ? config_.iter_pin_data : false;
    return opts;
}

bool RocksDBCache::IsStrictTtlExpired(const VersionedData& data) const {
    if (!config_.strict_ttl_reads || config_.ttl_seconds <= 0 ||
        data.timestamp_ms == 0) {
        return false;
    }

    const uint64_t now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    const uint64_t ttl_ms = static_cast<uint64_t>(config_.ttl_seconds) * 1000ULL;
    if (now_ms < data.timestamp_ms) {
        return false;
    }
    return (now_ms - data.timestamp_ms) >= ttl_ms;
}

std::string RocksDBCache::MakeOutboxStorageKey(uint64_t outbox_id) const {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s%020llu",
                  GetOutboxStoragePrefix().c_str(),
                  static_cast<unsigned long long>(outbox_id));
    return std::string(buffer);
}

std::string RocksDBCache::MakeDeadLetterStorageKey(
    uint64_t dead_letter_id) const {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s%020llu",
                  GetDeadLetterStoragePrefix().c_str(),
                  static_cast<unsigned long long>(dead_letter_id));
    return std::string(buffer);
}

bool RocksDBCache::ParseOutboxStorageKey(const rocksdb::Slice& storage_key,
                                         uint64_t* outbox_id) const {
    if (outbox_id == nullptr) {
        return false;
    }
    const std::string prefix = GetOutboxStoragePrefix();
    if (storage_key.size() <= prefix.size()) {
        return false;
    }
    if (std::memcmp(storage_key.data(), prefix.data(), prefix.size()) != 0) {
        return false;
    }
    const std::string id_str(storage_key.data() + prefix.size(),
                             storage_key.size() - prefix.size());
    try {
        *outbox_id = std::stoull(id_str);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RocksDBCache::ParseDeadLetterStorageKey(
    const rocksdb::Slice& storage_key,
    uint64_t* dead_letter_id) const {
    if (dead_letter_id == nullptr) {
        return false;
    }
    const std::string prefix = GetDeadLetterStoragePrefix();
    if (storage_key.size() <= prefix.size()) {
        return false;
    }
    if (std::memcmp(storage_key.data(), prefix.data(), prefix.size()) != 0) {
        return false;
    }
    const std::string id_str(storage_key.data() + prefix.size(),
                             storage_key.size() - prefix.size());
    try {
        *dead_letter_id = std::stoull(id_str);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<uint8_t> RocksDBCache::SerializeOutboxValue(
    const std::string& key,
    const VersionedData& data,
    Priority priority) const {
    const auto payload = SerializeVersionedData(data);
    const uint32_t key_size = static_cast<uint32_t>(key.size());
    const uint32_t payload_size = static_cast<uint32_t>(payload.size());
    const size_t total_size =
        sizeof(uint32_t) + key_size + sizeof(uint8_t) + sizeof(uint32_t) + payload_size;

    std::vector<uint8_t> out;
    out.reserve(total_size);
    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&key_size),
               reinterpret_cast<const uint8_t*>(&key_size) + sizeof(uint32_t));
    out.insert(out.end(), key.begin(), key.end());
    const uint8_t priority_raw = static_cast<uint8_t>(priority);
    out.push_back(priority_raw);
    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&payload_size),
               reinterpret_cast<const uint8_t*>(&payload_size) + sizeof(uint32_t));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> RocksDBCache::SerializeDeadLetterValue(
    const DeadLetterEntry& entry) const {
    const auto payload = SerializeVersionedData(entry.data);
    const uint32_t key_size = static_cast<uint32_t>(entry.key.size());
    const uint32_t error_size =
        static_cast<uint32_t>(entry.error_message.size());
    const uint32_t payload_size = static_cast<uint32_t>(payload.size());
    const size_t total_size =
        sizeof(uint32_t) + key_size + sizeof(uint8_t) + sizeof(uint32_t) +
        sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint32_t) + error_size +
        sizeof(uint32_t) + payload_size;

    std::vector<uint8_t> out;
    out.reserve(total_size);
    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&key_size),
               reinterpret_cast<const uint8_t*>(&key_size) +
                   sizeof(uint32_t));
    out.insert(out.end(), entry.key.begin(), entry.key.end());

    const uint8_t priority_raw = static_cast<uint8_t>(entry.priority);
    out.push_back(priority_raw);

    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&entry.attempts),
               reinterpret_cast<const uint8_t*>(&entry.attempts) +
                   sizeof(uint32_t));
    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&entry.durable_outbox_id),
               reinterpret_cast<const uint8_t*>(&entry.durable_outbox_id) +
                   sizeof(uint64_t));
    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&entry.recorded_at_ms),
               reinterpret_cast<const uint8_t*>(&entry.recorded_at_ms) +
                   sizeof(uint64_t));

    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&error_size),
               reinterpret_cast<const uint8_t*>(&error_size) +
                   sizeof(uint32_t));
    out.insert(out.end(), entry.error_message.begin(), entry.error_message.end());

    out.insert(out.end(),
               reinterpret_cast<const uint8_t*>(&payload_size),
               reinterpret_cast<const uint8_t*>(&payload_size) +
                   sizeof(uint32_t));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool RocksDBCache::DeserializeOutboxValue(const rocksdb::Slice& value,
                                          OutboxEntry* entry) {
    if (entry == nullptr) {
        return false;
    }
    constexpr size_t kMinHeaderSize = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
    if (value.size() < kMinHeaderSize) {
        return false;
    }

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(value.data());
    const uint8_t* end = cursor + value.size();

    uint32_t key_size = 0;
    std::memcpy(&key_size, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    if (key_size > static_cast<uint32_t>(end - cursor)) {
        return false;
    }
    entry->key.assign(reinterpret_cast<const char*>(cursor), key_size);
    cursor += key_size;

    if (cursor >= end) {
        return false;
    }
    const uint8_t priority_raw = *cursor++;
    if (priority_raw > static_cast<uint8_t>(Priority::CRITICAL)) {
        return false;
    }
    entry->priority = static_cast<Priority>(priority_raw);

    if (static_cast<size_t>(end - cursor) < sizeof(uint32_t)) {
        return false;
    }
    uint32_t payload_size = 0;
    std::memcpy(&payload_size, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    if (payload_size != static_cast<uint32_t>(end - cursor)) {
        return false;
    }

    VersionedData decoded;
    if (!DeserializeVersionedData(
            rocksdb::Slice(reinterpret_cast<const char*>(cursor), payload_size),
            decoded)) {
        return false;
    }
    entry->data = std::move(decoded);
    return true;
}

bool RocksDBCache::DeserializeDeadLetterValue(const rocksdb::Slice& value,
                                              DeadLetterEntry* entry) {
    if (entry == nullptr) {
        return false;
    }
    constexpr size_t kMinHeaderSize =
        sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) +
        sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint32_t) +
        sizeof(uint32_t);
    if (value.size() < kMinHeaderSize) {
        return false;
    }

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(value.data());
    const uint8_t* end = cursor + value.size();

    uint32_t key_size = 0;
    std::memcpy(&key_size, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    if (key_size > static_cast<uint32_t>(end - cursor)) {
        return false;
    }
    entry->key.assign(reinterpret_cast<const char*>(cursor), key_size);
    cursor += key_size;

    if (cursor >= end) {
        return false;
    }
    const uint8_t priority_raw = *cursor++;
    if (priority_raw > static_cast<uint8_t>(Priority::CRITICAL)) {
        return false;
    }
    entry->priority = static_cast<Priority>(priority_raw);

    if (static_cast<size_t>(end - cursor) <
        sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t)) {
        return false;
    }
    std::memcpy(&entry->attempts, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    std::memcpy(&entry->durable_outbox_id, cursor, sizeof(uint64_t));
    cursor += sizeof(uint64_t);
    std::memcpy(&entry->recorded_at_ms, cursor, sizeof(uint64_t));
    cursor += sizeof(uint64_t);

    if (static_cast<size_t>(end - cursor) < sizeof(uint32_t)) {
        return false;
    }
    uint32_t error_size = 0;
    std::memcpy(&error_size, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    if (error_size > static_cast<uint32_t>(end - cursor)) {
        return false;
    }
    entry->error_message.assign(reinterpret_cast<const char*>(cursor), error_size);
    cursor += error_size;

    if (static_cast<size_t>(end - cursor) < sizeof(uint32_t)) {
        return false;
    }
    uint32_t payload_size = 0;
    std::memcpy(&payload_size, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    if (payload_size != static_cast<uint32_t>(end - cursor)) {
        return false;
    }

    VersionedData decoded;
    if (!DeserializeVersionedData(
            rocksdb::Slice(reinterpret_cast<const char*>(cursor), payload_size),
            decoded)) {
        return false;
    }
    entry->data = std::move(decoded);
    return true;
}

bool RocksDBCache::AppendOutbox(const std::string& key,
                                const VersionedData& data,
                                Priority priority,
                                uint64_t* outbox_id) {
    if (!db_ || !outbox_cf_ || !meta_cf_) {
        return false;
    }
    if (key.empty() ||
        key.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }

    const auto serialized = SerializeOutboxValue(key, data, priority);
    std::lock_guard<std::mutex> lock(outbox_mutex_);
    const uint64_t assigned_id = outbox_next_id_;
    const uint64_t next_id = assigned_id + 1;

    rocksdb::WriteBatch batch;
    batch.Put(outbox_cf_, MakeOutboxStorageKey(assigned_id),
              rocksdb::Slice(reinterpret_cast<const char*>(serialized.data()),
                             serialized.size()));
    batch.Put(meta_cf_, GetOutboxNextIdKey(), EncodeUint64ToString(next_id));

    rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!status.ok()) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("AppendOutbox failed for key {}: {}", key,
                         status.ToString());
        }
        return false;
    }

    outbox_next_id_ = next_id;
    if (outbox_id != nullptr) {
        *outbox_id = assigned_id;
    }
    return true;
}

bool RocksDBCache::AckOutbox(uint64_t outbox_id) {
    if (!db_ || !outbox_cf_ || outbox_id == 0) {
        return false;
    }
    rocksdb::Status status = db_->Delete(
        rocksdb::WriteOptions(), outbox_cf_, MakeOutboxStorageKey(outbox_id));
    if (!status.ok()) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("AckOutbox failed id={}: {}", outbox_id,
                         status.ToString());
        }
    }
    return status.ok();
}

size_t RocksDBCache::ReplayOutbox(
    size_t limit,
    const std::function<bool(const OutboxEntry&)>& cb) {
    if (!db_ || !outbox_cf_ || !cb) {
        return 0;
    }

    size_t replayed = 0;
    std::unique_ptr<rocksdb::Iterator> it(
        db_->NewIterator(BuildScanReadOptions(), outbox_cf_));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        if (limit > 0 && replayed >= limit) {
            break;
        }

        OutboxEntry entry;
        if (!ParseOutboxStorageKey(it->key(), &entry.outbox_id)) {
            continue;
        }
        if (!DeserializeOutboxValue(it->value(), &entry)) {
            continue;
        }

        ++replayed;
        if (!cb(entry)) {
            break;
        }
    }
    return replayed;
}

size_t RocksDBCache::OutboxDepth() const {
    if (!db_ || !outbox_cf_) {
        return 0;
    }

    size_t depth = 0;
    std::unique_ptr<rocksdb::Iterator> it(
        db_->NewIterator(BuildScanReadOptions(), outbox_cf_));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        ++depth;
    }
    return depth;
}

bool RocksDBCache::AppendDeadLetter(const DeadLetterEntry& entry,
                                    uint64_t* dead_letter_id) {
    if (!db_ || !dead_letter_cf_ || !meta_cf_) {
        return false;
    }
    if (entry.key.empty() ||
        entry.key.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) ||
        entry.error_message.size() >
            static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    if (entry.recorded_at_ms == 0) {
        return false;
    }

    const auto serialized = SerializeDeadLetterValue(entry);
    std::lock_guard<std::mutex> lock(dead_letter_mutex_);
    const uint64_t assigned_id = dead_letter_next_id_;
    const uint64_t next_id = assigned_id + 1;

    rocksdb::WriteBatch batch;
    batch.Put(dead_letter_cf_, MakeDeadLetterStorageKey(assigned_id),
              rocksdb::Slice(reinterpret_cast<const char*>(serialized.data()),
                             serialized.size()));
    batch.Put(meta_cf_, GetDeadLetterNextIdKey(), EncodeUint64ToString(next_id));

    rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!status.ok()) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("AppendDeadLetter failed for key {}: {}", entry.key,
                         status.ToString());
        }
        return false;
    }

    dead_letter_next_id_ = next_id;
    if (dead_letter_id != nullptr) {
        *dead_letter_id = assigned_id;
    }
    return true;
}

bool RocksDBCache::AckDeadLetter(uint64_t dead_letter_id) {
    if (!db_ || !dead_letter_cf_ || dead_letter_id == 0) {
        return false;
    }
    rocksdb::Status status = db_->Delete(
        rocksdb::WriteOptions(), dead_letter_cf_,
        MakeDeadLetterStorageKey(dead_letter_id));
    if (!status.ok()) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("AckDeadLetter failed id={}: {}", dead_letter_id,
                         status.ToString());
        }
    }
    return status.ok();
}

size_t RocksDBCache::ReplayDeadLetter(
    size_t limit,
    const std::function<bool(const DeadLetterEntry&)>& cb) {
    if (!db_ || !dead_letter_cf_ || !cb) {
        return 0;
    }

    size_t replayed = 0;
    std::unique_ptr<rocksdb::Iterator> it(
        db_->NewIterator(BuildScanReadOptions(), dead_letter_cf_));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        if (limit > 0 && replayed >= limit) {
            break;
        }

        DeadLetterEntry entry;
        if (!ParseDeadLetterStorageKey(it->key(), &entry.dead_letter_id)) {
            continue;
        }
        if (!DeserializeDeadLetterValue(it->value(), &entry)) {
            continue;
        }

        ++replayed;
        if (!cb(entry)) {
            break;
        }
    }
    return replayed;
}

size_t RocksDBCache::DeadLetterDepth() const {
    if (!db_ || !dead_letter_cf_) {
        return 0;
    }

    size_t depth = 0;
    std::unique_ptr<rocksdb::Iterator> it(
        db_->NewIterator(BuildScanReadOptions(), dead_letter_cf_));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        ++depth;
    }
    return depth;
}

std::optional<VersionedData> RocksDBCache::Get(const std::string& key) {
    return Get(key, DataTier::kTtl);
}

std::optional<VersionedData> RocksDBCache::Get(const std::string& key,
                                               DataTier tier) {
    if (!db_) {
        return std::nullopt;
    }

    try {
        auto from_tier = GetFromColumnFamily(key, ResolveDataColumnFamily(tier));
        if (from_tier) {
            if (tier == DataTier::kTtl && IsStrictTtlExpired(*from_tier)) {
                return std::nullopt;
            }
            return from_tier;
        }
        if (default_cf_ != nullptr) {
            auto from_default = GetFromColumnFamily(key, default_cf_);
            if (from_default && tier == DataTier::kTtl &&
                IsStrictTtlExpired(*from_default)) {
                return std::nullopt;
            }
            return from_default;
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
    return Set(key, data, DataTier::kTtl);
}

bool RocksDBCache::Set(const std::string& key, const VersionedData& data,
                       DataTier tier) {
    try {
        return PutToDataColumnFamily(key, data, tier, false);
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
    return BatchSet(batch, DataTier::kTtl);
}

bool RocksDBCache::BatchSet(
    const std::vector<std::pair<std::string, VersionedData>>& batch,
    DataTier tier) {
    if (!db_ || batch.empty()) {
        return batch.empty();
    }
    auto* target_cf = ResolveDataColumnFamily(tier);
    if (target_cf == nullptr) {
        return false;
    }

    try {
        rocksdb::WriteBatch write_batch;
        uint64_t batch_max_version = 0;
        bool has_batch_max_version = false;
        auto* peer_cf = ResolvePeerDataColumnFamily(tier);

        for (const auto& [key, data] : batch) {
            auto serialized = SerializeVersionedData(data);
            write_batch.Put(target_cf, key,
                            rocksdb::Slice(
                                reinterpret_cast<const char*>(serialized.data()),
                                serialized.size()));
            if (peer_cf != nullptr) {
                write_batch.Delete(peer_cf, key);
            }
            if (default_cf_ != nullptr) {
                write_batch.Delete(default_cf_, key);
            }

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
            UpdateMaxVersionCache(batch_max_version);
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
    return SetSync(key, data, DataTier::kTtl);
}

bool RocksDBCache::SetSync(const std::string& key,
                           const VersionedData& data,
                           DataTier tier) {
    try {
        return PutToDataColumnFamily(key, data, tier, true);
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB SetSync: {}", e.what());
        }
        return false;
    }
}

size_t RocksDBCache::ForEach(IteratorCallback cb) {
    return ForEach(std::move(cb), DataTier::kTtl);
}

size_t RocksDBCache::ForEach(IteratorCallback cb, DataTier tier) {
    auto* target_cf = ResolveDataColumnFamily(tier);
    if (!db_ || target_cf == nullptr || !cb) {
        return 0;
    }

    auto iterate_with = [&](rocksdb::DB* db,
                            rocksdb::ColumnFamilyHandle* cf,
                            const rocksdb::ReadOptions& read_options) -> size_t {
        size_t count = 0;
        std::unique_ptr<rocksdb::Iterator> it(
            db->NewIterator(read_options, cf));
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
            if (tier == DataTier::kTtl && IsStrictTtlExpired(data)) {
                continue;
            }

            ++count;
            if (!cb(key, data)) {
                break;
            }
        }
        return count;
    };

    size_t count = 0;
    try {
        if (config_.isolate_foreach_scan_reader && !config_.scan_fill_cache) {
            auto destroy_scan_handles =
                [](rocksdb::DB* db,
                   std::vector<rocksdb::ColumnFamilyHandle*>* handles) {
                    if (db == nullptr || handles == nullptr) {
                        return;
                    }
                    for (auto* handle : *handles) {
                        if (handle != nullptr) {
                            db->DestroyColumnFamilyHandle(handle);
                        }
                    }
                    handles->clear();
                };

            rocksdb::DBOptions scan_db_options;
            scan_db_options.create_if_missing = false;
            scan_db_options.create_missing_column_families = false;
            scan_db_options.max_open_files = -1;

            rocksdb::BlockBasedTableOptions scan_table_options;
            scan_table_options.no_block_cache = true;
            scan_table_options.cache_index_and_filter_blocks = false;
            scan_table_options.pin_l0_filter_and_index_blocks_in_cache = false;
            scan_table_options.block_size =
                std::max<uint32_t>(1024, config_.block_size);

            rocksdb::Options scan_cf_base;
            scan_cf_base.table_factory.reset(
                rocksdb::NewBlockBasedTableFactory(scan_table_options));

            std::vector<rocksdb::ColumnFamilyDescriptor> scan_descriptors;
            scan_descriptors.reserve(kColumnFamilyCount);
            scan_descriptors.emplace_back(rocksdb::kDefaultColumnFamilyName,
                                          scan_cf_base);
            scan_descriptors.emplace_back(GetDataPersistentCFName(), scan_cf_base);
            scan_descriptors.emplace_back(GetDataTtlCFName(), scan_cf_base);
            scan_descriptors.emplace_back(GetOutboxCFName(), scan_cf_base);
            scan_descriptors.emplace_back(GetDeadLetterCFName(), scan_cf_base);
            scan_descriptors.emplace_back(GetMetaCFName(), scan_cf_base);

            std::vector<rocksdb::ColumnFamilyHandle*> scan_handles;
            scan_handles.reserve(kColumnFamilyCount);
            rocksdb::DB* raw_scan_db = nullptr;
            const std::string secondary_path =
                "/tmp/mir2_rocksdb_scan_secondary_" +
                std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()) +
                "_" +
                std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id()));
            std::error_code fs_ec;
            (void)std::filesystem::remove_all(secondary_path, fs_ec);
            (void)std::filesystem::create_directories(secondary_path, fs_ec);

            rocksdb::Status scan_status = rocksdb::DB::OpenAsSecondary(
                scan_db_options, config_.db_path, secondary_path,
                scan_descriptors, &scan_handles, &raw_scan_db);
            if (scan_status.ok() && raw_scan_db != nullptr &&
                scan_handles.size() == kColumnFamilyCount) {
                std::unique_ptr<rocksdb::DB> scan_db(raw_scan_db);
                raw_scan_db = nullptr;
                const rocksdb::Status catch_up_status =
                    scan_db->TryCatchUpWithPrimary();
                if (!catch_up_status.ok()) {
                    auto logger = spdlog::get("mir2");
                    if (logger) {
                        logger->warn("ForEach isolated scan catch-up failed: {}",
                                     catch_up_status.ToString());
                    }
                }
                const uint64_t primary_seq = db_->GetLatestSequenceNumber();
                const uint64_t secondary_seq = scan_db->GetLatestSequenceNumber();
                const bool scan_is_fresh = catch_up_status.ok() &&
                                           secondary_seq >= primary_seq;
                if (scan_is_fresh) {
                    rocksdb::ColumnFamilyHandle* scan_target_cf =
                        tier == DataTier::kPersistent
                            ? scan_handles[kDataPersistentCFIndex]
                            : scan_handles[kDataTtlCFIndex];
                    count = iterate_with(scan_db.get(), scan_target_cf,
                                         BuildScanReadOptions());
                    destroy_scan_handles(scan_db.get(), &scan_handles);
                    (void)std::filesystem::remove_all(secondary_path, fs_ec);
                    return count;
                }
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn(
                        "ForEach isolated scan reader stale (primary_seq={}, "
                        "secondary_seq={}), fallback to main DB iterator",
                        primary_seq, secondary_seq);
                }
                destroy_scan_handles(scan_db.get(), &scan_handles);
                (void)std::filesystem::remove_all(secondary_path, fs_ec);
            } else if (raw_scan_db != nullptr) {
                destroy_scan_handles(raw_scan_db, &scan_handles);
                delete raw_scan_db;
                raw_scan_db = nullptr;
            }

            if (raw_scan_db != nullptr) {
                destroy_scan_handles(raw_scan_db, &scan_handles);
                delete raw_scan_db;
                raw_scan_db = nullptr;
            }
            (void)std::filesystem::remove_all(secondary_path, fs_ec);

            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "ForEach isolated scan reader unavailable, falling back to "
                    "main DB iterator: {}",
                    scan_status.ToString());
            }
        }

        count = iterate_with(db_.get(), target_cf, BuildScanReadOptions());
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB ForEach: {}", e.what());
        }
    }

    return count;
}

size_t RocksDBCache::DeleteByPrefix(const std::string& prefix,
                                    DataTier tier,
                                    size_t batch_size) {
    if (!db_ || prefix.empty()) {
        return 0;
    }

    auto* target_cf = ResolveDataColumnFamily(tier);
    if (target_cf == nullptr) {
        return 0;
    }

    const size_t effective_batch_size = std::max<size_t>(1, batch_size);
    const rocksdb::Slice prefix_slice(prefix);
    size_t deleted = 0;
    size_t batch_items = 0;
    rocksdb::WriteBatch write_batch;
    const auto scan_options = BuildScanReadOptions();

    auto flush_batch = [&]() -> bool {
        if (batch_items == 0) {
            return true;
        }
        rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), &write_batch);
        if (!status.ok()) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->error("DeleteByPrefix flush failed for prefix {}: {}",
                              prefix, status.ToString());
            }
            return false;
        }
        batch_items = 0;
        write_batch.Clear();
        return true;
    };

    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(scan_options, target_cf));
    for (it->Seek(prefix_slice); it->Valid(); it->Next()) {
        const rocksdb::Slice key_slice = it->key();
        if (key_slice.size() < prefix_slice.size() ||
            std::memcmp(key_slice.data(), prefix_slice.data(),
                        prefix_slice.size()) != 0) {
            break;
        }

        std::string key = key_slice.ToString();
        write_batch.Delete(target_cf, key);
        if (auto* peer_cf = ResolvePeerDataColumnFamily(tier); peer_cf != nullptr) {
            write_batch.Delete(peer_cf, key);
        }
        if (default_cf_ != nullptr) {
            write_batch.Delete(default_cf_, key);
        }
        ++deleted;
        ++batch_items;

        if (batch_items >= effective_batch_size && !flush_batch()) {
            break;
        }
    }

    if (!flush_batch()) {
        return deleted - batch_items;
    }
    return deleted;
}

bool RocksDBCache::Delete(const std::string& key) {
    return Delete(key, DataTier::kTtl);
}

bool RocksDBCache::Delete(const std::string& key, DataTier tier) {
    if (!db_) {
        return false;
    }
    auto* target_cf = ResolveDataColumnFamily(tier);
    if (target_cf == nullptr) {
        return false;
    }

    rocksdb::WriteBatch write_batch;
    write_batch.Delete(target_cf, key);
    if (auto* peer_cf = ResolvePeerDataColumnFamily(tier); peer_cf != nullptr) {
        write_batch.Delete(peer_cf, key);
    }
    if (default_cf_ != nullptr) {
        write_batch.Delete(default_cf_, key);
    }

    rocksdb::Status status =
        db_->Write(rocksdb::WriteOptions(), &write_batch);
    return status.ok();
}

bool RocksDBCache::UpdateMaxVersion(uint64_t version) {
    if (!db_ || !meta_cf_) {
        return false;
    }

    try {
        std::string version_str(sizeof(uint64_t), '\0');
        std::memcpy(version_str.data(), &version, sizeof(uint64_t));

        rocksdb::Status status = db_->Put(rocksdb::WriteOptions(),
                                          meta_cf_, GetMaxVersionKey(), version_str);
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
    uint64_t live_data_size = 0;
    if (!GetUInt64Property("rocksdb.estimate-live-data-size", &live_data_size)) {
        return 0;
    }
    return static_cast<size_t>(live_data_size);
}

std::string RocksDBCache::GetDBStats() {
    if (!db_) {
        return "Database not initialized";
    }

    std::string stats;
    db_->GetProperty("rocksdb.stats", &stats);
    return stats;
}

bool RocksDBCache::GetUInt64Property(const std::string& property,
                                     uint64_t* out) const {
    if (!db_ || out == nullptr) {
        return false;
    }

    std::optional<uint64_t> ticker_value;
    if (statistics_ != nullptr) {
        if (property == "rocksdb.block-cache-hit") {
            ticker_value = statistics_->getTickerCount(rocksdb::BLOCK_CACHE_HIT);
        }
        if (property == "rocksdb.block-cache-miss") {
            ticker_value = statistics_->getTickerCount(rocksdb::BLOCK_CACHE_MISS);
        }
    }

    const rocksdb::Slice property_slice(property);
    uint64_t int_value = 0;
    // For multi-CF deployments, aggregated int properties better reflect
    // real load than default-CF-only properties.
    if (db_->GetAggregatedIntProperty(property_slice, &int_value)) {
        *out = ticker_value.has_value()
                   ? std::max(*ticker_value, int_value)
                   : int_value;
        return true;
    }
    if (db_->GetIntProperty(property_slice, &int_value)) {
        *out = ticker_value.has_value()
                   ? std::max(*ticker_value, int_value)
                   : int_value;
        return true;
    }
    if (ticker_value.has_value()) {
        *out = *ticker_value;
        return true;
    }

    std::string value;
    if (!db_->GetProperty(property_slice, &value)) {
        return false;
    }

    try {
        *out = static_cast<uint64_t>(std::stoull(value));
        return true;
    } catch (const std::exception&) {
        return false;
    }
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
