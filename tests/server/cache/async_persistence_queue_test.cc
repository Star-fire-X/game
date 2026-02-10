#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "cache/async_persistence_queue.h"

using namespace mir2::cache;

// Mock PostgresBackend for testing
class MockPostgresBackend : public PostgresBackend {
public:
    MOCK_METHOD(bool, SaveEntity, (const std::string&, const VersionedData&, uint64_t),
                (override));
    MOCK_METHOD(uint32_t, BatchSaveEntities,
                ((const std::vector<std::pair<std::string, VersionedData>>&)), (override));
    MOCK_METHOD(std::optional<VersionedData>, LoadEntity, (const std::string&),
                (override));
    MOCK_METHOD(bool, IsHealthy, (), (const, override));

    // 默认实现
    MockPostgresBackend() = default;
};

// 简单的 PostgresBackend 实现用于测试
class SimplePostgresBackend : public PostgresBackend {
public:
    bool SaveEntity(const std::string& key, const VersionedData& data,
                    uint64_t version) override {
        saved_count++;
        return true;
    }

    uint32_t BatchSaveEntities(
        const std::vector<std::pair<std::string, VersionedData>>& batch) override {
        batch_count += batch.size();
        return batch.size();
    }

    std::optional<VersionedData> LoadEntity(const std::string& key) override {
        return std::nullopt;
    }

    bool IsHealthy() const override { return true; }

    size_t saved_count = 0;
    size_t batch_count = 0;
};

class AsyncPersistenceQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = std::make_shared<SimplePostgresBackend>();

        config_ = AsyncPersistenceQueue::Config{
            .queue_capacity = 1000,
            .batch_interval_ms = 100,  // 缩短以加快测试
            .batch_size = 10,
            .worker_threads = 2,
            .retry_count = 2,
            .retry_delay_ms = 10,
        };

        queue_ = std::make_unique<AsyncPersistenceQueue>(backend_, config_);
    }

    void TearDown() override { queue_.reset(); }

    std::shared_ptr<SimplePostgresBackend> backend_;
    AsyncPersistenceQueue::Config config_;
    std::unique_ptr<AsyncPersistenceQueue> queue_;

    // 辅助函数：创建测试数据
    VersionedData CreateTestData(uint64_t version, const std::string& content) {
        auto data = std::make_shared<const std::vector<uint8_t>>(
            content.begin(), content.end());
        return VersionedData(version, data, 0);
    }
};

// 测试 1: 高优先级立即提交
TEST_F(AsyncPersistenceQueueTest, HighPriorityImmediateSubmit) {
    auto data = CreateTestData(1, "test_data");
    bool enqueued = queue_->Enqueue("key1", data, Priority::HIGH);

    EXPECT_TRUE(enqueued);

    // 等待异步处理
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.enqueued_total, 1);
    EXPECT_GT(stats.persisted_success, 0);
}

// 测试 2: 普通优先级批量处理
TEST_F(AsyncPersistenceQueueTest, NormalPriorityBatchProcessing) {
    // 入队 15 条普通优先级数据（batch_size = 10）
    for (int i = 0; i < 15; ++i) {
        auto data = CreateTestData(i, "data_" + std::to_string(i));
        bool enqueued = queue_->Enqueue("key_" + std::to_string(i), data,
                                       Priority::NORMAL);
        EXPECT_TRUE(enqueued);
    }

    // 等待批处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.enqueued_total, 15);
    EXPECT_GT(stats.persisted_success, 0);
}

// 测试 3: 队列满拒绝
TEST_F(AsyncPersistenceQueueTest, QueueFullRejection) {
    auto config = config_;
    config.queue_capacity = 5;  // 很小的容量
    auto queue = std::make_unique<AsyncPersistenceQueue>(backend_, config);

    // 填满队列
    for (int i = 0; i < 5; ++i) {
        auto data = CreateTestData(i, "data");
        bool enqueued = queue->Enqueue("key_" + std::to_string(i), data,
                                      Priority::HIGH);
        EXPECT_TRUE(enqueued);
    }

    // 下一个应该被拒绝
    auto data = CreateTestData(5, "overflow");
    bool enqueued = queue->Enqueue("key_overflow", data, Priority::HIGH);
    EXPECT_FALSE(enqueued);
}

// 测试 4: 优雅关闭 (FlushAll)
TEST_F(AsyncPersistenceQueueTest, GracefulShutdown) {
    // 入队数据
    for (int i = 0; i < 5; ++i) {
        auto data = CreateTestData(i, "data_" + std::to_string(i));
        queue_->Enqueue("key_" + std::to_string(i), data, Priority::NORMAL);
    }

    // 优雅关闭
    bool flushed = queue_->FlushAll(2000);
    EXPECT_TRUE(flushed);

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.high_priority_queue_depth + stats.normal_priority_queue_depth, 0);
}

// 测试 5: 并发高优先级写入
TEST_F(AsyncPersistenceQueueTest, ConcurrentHighPriorityWrites) {
    const int kNumThreads = 5;
    const int kOpsPerThread = 20;
    std::vector<std::thread> threads;

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t, kOpsPerThread]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                auto data = CreateTestData(i, "thread_" + std::to_string(t));
                queue_->Enqueue("key_" + std::to_string(t) + "_" + std::to_string(i),
                               data, Priority::HIGH);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // 等待处理
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.enqueued_total, kNumThreads * kOpsPerThread);
}

// 测试 6: 混合优先级处理
TEST_F(AsyncPersistenceQueueTest, MixedPriorityProcessing) {
    // 同时入队高和低优先级数据
    for (int i = 0; i < 10; ++i) {
        auto data = CreateTestData(i, "data_" + std::to_string(i));

        if (i % 2 == 0) {
            queue_->Enqueue("key_high_" + std::to_string(i), data, Priority::HIGH);
        } else {
            queue_->Enqueue("key_normal_" + std::to_string(i), data, Priority::NORMAL);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.enqueued_total, 10);
}

// 测试 7: 后端健康检查失败
TEST_F(AsyncPersistenceQueueTest, BackendHealthCheckFailure) {
    // 创建不健康的后端
    auto unhealthy_backend = std::make_shared<SimplePostgresBackend>();

    // 模拟健康检查失败
    class UnhealthyBackend : public PostgresBackend {
    public:
        bool SaveEntity(const std::string& key, const VersionedData& data,
                       uint64_t version) override {
            return false;
        }

        uint32_t BatchSaveEntities(
            const std::vector<std::pair<std::string, VersionedData>>& batch) override {
            return 0;
        }

        std::optional<VersionedData> LoadEntity(const std::string& key) override {
            return std::nullopt;
        }

        bool IsHealthy() const override { return false; }
    };

    auto bad_backend = std::make_shared<UnhealthyBackend>();
    auto queue = std::make_unique<AsyncPersistenceQueue>(bad_backend, config_);

    auto data = CreateTestData(1, "test");
    bool enqueued = queue->Enqueue("key", data, Priority::HIGH);

    // 应该被拒绝因为后端不健康
    EXPECT_FALSE(enqueued);
}

// 测试 8: 统计信息准确性
TEST_F(AsyncPersistenceQueueTest, StatisticsAccuracy) {
    const int kTotal = 25;

    for (int i = 0; i < kTotal; ++i) {
        auto data = CreateTestData(i, "data");
        queue_->Enqueue("key_" + std::to_string(i), data,
                       i % 3 == 0 ? Priority::HIGH : Priority::NORMAL);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.enqueued_total, kTotal);
    EXPECT_GT(stats.persisted_success, 0);
    EXPECT_LE(stats.persisted_success + stats.persisted_failed, kTotal);
}

// 测试 9: 批处理大小触发
TEST_F(AsyncPersistenceQueueTest, BatchSizeThresholdTrigger) {
    auto config = config_;
    config.batch_size = 5;  // 小批量
    auto queue = std::make_unique<AsyncPersistenceQueue>(backend_, config);

    // 入队 8 条数据（应该触发两次批处理）
    for (int i = 0; i < 8; ++i) {
        auto data = CreateTestData(i, "data");
        queue->Enqueue("key_" + std::to_string(i), data, Priority::NORMAL);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto stats = queue->GetStats();
    EXPECT_EQ(stats.enqueued_total, 8);
}

// 测试 10: 多次 FlushAll
TEST_F(AsyncPersistenceQueueTest, MultipleFlushs) {
    for (int flush = 0; flush < 3; ++flush) {
        for (int i = 0; i < 5; ++i) {
            auto data = CreateTestData(i, "data");
            queue_->Enqueue("key_" + std::to_string(i), data, Priority::NORMAL);
        }

        bool flushed = queue_->FlushAll(1000);
        EXPECT_TRUE(flushed);
    }

    auto stats = queue_->GetStats();
    EXPECT_EQ(stats.enqueued_total, 15);
}

// 测试 11: 长期运行稳定性
TEST_F(AsyncPersistenceQueueTest, LongRunningStability) {
    const int kDuration = 1000;  // 1 秒
    const int kOpsPerMs = 10;

    auto start = std::chrono::steady_clock::now();
    int counter = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (elapsed > kDuration) {
            break;
        }

        for (int i = 0; i < kOpsPerMs; ++i) {
            auto data = CreateTestData(counter, "data_" + std::to_string(counter));
            Priority priority = counter % 3 == 0 ? Priority::HIGH : Priority::NORMAL;
            queue_->Enqueue("key_" + std::to_string(counter), data, priority);
            counter++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    queue_->FlushAll(5000);

    auto stats = queue_->GetStats();
    EXPECT_GT(stats.enqueued_total, 0);
}
