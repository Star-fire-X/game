#include "rocksdb_cache.h"

#include <cstring>
#include <spdlog/spdlog.h>

#include "rocksdb/db_ttl.h"
#include "rocksdb/table.h"

namespace mir2::cache {

RocksDBCache::RocksDBCache(const Config& config) : config_(config) {}

RocksDBCache::~RocksDBCache() {
    // 正确释放 DBWithTTL
    if (db_) {
        delete db_;
        db_ = nullptr;
    }

    // block_cache 由智能指针自动释放
    block_cache_.reset();
}

bool RocksDBCache::Initialize() {
    auto logger = spdlog::get("mir2");

    try {
        // 配置 RocksDB 选项
        rocksdb::Options options;
        options.create_if_missing = true;

        // 创建 Block Cache（共享指针，自动释放）
        block_cache_ =
            std::shared_ptr<rocksdb::Cache>(rocksdb::NewLRUCache(
                config_.block_cache_size));

        // 配置表选项（Bloom Filter + Block Cache）
        rocksdb::BlockBasedTableOptions table_options;

        // Bloom Filter：10 bits/key，1% 假阳性率
        table_options.filter_policy.reset(
            rocksdb::NewBloomFilterPolicy(
                config_.bloom_filter_bits_per_key, false));

        // Block Cache 配置
        table_options.block_cache = block_cache_;
        table_options.cache_index_and_filter_blocks = true;
        table_options.pin_l0_filter_and_index_blocks_in_cache = true;
        table_options.block_size = 4096;  // 4KB Block

        options.table_factory.reset(
            rocksdb::NewBlockBasedTableFactory(table_options));

        // 压缩配置（LZ4 最快）
        options.compression = rocksdb::kLZ4Compression;

        // MemTable 配置
        options.write_buffer_size = config_.write_buffer_size;
        options.max_write_buffer_number = 2;

        // 并发配置
        options.IncreaseParallelism(std::thread::hardware_concurrency());
        options.OptimizeLevelStyleCompaction(64 * 1024 * 1024);

        // 打开 DBWithTTL 数据库
        rocksdb::Status status = rocksdb::DBWithTTL::Open(
            options, config_.db_path, &db_, config_.ttl_seconds);

        if (!status.ok()) {
            if (logger) {
                logger->error("Failed to open RocksDB: {}", status.ToString());
            }
            return false;
        }

        if (logger) {
            logger->info(
                "RocksDB initialized: path={}, ttl={}s, "
                "block_cache={}MB",
                config_.db_path, config_.ttl_seconds,
                config_.block_cache_size / (1024 * 1024));
        }

        // 尝试读取最大版本号
        std::string max_version_str;
        status = db_->Get(rocksdb::ReadOptions(), GetMaxVersionKey(),
                          &max_version_str);
        if (status.ok() && max_version_str.size() == sizeof(uint64_t)) {
            std::memcpy(&max_version_, max_version_str.data(),
                        sizeof(uint64_t));
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

        // 反序列化数据
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
        // 序列化数据
        auto serialized = SerializeVersionedData(data);

        // 写入数据库
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

        // 更新最大版本号
        if (data.version > max_version_) {
            max_version_ = data.version;
            UpdateMaxVersion(max_version_);
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
        return !batch.empty();
    }

    try {
        rocksdb::WriteBatch write_batch;

        // 构造批次
        for (const auto& [key, data] : batch) {
            auto serialized = SerializeVersionedData(data);
            write_batch.Put(key, rocksdb::Slice(
                                     reinterpret_cast<const char*>(
                                         serialized.data()),
                                     serialized.size()));

            // 追踪最大版本号
            if (data.version > max_version_) {
                max_version_ = data.version;
            }
        }

        // 原子提交（单次 WAL 写入）
        rocksdb::Status status =
            db_->Write(rocksdb::WriteOptions(), &write_batch);

        if (!status.ok()) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->error("RocksDB BatchSet failed: {}", status.ToString());
            }
            return false;
        }

        // 更新最大版本号元数据
        UpdateMaxVersion(max_version_);

        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in RocksDB BatchSet: {}", e.what());
        }
        return false;
    }
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
        return status.ok();
    } catch (const std::exception& e) {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->error("Exception in UpdateMaxVersion: {}", e.what());
        }
        return false;
    }
}

uint64_t RocksDBCache::GetMaxVersion() const {
    return max_version_;
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

    // 格式: [8 bytes version][8 bytes timestamp][4 bytes data_size][data]
    constexpr size_t header_size = 8 + 8 + 4;
    size_t data_size = data.GetSize();
    size_t total_size = header_size + data_size;

    result.reserve(total_size);

    // 写入版本号
    uint64_t version = data.version;
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&version),
                  reinterpret_cast<const uint8_t*>(&version) +
                      sizeof(uint64_t));

    // 写入时间戳
    uint64_t timestamp = data.timestamp_ms;
    result.insert(result.end(),
                  reinterpret_cast<const uint8_t*>(&timestamp),
                  reinterpret_cast<const uint8_t*>(&timestamp) +
                      sizeof(uint64_t));

    // 写入数据大小
    uint32_t size_u32 = static_cast<uint32_t>(data_size);
    result.insert(result.end(),
                  reinterpret_cast<const uint8_t*>(&size_u32),
                  reinterpret_cast<const uint8_t*>(&size_u32) +
                      sizeof(uint32_t));

    // 写入数据
    if (data.data) {
        result.insert(result.end(), data.data->begin(), data.data->end());
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

    // 读取版本号
    std::memcpy(&result.version, ptr, sizeof(uint64_t));
    ptr += sizeof(uint64_t);

    // 读取时间戳
    std::memcpy(&result.timestamp_ms, ptr, sizeof(uint64_t));
    ptr += sizeof(uint64_t);

    // 读取数据大小
    uint32_t size_u32 = 0;
    std::memcpy(&size_u32, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    // 读取数据
    size_t data_size = static_cast<size_t>(size_u32);
    if (header_size + data_size != data.size()) {
        return false;
    }

    if (data_size > 0) {
        auto vec = std::make_shared<const std::vector<uint8_t>>(
            ptr, ptr + data_size);
        result.data = vec;
    } else {
        result.data =
            std::make_shared<const std::vector<uint8_t>>();
    }

    return true;
}

}  // namespace mir2::cache
