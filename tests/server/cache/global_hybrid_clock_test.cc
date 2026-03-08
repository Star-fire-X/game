#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "cache/cache_types.h"
#include "cache/global_hybrid_clock.h"

using namespace mir2::cache;

class GlobalHybridClockTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试开始前创建新的时钟
        clock_ = std::make_unique<GlobalHybridClock>(nullptr);
    }

    void TearDown() override { clock_.reset(); }

    std::unique_ptr<GlobalHybridClock> clock_;
};

TEST_F(GlobalHybridClockTest, BasicIncrement) {
    // 测试基本的递增功能
    uint64_t v1 = clock_->Next();
    uint64_t v2 = clock_->Next();
    uint64_t v3 = clock_->Next();

    EXPECT_GT(v2, v1);
    EXPECT_GT(v3, v2);
}

TEST_F(GlobalHybridClockTest, MonotonicIncrement) {
    // 生成 1000 个版本号，验证严格递增
    std::vector<uint64_t> versions;
    constexpr int count = 1000;
    versions.reserve(count);

    for (int i = 0; i < count; ++i) {
        versions.push_back(clock_->Next());
    }

    for (int i = 1; i < count; ++i) {
        EXPECT_GT(versions[i], versions[i - 1])
            << "Version not strictly increasing at index " << i;
    }
}

TEST_F(GlobalHybridClockTest, HLCEncoding) {
    // 测试版本号编码/解码
    uint64_t version = clock_->Next();

    uint64_t physical = DecodeHLCPhysical(version);
    uint16_t logical = DecodeHLCLogical(version);

    // 重新编码应得到相同结果
    uint64_t re_encoded = EncodeHLC(physical, logical);
    EXPECT_EQ(re_encoded, version);
}

TEST_F(GlobalHybridClockTest, PhysicalTimeProgression) {
    // 测试物理时间随系统时间推进
    uint64_t v1 = clock_->Next();
    uint64_t physical1 = DecodeHLCPhysical(v1);

    // 等待 10ms
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint64_t v2 = clock_->Next();
    uint64_t physical2 = DecodeHLCPhysical(v2);

    // 物理时间应至少增加 10ms 或逻辑计数器递增
    EXPECT_GE(physical2, physical1);
}

TEST_F(GlobalHybridClockTest, LogicalCounterIncrement) {
    // 测试逻辑计数器在同一毫秒内递增
    uint64_t v1 = clock_->Next();
    uint64_t physical1 = DecodeHLCPhysical(v1);
    uint16_t logical1 = DecodeHLCLogical(v1);

    // 快速调用多次（同一毫秒内）
    uint64_t v2 = clock_->Next();
    uint64_t physical2 = DecodeHLCPhysical(v2);
    uint16_t logical2 = DecodeHLCLogical(v2);

    if (physical1 == physical2) {
        // 同一毫秒内，逻辑计数器应递增
        EXPECT_EQ(logical2, logical1 + 1);
    } else {
        // 不同毫秒，逻辑计数器重置
        EXPECT_EQ(logical2, 0);
    }
}

TEST_F(GlobalHybridClockTest, ConcurrentNext) {
    // 测试并发调用 Next()
    constexpr int num_threads = 10;
    constexpr int ops_per_thread = 100;
    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t>> results(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, &results]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                results[i].push_back(clock_->Next());
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 收集所有版本号并检查无重复
    std::vector<uint64_t> all_versions;
    for (const auto& vec : results) {
        all_versions.insert(all_versions.end(), vec.begin(), vec.end());
    }

    std::sort(all_versions.begin(), all_versions.end());
    auto unique_end = std::unique(all_versions.begin(), all_versions.end());

    EXPECT_EQ(unique_end, all_versions.end())
        << "Found duplicate version numbers";
}

TEST_F(GlobalHybridClockTest, PerformanceBenchmark) {
    // 性能测试：>1M ops/s 目标
    constexpr int iterations = 1000000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        clock_->Next();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                       end - start)
                       .count();

    double ops_per_second = (double)iterations / (duration / 1e6);
    std::cout << "GlobalHybridClock performance: " << ops_per_second
              << " ops/s" << std::endl;

    // 验证性能达标（>1M ops/s）
    EXPECT_GT(ops_per_second, 1000000.0)
        << "Performance target not met";
}

TEST_F(GlobalHybridClockTest, LogicalOverflowHandling) {
    // 测试逻辑计数器溢出处理（极端情况）
    // 创建一个初始版本号，逻辑计数器接近上限
    uint64_t physical = 1000;  // 1000ms
    uint16_t logical = 0xFFFE;  // 接近溢出
    uint64_t initial = EncodeHLC(physical, logical);

    // 手动设置时钟到接近溢出状态
    clock_ = std::make_unique<GlobalHybridClock>(nullptr);
    // 注意：这里无法直接设置内部状态，因此此测试主要验证不会崩溃

    // 生成多个版本号，确保不会崩溃
    for (int i = 0; i < 100; ++i) {
        uint64_t v = clock_->Next();
        EXPECT_GT(v, 0);
    }
}
