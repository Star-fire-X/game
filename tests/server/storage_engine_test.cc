/**
 * @file storage_engine_test.cc
 * @brief StorageEngine骨架测试
 */

#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"
#include "storage_engine/backends/common/account_storage_codec.h"
#include "storage_engine/utils/circuit_breaker.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

using namespace mir2::storage_engine;

class UnhealthyBackend : public test::NoopStorageBackend {
public:
    bool IsHealthy() const override {
        return false;
    }
};

class CountingBackend : public test::NoopStorageBackend {
public:
    std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
        const std::string&) override {
        load_calls.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    bool IsHealthy() const override {
        health_checks.fetch_add(1, std::memory_order_relaxed);
        return healthy.load(std::memory_order_relaxed);
    }

    std::atomic<bool> healthy{true};
    mutable std::atomic<uint32_t> load_calls{0};
    mutable std::atomic<uint32_t> health_checks{0};
};

class AccountPayloadBackend : public test::NoopStorageBackend {
public:
    explicit AccountPayloadBackend(std::string username)
        : account_key_(mir2::db::BuildAccountStorageKey(username)) {}

    std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
        const std::string& key) override {
        if (key != account_key_) {
            return std::nullopt;
        }

        load_calls.fetch_add(1, std::memory_order_relaxed);
        mir2::db::AccountData account;
        account.id = 9527;
        account.username = "alice";
        account.password_hash = "$2b$12$dummy.hash.for.testing.only";
        account.email = "alice@example.com";
        account.created_at = 1700000000000;
        account.last_login = 1700000000100;
        account.banned = false;
        return std::make_pair(account.last_login, mir2::db::EncodeAccountData(account));
    }

    std::string account_key_;
    std::atomic<uint32_t> load_calls{0};
};

// ===== 测试夹具 =====
class StorageEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (StorageEngine::IsInitialized()) {
            StorageEngine::Shutdown();
        }

        auto backend = std::make_unique<test::NoopStorageBackend>();

        ASSERT_TRUE(StorageEngine::Initialize(std::move(backend)));
    }

    void TearDown() override {
        StorageEngine::Shutdown();
    }
};

// ===== 测试用例 =====
TEST_F(StorageEngineTest, SingletonPattern) {
    ASSERT_TRUE(StorageEngine::IsInitialized());

    auto& engine1 = StorageEngine::Instance();
    auto& engine2 = StorageEngine::Instance();

    ASSERT_EQ(&engine1, &engine2);
}

TEST_F(StorageEngineTest, SetAndGet) {
    auto& engine = StorageEngine::Instance();

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    ASSERT_TRUE(engine.Set("test_key", data));

    // L1 cache should now return the value
    auto result = engine.Get("test_key");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->data, data);
}

TEST_F(StorageEngineTest, UpdateWithPureComputation) {
    auto& engine = StorageEngine::Instance();

    // 初始值：100
    std::vector<uint8_t> initial_data = {100, 0, 0, 0};
    engine.Set("gold", initial_data);

    // 扣除50金币
    bool success = engine.Update("gold",
        [](const std::optional<VersionedData>& current)
            -> std::optional<std::vector<uint8_t>> {
            if (!current) return std::nullopt;

            // 纯计算：反序列化
            int32_t gold = *reinterpret_cast<const int32_t*>(current->data.data());

            // 纯计算：逻辑判断
            if (gold < 50) return std::nullopt;

            // 纯计算：序列化
            int32_t new_gold = gold - 50;
            std::vector<uint8_t> result(4);
            std::memcpy(result.data(), &new_gold, 4);
            return result;
        }
    );

    ASSERT_TRUE(success);
}

TEST_F(StorageEngineTest, UpdateBusinessAbort) {
    auto& engine = StorageEngine::Instance();

    // 初始值：30金币
    std::vector<uint8_t> initial_data = {30, 0, 0, 0};
    engine.Set("gold", initial_data);

    // 尝试扣除50金币（余额不足）
    bool success = engine.Update("gold",
        [](const std::optional<VersionedData>& current)
            -> std::optional<std::vector<uint8_t>> {
            if (!current) return std::nullopt;

            int32_t gold = *reinterpret_cast<const int32_t*>(current->data.data());
            if (gold < 50) {
                return std::nullopt;  // 业务逻辑放弃
            }

            int32_t new_gold = gold - 50;
            std::vector<uint8_t> result(4);
            std::memcpy(result.data(), &new_gold, 4);
            return result;
        }
    );

    // 业务逻辑放弃仍返回true
    ASSERT_TRUE(success);

    // 验证统计指标
    auto metrics = engine.GetHealthMetrics();
    ASSERT_EQ(metrics.update_aborted, 1);
}

TEST_F(StorageEngineTest, UpdateRetriesOnVersionConflictAndEventuallySucceeds) {
    auto& engine = StorageEngine::Instance();

    int32_t initial = 10;
    std::vector<uint8_t> initial_data(sizeof(int32_t));
    std::memcpy(initial_data.data(), &initial, sizeof(int32_t));
    ASSERT_TRUE(engine.Set("conflict:key", initial_data));

    constexpr int kForcedConflicts = 4;
    constexpr int kMaxRetries = kForcedConflicts + 2;

    std::mutex mutex;
    std::condition_variable cv;
    int attempts_seen = 0;
    int attempts_released = 0;
    bool stop_wait = false;

    bool update_result = false;
    std::thread updater([&]() {
        update_result = engine.Update(
            "conflict:key",
            [&](const std::optional<VersionedData>& current)
                -> std::optional<std::vector<uint8_t>> {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ++attempts_seen;
                    cv.notify_all();
                    cv.wait(lock, [&]() {
                        return stop_wait || attempts_released >= attempts_seen;
                    });
                }

                int32_t value = 0;
                if (current && current->data.size() >= sizeof(int32_t)) {
                    std::memcpy(&value, current->data.data(), sizeof(int32_t));
                }
                ++value;
                std::vector<uint8_t> next(sizeof(int32_t));
                std::memcpy(next.data(), &value, sizeof(int32_t));
                return next;
            },
            kMaxRetries);
    });

    for (int i = 1; i <= kForcedConflicts; ++i) {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return attempts_seen >= i;
        }));
        lock.unlock();

        int32_t conflict_value = 100 + i;
        std::vector<uint8_t> conflict_data(sizeof(int32_t));
        std::memcpy(conflict_data.data(), &conflict_value, sizeof(int32_t));
        ASSERT_TRUE(engine.Set("conflict:key", conflict_data));

        lock.lock();
        attempts_released = i;
        cv.notify_all();
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(
            lock, std::chrono::seconds(1), [&]() {
                return attempts_seen >= (kForcedConflicts + 1);
            }));
        attempts_released = attempts_seen;
        stop_wait = true;
        cv.notify_all();
    }

    updater.join();
    ASSERT_TRUE(update_result);

    auto metrics = engine.GetHealthMetrics();
    EXPECT_GE(metrics.update_conflicts, static_cast<uint64_t>(kForcedConflicts));
    EXPECT_GE(metrics.update_retries, static_cast<uint64_t>(kForcedConflicts));
}

TEST_F(StorageEngineTest, HealthMetrics) {
    auto& engine = StorageEngine::Instance();

    auto metrics = engine.GetHealthMetrics();
    ASSERT_TRUE(metrics.is_healthy);
    ASSERT_EQ(metrics.total_updates, 0);
}

TEST_F(StorageEngineTest, UpdateLatencyMetricsAreTracked) {
    auto& engine = StorageEngine::Instance();
    std::vector<uint8_t> initial_data = {10, 0, 0, 0};
    ASSERT_TRUE(engine.Set("latency:gold", initial_data));

    ASSERT_TRUE(engine.Update(
        "latency:gold",
        [](const std::optional<VersionedData>& current)
            -> std::optional<std::vector<uint8_t>> {
            if (!current || current->data.size() < sizeof(int32_t)) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            int32_t gold = 0;
            std::memcpy(&gold, current->data.data(), sizeof(int32_t));
            int32_t next = gold + 1;
            std::vector<uint8_t> result(sizeof(int32_t));
            std::memcpy(result.data(), &next, sizeof(int32_t));
            return result;
        }));

    const auto metrics = engine.GetHealthMetrics();
    EXPECT_GT(metrics.avg_update_latency_ms, 1.0);
    EXPECT_GT(metrics.p99_update_latency_ms, 1.0);
}

TEST(StorageEngineSetSemanticsTest, SetFailsWhenNoPersistencePathAvailable) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_no_persist";

    auto backend = std::make_unique<UnhealthyBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {9, 8, 7};
    EXPECT_FALSE(engine.Set("persist:none", data));
    EXPECT_FALSE(engine.Get("persist:none").has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEngineSetSemanticsTest, SetSucceedsWhenQueueAcceptsWithoutL2) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_queue_only";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {1, 2, 3, 4};
    EXPECT_TRUE(engine.Set("persist:queue", data));

    auto result = engine.Get("persist:queue");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, data);

    StorageEngine::Shutdown();
}

TEST(StorageEngineValidationTest, RejectsInvalidKeysAndOversizedValues) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend)));
    auto& engine = StorageEngine::Instance();

    const std::string empty_key;
    const std::string oversized_key(513, 'k');
    const std::string key_with_nul("abc\0def", 7);
    const std::vector<uint8_t> small_value = {1, 2, 3};
    const std::vector<uint8_t> oversized_value(
        4 * 1024 * 1024 + 1, 0xAB);

    EXPECT_FALSE(engine.Set(empty_key, small_value));
    EXPECT_FALSE(engine.Set(oversized_key, small_value));
    EXPECT_FALSE(engine.Set(key_with_nul, small_value));
    EXPECT_FALSE(engine.Set("value:too_big", oversized_value));
    EXPECT_FALSE(engine.SetSync(empty_key, small_value));
    EXPECT_FALSE(engine.Update(
        empty_key,
        [](const std::optional<VersionedData>&) -> std::optional<std::vector<uint8_t>> {
            return std::vector<uint8_t>{1};
        }));

    EXPECT_FALSE(engine.Get(empty_key).has_value());
    EXPECT_FALSE(engine.LoadFromDB(empty_key).has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEngineTtlTest, L1EntryExpiresWhenTtlConfigured) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l1_ttl_seconds = 1;
    config.l2_path = "/dev/null/mir2_storage_engine_ttl";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {4, 3, 2, 1};
    ASSERT_TRUE(engine.Set("ttl:key", data));
    ASSERT_TRUE(engine.Get("ttl:key").has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    EXPECT_FALSE(engine.Get("ttl:key").has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEngineLoadFromDBCircuitBreakerTest, StopsProbingUnhealthyBackendAfterThreshold) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.circuit_breaker_threshold = 2;
    config.circuit_breaker_timeout_ms = 60 * 1000;

    auto backend = std::make_unique<CountingBackend>();
    backend->healthy.store(false, std::memory_order_relaxed);
    auto* backend_ptr = backend.get();

    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    for (int i = 0; i < 6; ++i) {
        EXPECT_FALSE(engine.LoadFromDB("breaker:key").has_value());
    }

    EXPECT_EQ(backend_ptr->load_calls.load(std::memory_order_relaxed), 0U);
    EXPECT_LE(backend_ptr->health_checks.load(std::memory_order_relaxed), 3U);

    StorageEngine::Shutdown();
}

TEST(StorageEngineSensitiveAccountCacheTest,
     AccountPayloadLoadedFromDBDoesNotPopulateReadCaches) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_sensitive_account";

    auto backend = std::make_unique<AccountPayloadBackend>("alice");
    auto* backend_ptr = backend.get();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const std::string key = mir2::db::BuildAccountStorageKey("alice");
    auto first = engine.LoadFromDB(key);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(backend_ptr->load_calls.load(std::memory_order_relaxed), 1U);

    // Sensitive account payload should not be retained in L1/L2 read caches.
    EXPECT_FALSE(engine.Get(key).has_value());

    auto second = engine.LoadFromDB(key);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(backend_ptr->load_calls.load(std::memory_order_relaxed), 2U);

    StorageEngine::Shutdown();
}

TEST(CircuitBreakerHotPathTest, IsOpenTransitionsToHalfOpenWithoutGetStateUpgradePath) {
    using mir2::storage_engine::utils::CircuitBreaker;

    CircuitBreaker::Config config{
        .failure_threshold = 1,
        .failure_rate_threshold = 1.0,
        .min_requests = 1,
        .open_timeout_ms = 20,
        .half_open_max_requests = 1,
        .half_open_success_threshold = 1,
    };
    CircuitBreaker breaker(config);

    breaker.OnFailure();
    EXPECT_TRUE(breaker.IsOpen());
    EXPECT_EQ(breaker.GetState(), CircuitBreaker::State::OPEN);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    EXPECT_FALSE(breaker.IsOpen());
    EXPECT_EQ(breaker.GetState(), CircuitBreaker::State::HALF_OPEN);
}

// ===== FNV-1a哈希分布测试 =====
TEST(FNV1aHashTest, Distribution) {
    // 验证连续key的哈希分布
    constexpr size_t kStripes = 64;
    std::array<int, kStripes> stripe_counts{};

    for (int i = 0; i < 10000; ++i) {
        std::string key = "player:" + std::to_string(i) + ":gold";

        // 模拟FNV-1a哈希
        constexpr uint64_t kFNVPrime = 1099511628211ULL;
        constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;

        uint64_t hash = kFNVOffsetBasis;
        for (char c : key) {
            hash ^= static_cast<uint64_t>(c);
            hash *= kFNVPrime;
        }

        size_t stripe = hash & (kStripes - 1);
        stripe_counts[stripe]++;
    }

    // 验证分布均匀性
    int max_count = *std::max_element(stripe_counts.begin(), stripe_counts.end());
    int min_count = *std::min_element(stripe_counts.begin(), stripe_counts.end());

    double variance_ratio = static_cast<double>(max_count) / min_count;

    // 允许20%偏差
    ASSERT_LT(variance_ratio, 1.5);
}

namespace {

void SetBenchmarkOnlyEnv() {
#ifdef _WIN32
    _putenv_s("LEGEND2_BENCHMARK_ONLY", "1");
#else
    setenv("LEGEND2_BENCHMARK_ONLY", "1", 1);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    bool benchmark_only = false;
    std::vector<char*> filtered_args;
    filtered_args.reserve(static_cast<size_t>(argc));
    if (argc > 0) {
        filtered_args.push_back(argv[0]);
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--benchmark-only") == 0) {
            benchmark_only = true;
            continue;
        }
        filtered_args.push_back(argv[i]);
    }

    int filtered_argc = static_cast<int>(filtered_args.size());
    ::testing::InitGoogleTest(&filtered_argc, filtered_args.data());

    if (benchmark_only) {
        SetBenchmarkOnlyEnv();
        if (::testing::GTEST_FLAG(filter).empty() ||
            ::testing::GTEST_FLAG(filter) == "*") {
            ::testing::GTEST_FLAG(filter) =
                "KcpPerformanceTest.*:KcpLossyPerformanceTest.*:"
                "KcpConcurrencyPerformanceTest.*";
        }
    }

    return RUN_ALL_TESTS();
}
