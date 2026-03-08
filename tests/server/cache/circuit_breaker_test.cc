#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "cache/circuit_breaker.h"

using namespace mir2::cache;

class CircuitBreakerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试开始前创建新的熔断器
        config_ = CircuitBreaker::Config{
            .failure_threshold = 5,
            .failure_rate_threshold = 0.5,
            .min_requests = 10,
            .open_timeout_ms = 500,
            .half_open_max_requests = 3,
            .half_open_success_threshold = 2,
        };
        breaker_ = std::make_unique<CircuitBreaker>(config_);
    }

    void TearDown() override { breaker_.reset(); }

    std::unique_ptr<CircuitBreaker> breaker_;
    CircuitBreaker::Config config_;
};

// 测试 1: 初始状态为 CLOSED
TEST_F(CircuitBreakerTest, InitialStateClosed) {
    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::CLOSED);
    EXPECT_FALSE(breaker_->IsOpen());
}

// 测试 2: 连续失败触发 OPEN
TEST_F(CircuitBreakerTest, ConsecutiveFailureTriggersOpen) {
    for (int i = 0; i < 5; ++i) {
        breaker_->OnFailure();
    }

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::OPEN);
    EXPECT_TRUE(breaker_->IsOpen());
}

// 测试 3: 失败率超过阈值触发 OPEN
TEST_F(CircuitBreakerTest, FailureRateTriggersOpen) {
    // 10 个请求中 6 个失败（60% > 50% 阈值）
    for (int i = 0; i < 6; ++i) {
        breaker_->OnFailure();
    }

    for (int i = 0; i < 4; ++i) {
        breaker_->OnSuccess();
    }

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::OPEN);
    EXPECT_TRUE(breaker_->IsOpen());
}

// 测试 4: Execute 在 CLOSED 状态下正常执行
TEST_F(CircuitBreakerTest, ExecuteInClosedState) {
    int value = 0;
    auto result = breaker_->Execute([&value]() {
        value = 42;
        return value;
    });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(value, 42);
}

// 测试 5: Execute 在 OPEN 状态下拒绝请求
TEST_F(CircuitBreakerTest, ExecuteInOpenState) {
    // 触发熔断
    for (int i = 0; i < 5; ++i) {
        breaker_->OnFailure();
    }

    EXPECT_TRUE(breaker_->IsOpen());

    int value = 0;
    auto result = breaker_->Execute([&value]() {
        value = 42;
        return value;
    });

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(value, 0);  // 不应该执行
}

// 测试 6: OPEN -> HALF_OPEN 超时转移
TEST_F(CircuitBreakerTest, OpenToHalfOpenTimeout) {
    // 触发熔断
    for (int i = 0; i < 5; ++i) {
        breaker_->OnFailure();
    }

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::OPEN);

    // 等待超时
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.open_timeout_ms + 100));

    // 应该转移到 HALF_OPEN
    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::HALF_OPEN);
}

// 测试 7: HALF_OPEN -> CLOSED 成功恢复
TEST_F(CircuitBreakerTest, HalfOpenToClosed) {
    // 触发熔断
    for (int i = 0; i < 5; ++i) {
        breaker_->OnFailure();
    }

    // 等待转移到 HALF_OPEN
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.open_timeout_ms + 100));

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::HALF_OPEN);

    // 记录成功，达到阈值
    for (int i = 0; i < 2; ++i) {
        breaker_->OnSuccess();
    }

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::CLOSED);
}

// 测试 8: HALF_OPEN -> OPEN 失败回退
TEST_F(CircuitBreakerTest, HalfOpenToOpen) {
    // 触发熔断
    for (int i = 0; i < 5; ++i) {
        breaker_->OnFailure();
    }

    // 等待转移到 HALF_OPEN
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.open_timeout_ms + 100));

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::HALF_OPEN);

    // 在 HALF_OPEN 中任何失败都回到 OPEN
    breaker_->OnFailure();

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::OPEN);
}

// 测试 9: 成功重置连续失败计数
TEST_F(CircuitBreakerTest, SuccessResetsConsecutiveFailures) {
    breaker_->OnFailure();
    breaker_->OnFailure();
    breaker_->OnFailure();

    auto stats = breaker_->GetStats();
    EXPECT_EQ(stats.consecutive_failures, 3);

    breaker_->OnSuccess();

    stats = breaker_->GetStats();
    EXPECT_EQ(stats.consecutive_failures, 0);
}

// 测试 10: GetStats 返回正确的统计信息
TEST_F(CircuitBreakerTest, GetStatsReturnsCorrectValues) {
    breaker_->OnFailure();
    breaker_->OnFailure();
    breaker_->OnSuccess();

    auto stats = breaker_->GetStats();

    EXPECT_EQ(stats.state, CircuitBreaker::State::CLOSED);
    EXPECT_EQ(stats.total_requests, 3);
    EXPECT_EQ(stats.total_failures, 2);
    EXPECT_EQ(stats.consecutive_failures, 0);
    EXPECT_DOUBLE_EQ(stats.failure_rate, 2.0 / 3.0);
}

// 测试 11: 并发操作安全性
TEST_F(CircuitBreakerTest, ConcurrentOperationsSafety) {
    const int kNumThreads = 10;
    const int kOpsPerThread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                if (i % 2 == 0) {
                    breaker_->OnSuccess();
                } else {
                    breaker_->OnFailure();
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto stats = breaker_->GetStats();

    // 总请求数应该是 10 * 100 = 1000
    EXPECT_EQ(stats.total_requests, 1000);

    // 应该有约 500 个失败
    EXPECT_EQ(stats.total_failures, 500);

    // 状态应该仍然有效
    EXPECT_NE(stats.state, CircuitBreaker::State::OPEN);
}

// 测试 12: Reset 功能
TEST_F(CircuitBreakerTest, ResetFunctionality) {
    // 触发熔断
    for (int i = 0; i < 5; ++i) {
        breaker_->OnFailure();
    }

    EXPECT_TRUE(breaker_->IsOpen());

    breaker_->Reset();

    EXPECT_EQ(breaker_->GetState(), CircuitBreaker::State::CLOSED);

    auto stats = breaker_->GetStats();
    EXPECT_EQ(stats.total_requests, 0);
    EXPECT_EQ(stats.total_failures, 0);
    EXPECT_EQ(stats.consecutive_failures, 0);
}

// 测试 13: Execute 异常处理
TEST_F(CircuitBreakerTest, ExecuteExceptionHandling) {
    int value = 0;
    auto result = breaker_->Execute([&value]() {
        value = 42;
        throw std::runtime_error("Test exception");
        return value;
    });

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(value, 42);  // 副作用发生

    auto stats = breaker_->GetStats();
    EXPECT_EQ(stats.total_failures, 1);
}

// 测试 14: Execute 返回 void 的特化版本
TEST_F(CircuitBreakerTest, ExecuteVoidSpecialization) {
    int counter = 0;
    bool success = breaker_->Execute([&counter]() { counter++; });

    EXPECT_TRUE(success);
    EXPECT_EQ(counter, 1);
}

// 测试 15: 性能基准 - 1M 操作
TEST_F(CircuitBreakerTest, PerformanceBenchmark) {
    auto start = std::chrono::high_resolution_clock::now();

    const int kIterations = 1000000;
    for (int i = 0; i < kIterations; ++i) {
        breaker_->OnSuccess();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 应该在 100ms 以内完成 1M 操作
    EXPECT_LT(duration, 100) << "Performance benchmark: " << duration << "ms for "
                             << kIterations << " operations";
}
