#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

#include "cache/cache_types.h"
#include "cache/rocksdb_cache.h"

namespace fs = std::filesystem;
using namespace mir2::cache;

class RocksDBCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 使用临时数据库路径
        db_path_ = "./test_rocksdb_" +
                   std::to_string(std::chrono::system_clock::now()
                                      .time_since_epoch()
                                      .count());

        config_.db_path = db_path_;
        config_.block_cache_size = 64 * 1024 * 1024;  // 64MB for tests
        config_.ttl_seconds = 3600;

        cache_ = std::make_unique<RocksDBCache>(config_);
        ASSERT_TRUE(cache_->Initialize());
    }

    void TearDown() override {
        cache_.reset();

        // 清理临时数据库
        if (fs::exists(db_path_)) {
            fs::remove_all(db_path_);
        }
    }

    RocksDBCache::Config config_;
    std::unique_ptr<RocksDBCache> cache_;
    std::string db_path_;

    // 创建测试数据
    VersionedData CreateTestData(uint64_t version, size_t data_size) {
        auto vec = std::make_shared<const std::vector<uint8_t>>(
            data_size, static_cast<uint8_t>(version % 256));
        return VersionedData(version, vec,
                             std::chrono::system_clock::now()
                                 .time_since_epoch()
                                 .count() /
                                 1000000);
    }
};

TEST_F(RocksDBCacheTest, BasicSetGet) {
    auto data = CreateTestData(100, 1000);
    EXPECT_TRUE(cache_->Set("key1", data));

    auto result = cache_->Get("key1");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->version, 100);
    EXPECT_EQ(result->GetSize(), 1000);
}

TEST_F(RocksDBCacheTest, GetMissReturnsNullopt) {
    auto result = cache_->Get("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RocksDBCacheTest, Delete) {
    auto data = CreateTestData(100, 1000);
    EXPECT_TRUE(cache_->Set("key1", data));

    EXPECT_TRUE(cache_->Get("key1").has_value());
    EXPECT_TRUE(cache_->Delete("key1"));
    EXPECT_FALSE(cache_->Get("key1").has_value());
}

TEST_F(RocksDBCacheTest, Overwrite) {
    auto data1 = CreateTestData(100, 1000);
    auto data2 = CreateTestData(200, 2000);

    EXPECT_TRUE(cache_->Set("key1", data1));
    EXPECT_EQ(cache_->Get("key1")->version, 100);

    EXPECT_TRUE(cache_->Set("key1", data2));
    auto result = cache_->Get("key1");
    EXPECT_EQ(result->version, 200);
    EXPECT_EQ(result->GetSize(), 2000);
}

TEST_F(RocksDBCacheTest, BatchSet) {
    std::vector<std::pair<std::string, VersionedData>> batch;
    for (int i = 0; i < 10; ++i) {
        batch.emplace_back(
            "batch_key_" + std::to_string(i),
            CreateTestData(i, 1000));
    }

    EXPECT_TRUE(cache_->BatchSet(batch));

    // 验证所有数据都写入成功
    for (int i = 0; i < 10; ++i) {
        auto result = cache_->Get("batch_key_" + std::to_string(i));
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result->version, i);
    }
}

TEST_F(RocksDBCacheTest, MaxVersionTracking) {
    auto data1 = CreateTestData(100, 1000);
    auto data2 = CreateTestData(200, 1000);
    auto data3 = CreateTestData(150, 1000);  // 不是最大

    EXPECT_TRUE(cache_->Set("key1", data1));
    EXPECT_EQ(cache_->GetMaxVersion(), 100);

    EXPECT_TRUE(cache_->Set("key2", data2));
    EXPECT_EQ(cache_->GetMaxVersion(), 200);

    EXPECT_TRUE(cache_->Set("key3", data3));
    EXPECT_EQ(cache_->GetMaxVersion(), 200);  // 仍是最大
}

TEST_F(RocksDBCacheTest, LargeDataSet) {
    // 测试存储大数据
    constexpr size_t large_size = 1024 * 1024;  // 1MB
    auto data = CreateTestData(1000, large_size);

    EXPECT_TRUE(cache_->Set("large_key", data));

    auto result = cache_->Get("large_key");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->GetSize(), large_size);

    // 验证数据内容
    if (result->data) {
        for (size_t i = 0; i < std::min(size_t(100), result->data->size());
             ++i) {
            EXPECT_EQ((*result->data)[i], 232);  // 1000 % 256
        }
    }
}

TEST_F(RocksDBCacheTest, EmptyDataSet) {
    // 测试存储空数据
    auto empty_data = VersionedData(50,
                                     std::make_shared<
                                         const std::vector<uint8_t>>(),
                                     123456);

    EXPECT_TRUE(cache_->Set("empty_key", empty_data));

    auto result = cache_->Get("empty_key");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->version, 50);
    EXPECT_EQ(result->GetSize(), 0);
}

TEST_F(RocksDBCacheTest, PerformanceBenchmark) {
    // 性能测试：Get <50μs，单条写入 <50μs
    constexpr int iterations = 10000;

    // 首先写入数据
    for (int i = 0; i < iterations; ++i) {
        cache_->Set("perf_key_" + std::to_string(i),
                    CreateTestData(i, 1000));
    }

    // 读取性能
    auto read_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        cache_->Get("perf_key_" + std::to_string(i % (iterations / 2)));
    }
    auto read_end = std::chrono::high_resolution_clock::now();
    auto read_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(
            read_end - read_start)
            .count();

    double read_time_per_op = (double)read_duration / iterations;
    std::cout << "RocksDB Read time: " << read_time_per_op << " μs/op"
              << std::endl;

    // 性能达标验证：<50μs
    EXPECT_LT(read_time_per_op, 50.0);
}

TEST_F(RocksDBCacheTest, BatchPerformance) {
    // 测试批量写入性能
    constexpr int batch_size = 100;
    constexpr int num_batches = 100;

    auto write_start = std::chrono::high_resolution_clock::now();

    for (int b = 0; b < num_batches; ++b) {
        std::vector<std::pair<std::string, VersionedData>> batch;
        for (int i = 0; i < batch_size; ++i) {
            batch.emplace_back(
                "batch_" + std::to_string(b) + "_" + std::to_string(i),
                CreateTestData(b * batch_size + i, 1000));
        }
        EXPECT_TRUE(cache_->BatchSet(batch));
    }

    auto write_end = std::chrono::high_resolution_clock::now();
    auto write_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(
            write_end - write_start)
            .count();

    int total_ops = batch_size * num_batches;
    double write_time_per_op = (double)write_duration / total_ops;

    std::cout << "RocksDB Batch Write time: " << write_time_per_op
              << " μs/op" << std::endl;
}

TEST_F(RocksDBCacheTest, SerializationRoundTrip) {
    // 测试序列化和反序列化的完整性
    for (int i = 0; i < 10; ++i) {
        auto original = CreateTestData(i * 100, i * 1000 + 500);

        EXPECT_TRUE(cache_->Set("serialize_test_" + std::to_string(i),
                                original));

        auto retrieved =
            cache_->Get("serialize_test_" + std::to_string(i));
        EXPECT_TRUE(retrieved.has_value());

        EXPECT_EQ(retrieved->version, original.version);
        EXPECT_EQ(retrieved->timestamp_ms, original.timestamp_ms);
        EXPECT_EQ(retrieved->GetSize(), original.GetSize());

        // 验证数据内容
        if (original.data && retrieved->data) {
            for (size_t j = 0; j < original.data->size(); ++j) {
                EXPECT_EQ((*retrieved->data)[j],
                         (*original.data)[j]);
            }
        }
    }
}

TEST_F(RocksDBCacheTest, PersistenceAcrossInstances) {
    // 测试数据在数据库实例重启后是否持久
    std::string test_key = "persistence_test";
    auto original = CreateTestData(999, 10000);

    EXPECT_TRUE(cache_->Set(test_key, original));

    // 关闭缓存
    cache_.reset();

    // 创建新的缓存实例（使用相同的数据库路径）
    auto new_cache = std::make_unique<RocksDBCache>(config_);
    ASSERT_TRUE(new_cache->Initialize());

    // 验证数据仍然存在
    auto retrieved = new_cache->Get(test_key);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->version, 999);
    EXPECT_EQ(retrieved->GetSize(), 10000);

    new_cache.reset();
}
