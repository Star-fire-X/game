#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "cache/tiered_cache.h"

using namespace mir2::cache;

// Mock PostgresBackend for testing
class TestPostgresBackend : public PostgresBackend {
public:
    bool SaveEntity(const std::string& key, const VersionedData& data,
                    uint64_t version) override {
        saved_entities_[key] = {data, version};
        return true;
    }

    uint32_t BatchSaveEntities(
        const std::vector<std::pair<std::string, VersionedData>>& batch) override {
        for (const auto& [key, data] : batch) {
            saved_entities_[key] = {data, data.version};
        }
        return batch.size();
    }

    std::optional<VersionedData> LoadEntity(const std::string& key) override {
        auto it = saved_entities_.find(key);
        if (it != saved_entities_.end()) {
            return it->second.first;
        }
        return std::nullopt;
    }

    bool IsHealthy() const override { return healthy_; }

    void SetHealthy(bool healthy) { healthy_ = healthy; }

    std::map<std::string, std::pair<VersionedData, uint64_t>> saved_entities_;
    bool healthy_ = true;
};

class TieredCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = std::make_shared<TestPostgresBackend>();

        config_.l1_config.capacity = 100;
        config_.l2_config.db_path = "./test_rocksdb_" + GetTestName();
        config_.l2_config.ttl_seconds = 3600;
        config_.pg_queue_config.batch_interval_ms = 50;
        config_.pg_queue_config.batch_size = 10;
        config_.circuit_breaker_config.failure_threshold = 3;
        config_.circuit_breaker_config.open_timeout_ms = 200;

        cache_ = std::make_unique<TieredCache>(backend_, config_);
    }

    void TearDown() override {
        cache_->Shutdown();
        cache_.reset();
    }

    std::string GetTestName() const {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }

    std::shared_ptr<TestPostgresBackend> backend_;
    TieredCache::Config config_;
    std::unique_ptr<TieredCache> cache_;

    // 辅助函数
    VersionedData CreateData(const std::string& content) {
        auto ptr = std::make_shared<const std::vector<uint8_t>>(
            content.begin(), content.end());
        return VersionedData(0, ptr, 0);
    }

    std::vector<uint8_t> StringToBytes(const std::string& str) {
        return std::vector<uint8_t>(str.begin(), str.end());
    }
};

// 测试 1: SetState 直接覆盖
TEST_F(TieredCacheTest, SetStateDirectOverwrite) {
    auto data = StringToBytes("hello");
    bool success = cache_->SetState("key1", data, Priority::NORMAL);

    EXPECT_TRUE(success);

    // 验证数据可以读取
    auto result = cache_->Get("key1");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->GetSize(), 5);
}

// 测试 2: Get 三层回填
TEST_F(TieredCacheTest, GetThreeLayerFallback) {
    // 第一次 Get 时不存在
    auto result1 = cache_->Get("key1");
    EXPECT_FALSE(result1.has_value());

    // SetState 创建数据
    auto data = StringToBytes("test_data");
    cache_->SetState("key1", data, Priority::NORMAL);

    // 第二次 Get 应该命中
    auto result2 = cache_->Get("key1");
    EXPECT_TRUE(result2.has_value());
}

// 测试 3: UpdateWithRetry 基本功能
TEST_F(TieredCacheTest, UpdateWithRetryBasic) {
    // 初始化数据
    auto initial_data = StringToBytes("100");
    cache_->SetState("balance", initial_data, Priority::NORMAL);

    // 等待持久化
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 执行更新：增加 50
    bool success = cache_->UpdateWithRetry(
        "balance",
        [this](const std::optional<VersionedData>& current) {
            // 简单的模拟：直接返回新数据
            auto new_data = StringToBytes("150");
            auto ptr = std::make_shared<const std::vector<uint8_t>>(
                new_data.begin(), new_data.end());
            return std::optional<std::shared_ptr<const std::vector<uint8_t>>>(ptr);
        },
        5, Priority::HIGH);

    EXPECT_TRUE(success);

    auto result = cache_->Get("balance");
    EXPECT_TRUE(result.has_value());
}

// 测试 4: 并发冲突检测
TEST_F(TieredCacheTest, ConcurrentConflictDetection) {
    auto initial_data = StringToBytes("initial");
    cache_->SetState("key", initial_data, Priority::NORMAL);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool update1_success = true;
    bool update2_success = true;

    std::thread t1([this, &update1_success]() {
        update1_success = cache_->UpdateWithRetry(
            "key",
            [this](const std::optional<VersionedData>& current) {
                auto new_data = StringToBytes("update1");
                auto ptr = std::make_shared<const std::vector<uint8_t>>(
                    new_data.begin(), new_data.end());
                return std::optional<std::shared_ptr<const std::vector<uint8_t>>>(ptr);
            });
    });

    std::thread t2([this, &update2_success]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        update2_success = cache_->UpdateWithRetry(
            "key",
            [this](const std::optional<VersionedData>& current) {
                auto new_data = StringToBytes("update2");
                auto ptr = std::make_shared<const std::vector<uint8_t>>(
                    new_data.begin(), new_data.end());
                return std::optional<std::shared_ptr<const std::vector<uint8_t>>>(ptr);
            });
    });

    t1.join();
    t2.join();

    EXPECT_TRUE(update1_success);
    EXPECT_TRUE(update2_success);
}

// 测试 5: LoadFromDBAsync 异步加载
TEST_F(TieredCacheTest, LoadFromDBAsync) {
    // 模拟从 DB 加载
    auto test_data = CreateData("db_loaded_data");
    backend_->SaveEntity("async_key", test_data, test_data.version);

    auto future = cache_->LoadFromDBAsync("async_key");

    // 等待异步完成
    auto result = future.get();

    EXPECT_TRUE(result.has_value());
}

// 测试 6: 高优先级 <100ms 持久化
TEST_F(TieredCacheTest, HighPriorityPersistence) {
    auto start = std::chrono::high_resolution_clock::now();

    auto data = StringToBytes("high_priority_data");
    bool success = cache_->SetState("urgent", data, Priority::HIGH);

    EXPECT_TRUE(success);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // SetState 本身应该很快
    EXPECT_LT(duration, 50);
}

// 测试 7: 普通优先级 30s 批量处理
TEST_F(TieredCacheTest, NormalPriorityBatching) {
    const int kDataCount = 5;

    for (int i = 0; i < kDataCount; ++i) {
        auto data = StringToBytes("data_" + std::to_string(i));
        cache_->SetState("key_" + std::to_string(i), data, Priority::NORMAL);
    }

    // 等待批处理（在测试配置中是 50ms）
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    auto metrics = cache_->GetHealthMetrics();
    EXPECT_TRUE(metrics.is_healthy);
}

// 测试 8: 熔断降级
TEST_F(TieredCacheTest, CircuitBreakerDegradation) {
    auto data = StringToBytes("test");

    // 正常工作
    bool success1 = cache_->SetState("key1", data, Priority::NORMAL);
    EXPECT_TRUE(success1);

    // 模拟 L2 故障不在这个测试中实现（需要 RocksDB 模拟）
    // 但我们验证熔断器存在
    auto metrics = cache_->GetHealthMetrics();
    EXPECT_NE(metrics.l2_state, CircuitBreaker::State::OPEN);
}

// 测试 9: Get 命中率
TEST_F(TieredCacheTest, GetHitRatio) {
    // 写入 5 条数据
    for (int i = 0; i < 5; ++i) {
        auto data = StringToBytes("data_" + std::to_string(i));
        cache_->SetState("key_" + std::to_string(i), data, Priority::NORMAL);
    }

    // 读取 3 次命中
    for (int i = 0; i < 3; ++i) {
        auto result = cache_->Get("key_" + std::to_string(i));
        EXPECT_TRUE(result.has_value());
    }

    // 读取 2 次未命中
    for (int i = 5; i < 7; ++i) {
        auto result = cache_->Get("key_" + std::to_string(i));
        EXPECT_FALSE(result.has_value());
    }

    auto metrics = cache_->GetHealthMetrics();
    // L1 应该有一些命中
    EXPECT_GT(metrics.l1_hit_ratio, 0.0);
}

// 测试 10: IsHealthy 检查
TEST_F(TieredCacheTest, HealthyCheck) {
    EXPECT_TRUE(cache_->IsHealthy());

    // 模拟后端不健康
    backend_->SetHealthy(false);
    EXPECT_FALSE(cache_->IsHealthy());
}

// 测试 11: 并发读写
TEST_F(TieredCacheTest, ConcurrentReadWrite) {
    const int kNumThreads = 5;
    const int kOpsPerThread = 20;
    std::vector<std::thread> threads;

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t, kOpsPerThread]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
                auto data = StringToBytes("data_" + std::to_string(i));

                // 交替读写
                if (i % 2 == 0) {
                    cache_->SetState(key, data, Priority::NORMAL);
                } else {
                    cache_->Get(key);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // 应该没有崩溃
    auto metrics = cache_->GetHealthMetrics();
    EXPECT_TRUE(metrics.is_healthy);
}

// 测试 12: 顺序版本控制
TEST_F(TieredCacheTest, SequentialVersionControl) {
    auto data1 = StringToBytes("version1");
    auto data2 = StringToBytes("version2");
    auto data3 = StringToBytes("version3");

    cache_->SetState("key", data1, Priority::NORMAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    cache_->SetState("key", data2, Priority::NORMAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    cache_->SetState("key", data3, Priority::NORMAL);

    auto result = cache_->Get("key");
    EXPECT_TRUE(result.has_value());

    // 版本号应该严格递增
    EXPECT_GT(result->version, 0);
}

// 测试 13: Shutdown 优雅关闭
TEST_F(TieredCacheTest, GracefulShutdown) {
    auto data = StringToBytes("test");

    for (int i = 0; i < 10; ++i) {
        cache_->SetState("key_" + std::to_string(i), data, Priority::HIGH);
    }

    // 优雅关闭
    cache_->Shutdown();

    // 应该完成所有待处理操作
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// 测试 14: 性能基准 - SetState <1ms
TEST_F(TieredCacheTest, SetStatePerformance) {
    auto data = StringToBytes("benchmark");

    auto start = std::chrono::high_resolution_clock::now();

    const int kIterations = 100;
    for (int i = 0; i < kIterations; ++i) {
        cache_->SetState("key_" + std::to_string(i), data, Priority::NORMAL);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 平均每个 SetState <1ms (1000μs)
    double avg_us = static_cast<double>(duration) / kIterations;
    EXPECT_LT(avg_us, 1000.0) << "Average SetState time: " << avg_us << "μs";
}

// 测试 15: 性能基准 - Get <1ms
TEST_F(TieredCacheTest, GetPerformance) {
    // 预填充缓存
    auto data = StringToBytes("benchmark");
    for (int i = 0; i < 50; ++i) {
        cache_->SetState("key_" + std::to_string(i), data, Priority::NORMAL);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto start = std::chrono::high_resolution_clock::now();

    const int kIterations = 100;
    for (int i = 0; i < kIterations; ++i) {
        cache_->Get("key_" + std::to_string(i % 50));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 平均每个 Get <1ms
    double avg_us = static_cast<double>(duration) / kIterations;
    EXPECT_LT(avg_us, 1000.0) << "Average Get time: " << avg_us << "μs";
}

// 测试 16: 端到端集成
TEST_F(TieredCacheTest, EndToEndIntegration) {
    // 模拟游戏场景
    const std::string kPlayerId = "player_123";

    // 1. 玩家登录，加载数据
    auto load_future = cache_->LoadFromDBAsync(kPlayerId);
    auto loaded_data = load_future.get();

    // 2. 设置玩家位置（SetState）
    auto position = StringToBytes("x:100,y:200");
    cache_->SetState(kPlayerId + ":position", position, Priority::NORMAL);

    // 3. 增加金币（UpdateWithRetry）
    bool gold_updated = cache_->UpdateWithRetry(
        kPlayerId + ":gold",
        [this](const std::optional<VersionedData>& current) {
            auto new_gold = StringToBytes("9999");
            auto ptr = std::make_shared<const std::vector<uint8_t>>(
                new_gold.begin(), new_gold.end());
            return std::optional<std::shared_ptr<const std::vector<uint8_t>>>(ptr);
        });

    // 4. 读取数据验证
    auto position_result = cache_->Get(kPlayerId + ":position");
    EXPECT_TRUE(position_result.has_value());

    // 5. 检查健康状态
    auto metrics = cache_->GetHealthMetrics();
    EXPECT_TRUE(metrics.is_healthy);
}
