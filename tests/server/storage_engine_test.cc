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
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
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

class SaveCountingBackend : public test::NoopStorageBackend {
public:
    IStorageBackend::StorageResult Save(
        const std::string&,
        uint64_t,
        const std::vector<uint8_t>&) override {
        save_calls.fetch_add(1, std::memory_order_relaxed);
        return IStorageBackend::StorageResult{true, "", 0};
    }

    IStorageBackend::StorageResult SaveBatch(
        const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
        override {
        save_batch_calls.fetch_add(1, std::memory_order_relaxed);
        return IStorageBackend::StorageResult{true, "", 0};
    }

    std::atomic<uint32_t> save_calls{0};
    std::atomic<uint32_t> save_batch_calls{0};
};

class FailingSaveBackend : public test::NoopStorageBackend {
public:
    IStorageBackend::StorageResult Save(
        const std::string&,
        uint64_t,
        const std::vector<uint8_t>&) override {
        save_calls.fetch_add(1, std::memory_order_relaxed);
        return IStorageBackend::StorageResult{false, "forced save failure", 0};
    }

    IStorageBackend::StorageResult SaveBatch(
        const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
        override {
        return IStorageBackend::StorageResult{false, "forced batch failure", 0};
    }

    bool IsHealthy() const override {
        return true;
    }

    std::atomic<uint32_t> save_calls{0};
};

class FailingValidateBackend : public test::NoopStorageBackend {
 public:
  IStorageBackend::StorageResult Validate() override {
    return IStorageBackend::StorageResult{false, "forced validate failure", 0};
  }
};

struct DurableToggleBackendState {
    mutable std::mutex mutex;
    std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>> store;
    std::atomic<bool> healthy{true};
    std::atomic<uint32_t> save_calls{0};
};

class DurableToggleBackend : public IStorageBackend {
public:
    explicit DurableToggleBackend(std::shared_ptr<DurableToggleBackendState> state)
        : state_(std::move(state)) {}

    StorageResult Save(const std::string& key,
                       uint64_t version,
                       const std::vector<uint8_t>& data) override {
        if (!IsHealthy()) {
            return StorageResult{false, "backend not healthy", 0};
        }
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->store.find(key);
        if (it == state_->store.end() || it->second.first < version) {
            state_->store[key] = std::make_pair(version, data);
        }
        state_->save_calls.fetch_add(1, std::memory_order_relaxed);
        return StorageResult{true, "", 0};
    }

    StorageResult SaveBatch(
        const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items)
        override {
        if (!IsHealthy()) {
            return StorageResult{false, "backend not healthy", 0};
        }
        std::lock_guard<std::mutex> lock(state_->mutex);
        for (const auto& [key, version, data] : items) {
            auto it = state_->store.find(key);
            if (it == state_->store.end() || it->second.first < version) {
                state_->store[key] = std::make_pair(version, data);
            }
            state_->save_calls.fetch_add(1, std::memory_order_relaxed);
        }
        return StorageResult{true, "", 0};
    }

    std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
        const std::string& key) override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->store.find(key);
        if (it == state_->store.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
        override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->store;
    }

    StorageResult Validate() override {
        return StorageResult{true, "", 0};
    }

    bool IsHealthy() const override {
        return state_->healthy.load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<DurableToggleBackendState> state_;
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

class TestInvalidationAdapter : public StorageEngine::IInvalidationAdapter {
public:
    bool Start(StorageEngine::InvalidationInboundHandler inbound_handler) override {
        if (!start_success_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = true;
        stopped_ = false;
        inbound_handler_ = std::move(inbound_handler);
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        inbound_handler_ = nullptr;
    }

    bool Publish(const StorageEngine::InvalidationEvent& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        published_events_.push_back(event);
        return publish_success_;
    }

    void SetStartSuccess(bool value) {
        start_success_ = value;
    }

    void SetPublishSuccess(bool value) {
        publish_success_ = value;
    }

    bool EmitInbound(const StorageEngine::InvalidationEvent& event) {
        StorageEngine::InvalidationInboundHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = inbound_handler_;
        }
        if (!handler) {
            return false;
        }
        handler(event);
        return true;
    }

    size_t published_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return published_events_.size();
    }

    std::optional<StorageEngine::InvalidationEvent> published_at(
        size_t index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= published_events_.size()) {
            return std::nullopt;
        }
        return published_events_[index];
    }

    bool started() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return started_;
    }

    bool stopped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopped_;
    }

private:
    mutable std::mutex mutex_;
    bool start_success_ = true;
    bool publish_success_ = true;
    bool started_ = false;
    bool stopped_ = false;
    StorageEngine::InvalidationInboundHandler inbound_handler_;
    std::vector<StorageEngine::InvalidationEvent> published_events_;
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

TEST_F(StorageEngineTest, CompareAndSetUpdatesWhenVersionMatches) {
    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> original = {1, 2, 3};
    ASSERT_TRUE(engine.Set("cas:key", original));

    auto current = engine.Get("cas:key");
    ASSERT_TRUE(current.has_value());
    const uint64_t expected_version = current->version;

    const std::vector<uint8_t> updated = {9, 9, 9};
    ASSERT_TRUE(engine.CompareAndSet("cas:key", expected_version, updated));

    auto after = engine.Get("cas:key");
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->data, updated);
}

TEST_F(StorageEngineTest, CompareAndSetRejectsVersionMismatch) {
    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> original = {1, 2, 3};
    ASSERT_TRUE(engine.Set("cas:mismatch", original));
    auto current = engine.Get("cas:mismatch");
    ASSERT_TRUE(current.has_value());

    const std::vector<uint8_t> updated = {4, 5, 6};
    EXPECT_FALSE(
        engine.CompareAndSet("cas:mismatch", current->version + 100, updated));

    auto after = engine.Get("cas:mismatch");
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->data, original);
}

TEST_F(StorageEngineTest, BatchSetAndBatchGetRoundTrip) {
    auto& engine = StorageEngine::Instance();
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> kvs = {
        {"batch:a", {1, 1}},
        {"batch:b", {2, 2}},
        {"batch:c", {3, 3}},
    };
    ASSERT_TRUE(engine.BatchSet(kvs));

    const auto result =
        engine.BatchGet({"batch:a", "batch:b", "batch:c", "batch:missing"});
    ASSERT_EQ(result.size(), 4u);
    ASSERT_TRUE(result[0].has_value());
    ASSERT_TRUE(result[1].has_value());
    ASSERT_TRUE(result[2].has_value());
    EXPECT_EQ(result[0]->data, (std::vector<uint8_t>{1, 1}));
    EXPECT_EQ(result[1]->data, (std::vector<uint8_t>{2, 2}));
    EXPECT_EQ(result[2]->data, (std::vector<uint8_t>{3, 3}));
    EXPECT_FALSE(result[3].has_value());
}

TEST_F(StorageEngineTest, PutAndDeleteRoundTrip) {
    auto& engine = StorageEngine::Instance();

    WriteOptions write_options;
    write_options.durability = WriteDurability::kBestEffort;
    write_options.priority = Priority::NORMAL;
    ASSERT_TRUE(engine.Put("phase1:key", {4, 5, 6}, write_options));

    auto loaded = engine.Get("phase1:key");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->data, (std::vector<uint8_t>{4, 5, 6}));

    DeleteOptions delete_options;
    EXPECT_TRUE(engine.Delete("phase1:key", delete_options));
    EXPECT_FALSE(engine.Get("phase1:key").has_value());
}

TEST_F(StorageEngineTest, BatchWriteReturnsPerItemFailureDetails) {
    auto& engine = StorageEngine::Instance();

    std::vector<BatchWriteItem> items;
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "phase1:batch:ok",
        .value = {1, 2, 3},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "",
        .value = {9},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kDelete,
        .key = "phase1:batch:missing",
        .value = {},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });

    const auto result = engine.BatchWrite(items);
    EXPECT_EQ(result.total, 3U);
    EXPECT_EQ(result.succeeded, 2U);
    EXPECT_EQ(result.failed, 1U);
    ASSERT_EQ(result.failed_keys.size(), 1U);
    EXPECT_TRUE(result.failed_keys[0].empty());
    ASSERT_EQ(result.failure_reasons.size(), 1U);
    EXPECT_FALSE(result.failure_reasons[0].empty());
}

TEST(StorageEnginePhase1ApiTest,
     PutBestEffortWithBypassSyncPrefixAvoidsImmediateSyncCompensation) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_phase1_bypass";
    config.enable_strict_write_guarantee = false;
    config.auto_sync_interval_ms = 60000;
    config.sync_write_key_prefixes = {"char:"};

    auto backend = std::make_unique<SaveCountingBackend>();
    auto* backend_ptr = backend.get();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    WriteOptions options;
    options.durability = WriteDurability::kBestEffort;
    options.priority = Priority::NORMAL;
    options.bypass_sync_prefix_upgrade = true;

    ASSERT_TRUE(engine.Put("char:phase1:bypass", {7, 7, 7}, options));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(backend_ptr->save_calls.load(std::memory_order_relaxed), 0U);

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest, ValidateStorageSurfacesBackendValidationFailure) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    auto backend = std::make_unique<FailingValidateBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend)));

    auto& engine = StorageEngine::Instance();
    const auto report = engine.ValidateStorage();
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.summary.find("forced validate failure"), std::string::npos);

    StorageEngine::Shutdown();
}

TEST_F(StorageEngineTest, BatchWriteReportsStructuredFailureReasonCode) {
    auto& engine = StorageEngine::Instance();

    std::vector<BatchWriteItem> items;
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "",
        .value = {1},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });

    const auto result = engine.BatchWrite(items);
    EXPECT_EQ(result.total, 1U);
    EXPECT_EQ(result.failed, 1U);
    ASSERT_EQ(result.failure_reason_codes.size(), 1U);
    EXPECT_EQ(result.failure_reason_codes[0], WriteRejectReason::kInvalidKey);
}

TEST(StorageEnginePhase1ApiTest, PutAndDeleteWithAccessHonorTokenPolicy) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext denied{
        .principal = "bob",
        .access_token = "bad",
        .trusted = false,
    };
    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    WriteOptions options;
    options.durability = WriteDurability::kBestEffort;
    options.priority = Priority::NORMAL;
    EXPECT_FALSE(engine.PutWithAccess("phase1:acl:key", {8, 8}, denied, options));
    EXPECT_TRUE(engine.PutWithAccess("phase1:acl:key", {9, 9}, allowed, options));

    DeleteOptions delete_options;
    EXPECT_FALSE(engine.DeleteWithAccess("phase1:acl:key", denied, delete_options));
    EXPECT_TRUE(engine.DeleteWithAccess("phase1:acl:key", allowed, delete_options));

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchWriteWithAccessAllowsAuthorizedItemsAndReportsPerItemFailure) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    std::vector<BatchWriteItem> items;
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "phase1:acl:batch:ok",
        .value = {1, 2, 3},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "",
        .value = {9},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kDelete,
        .key = "phase1:acl:batch:missing",
        .value = {},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });

    const auto result = engine.BatchWriteWithAccess(items, allowed);
    EXPECT_EQ(result.total, 3U);
    EXPECT_EQ(result.succeeded, 2U);
    EXPECT_EQ(result.failed, 1U);
    ASSERT_EQ(result.failed_keys.size(), 1U);
    EXPECT_TRUE(result.failed_keys[0].empty());
    ASSERT_EQ(result.failure_reason_codes.size(), 1U);
    EXPECT_EQ(result.failure_reason_codes[0], WriteRejectReason::kInvalidKey);

    auto loaded = engine.Get("phase1:acl:batch:ok");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->data, (std::vector<uint8_t>{1, 2, 3}));

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchSetWithAccessReturnsFalseButKeepsAllowedWritesWhenOneItemInvalid) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    const std::vector<std::pair<std::string, std::vector<uint8_t>>> kvs = {
        {"phase1:acl:batchset:ok", {7, 7, 7}},
        {"", {9}},
    };

    EXPECT_FALSE(engine.BatchSetWithAccess(kvs, allowed, Priority::NORMAL));

    auto loaded = engine.Get("phase1:acl:batchset:ok");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->data, (std::vector<uint8_t>{7, 7, 7}));

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchGetWithAccessReturnsPerItemResultsWhenInvalidKeyPresent) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    WriteOptions options;
    options.durability = WriteDurability::kBestEffort;
    options.priority = Priority::NORMAL;
    ASSERT_TRUE(engine.PutWithAccess("phase1:acl:batchget:ok", {5, 5, 5},
                                     allowed, options));

    const auto result = engine.BatchGetWithAccess(
        {"phase1:acl:batchget:ok", ""}, allowed);
    ASSERT_EQ(result.size(), 2U);
    ASSERT_TRUE(result[0].has_value());
    EXPECT_EQ(result[0]->data, (std::vector<uint8_t>{5, 5, 5}));
    EXPECT_FALSE(result[1].has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchSetWithAccessRespectsLegacyPathWhenNewWritePathDisabled) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";
    config.enable_new_write_path = false;

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    const std::vector<std::pair<std::string, std::vector<uint8_t>>> kvs = {
        {"phase1:acl:legacy:ok", {4, 4, 4}},
        {"", {1}},
    };
    EXPECT_FALSE(engine.BatchSetWithAccess(kvs, allowed, Priority::NORMAL));
    EXPECT_FALSE(engine.Get("phase1:acl:legacy:ok").has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     LegacyBatchSetWithAccessDenialEmitsBatchAuditSummary) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";
    config.enable_new_write_path = false;

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    const std::vector<std::pair<std::string, std::vector<uint8_t>>> kvs = {
        {"phase1:acl:legacy:audit:ok", {1, 1, 1}},
        {"", {9}},
    };
    EXPECT_FALSE(engine.BatchSetWithAccess(kvs, allowed, Priority::NORMAL));

    const auto audit = engine.GetRecentAuditEntries(32);
    bool has_item_denied = false;
    bool has_batch_summary = false;
    for (const auto& entry : audit) {
        if (entry.operation == "batch_set" &&
            entry.key.empty() &&
            !entry.success &&
            entry.reason == "invalid_key") {
            has_item_denied = true;
        }
        if (entry.operation == "batch_set" &&
            entry.key == "<batch>" &&
            !entry.success) {
            has_batch_summary = true;
        }
    }
    EXPECT_TRUE(has_item_denied);
    EXPECT_TRUE(has_batch_summary);

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchGetWithAccessRespectsLegacyPathWhenNewWritePathDisabled) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";
    config.enable_new_write_path = false;

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    WriteOptions options;
    options.durability = WriteDurability::kBestEffort;
    options.priority = Priority::NORMAL;
    ASSERT_TRUE(engine.PutWithAccess("phase1:acl:legacy:batchget:ok", {2, 2, 2},
                                     allowed, options));

    const auto result = engine.BatchGetWithAccess(
        {"phase1:acl:legacy:batchget:ok", ""}, allowed);
    ASSERT_EQ(result.size(), 2U);
    EXPECT_FALSE(result[0].has_value());
    EXPECT_FALSE(result[1].has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchWriteWithAccessRespectsLegacyPathWhenNewWritePathDisabled) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";
    config.enable_new_write_path = false;

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    std::vector<BatchWriteItem> items;
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "phase1:acl:legacy:batchwrite:ok",
        .value = {3, 3, 3},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "",
        .value = {9},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });

    const auto result = engine.BatchWriteWithAccess(items, allowed);
    EXPECT_EQ(result.total, 2U);
    EXPECT_EQ(result.succeeded, 0U);
    EXPECT_EQ(result.failed, 2U);
    EXPECT_FALSE(engine.Get("phase1:acl:legacy:batchwrite:ok").has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEnginePhase1ApiTest,
     BatchWriteWithAccessEmitsItemAuditForWriteStageFailure) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "phase1-token";
    config.enable_new_write_path = true;

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
    auto& engine = StorageEngine::Instance();

    const StorageEngine::AccessContext allowed{
        .principal = "bob",
        .access_token = "phase1-token",
        .trusted = false,
    };

    std::vector<BatchWriteItem> items;
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "phase1:acl:audit:ok",
        .value = {1, 2, 3},
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });
    items.push_back(BatchWriteItem{
        .op = BatchWriteItem::Op::kPut,
        .key = "phase1:acl:audit:oversize",
        .value = std::vector<uint8_t>(4 * 1024 * 1024 + 1, 0xAB),
        .write_options = WriteOptions{},
        .delete_options = DeleteOptions{},
    });

    const auto result = engine.BatchWriteWithAccess(items, allowed);
    EXPECT_EQ(result.total, 2U);
    EXPECT_EQ(result.succeeded, 1U);
    EXPECT_EQ(result.failed, 1U);

    const auto audit = engine.GetRecentAuditEntries(64);
    bool has_item_failure = false;
    for (const auto& entry : audit) {
        if (entry.operation == "batch_write" &&
            entry.key == "phase1:acl:audit:oversize" &&
            !entry.success &&
            entry.reason == "value_too_large") {
            has_item_failure = true;
            break;
        }
    }
    EXPECT_TRUE(has_item_failure);

    StorageEngine::Shutdown();
}

TEST_F(StorageEngineTest, InvalidateAndInvalidateByPrefix) {
    auto& engine = StorageEngine::Instance();
    ASSERT_TRUE(engine.Set("inv:one", {1}));
    ASSERT_TRUE(engine.Set("inv:two", {2}));
    ASSERT_TRUE(engine.Set("other:key", {3}));

    EXPECT_TRUE(engine.Invalidate("inv:one"));
    EXPECT_FALSE(engine.Get("inv:one").has_value());

    const size_t removed = engine.InvalidateByPrefix("inv:");
    EXPECT_GE(removed, 1u);
    EXPECT_FALSE(engine.Get("inv:two").has_value());
    EXPECT_TRUE(engine.Get("other:key").has_value());
}

TEST_F(StorageEngineTest, InvalidationSubscriberReceivesEvents) {
    auto& engine = StorageEngine::Instance();
    ASSERT_TRUE(engine.Set("sub:key", {1, 2, 3}));

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<StorageEngine::InvalidationEvent> events;
    const uint64_t subscription_id = engine.SubscribeInvalidation(
        [&](const StorageEngine::InvalidationEvent& event) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
            cv.notify_all();
        });
    ASSERT_NE(subscription_id, 0u);

    ASSERT_TRUE(engine.Invalidate("sub:key"));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return !events.empty();
        }));
    }
    ASSERT_TRUE(engine.UnsubscribeInvalidation(subscription_id));

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front().type, StorageEngine::InvalidationEventType::kKey);
    EXPECT_EQ(events.front().key_or_prefix, "sub:key");
    EXPECT_FALSE(events.front().from_broadcast);
}

TEST_F(StorageEngineTest, BroadcastInvalidationRemovesData) {
    auto& engine = StorageEngine::Instance();
    ASSERT_TRUE(engine.Set("remote:key", {9, 8, 7}));
    ASSERT_TRUE(engine.Get("remote:key").has_value());

    ASSERT_TRUE(engine.InvalidateFromBroadcast("remote:key"));
    EXPECT_FALSE(engine.Get("remote:key").has_value());
}

TEST_F(StorageEngineTest, InvalidationAdapterPublishesLocalEvents) {
    auto& engine = StorageEngine::Instance();
    auto adapter = std::make_shared<TestInvalidationAdapter>();
    ASSERT_TRUE(engine.SetInvalidationAdapter(adapter));
    ASSERT_TRUE(adapter->started());

    ASSERT_TRUE(engine.Set("adapter:local:key", {1, 2}));
    ASSERT_TRUE(engine.Invalidate("adapter:local:key"));

    ASSERT_EQ(adapter->published_count(), 1u);
    const auto event = adapter->published_at(0);
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, StorageEngine::InvalidationEventType::kKey);
    EXPECT_EQ(event->key_or_prefix, "adapter:local:key");
    EXPECT_FALSE(event->from_broadcast);

    engine.ClearInvalidationAdapter();
    EXPECT_TRUE(adapter->stopped());
}

TEST_F(StorageEngineTest, InvalidationAdapterInboundEventIsAppliedLocally) {
    auto& engine = StorageEngine::Instance();
    auto adapter = std::make_shared<TestInvalidationAdapter>();
    ASSERT_TRUE(engine.SetInvalidationAdapter(adapter));

    ASSERT_TRUE(engine.Set("adapter:inbound:key", {9, 9, 9}));
    ASSERT_TRUE(engine.Get("adapter:inbound:key").has_value());

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<StorageEngine::InvalidationEvent> events;
    const uint64_t subscription_id = engine.SubscribeInvalidation(
        [&](const StorageEngine::InvalidationEvent& event) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
            cv.notify_all();
        });
    ASSERT_NE(subscription_id, 0u);

    ASSERT_TRUE(adapter->EmitInbound(StorageEngine::InvalidationEvent{
        .type = StorageEngine::InvalidationEventType::kKey,
        .key_or_prefix = "adapter:inbound:key",
        .timestamp_ms = 0,
        .from_broadcast = false,
    }));

    EXPECT_FALSE(engine.Get("adapter:inbound:key").has_value());
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return !events.empty();
        }));
    }
    ASSERT_TRUE(engine.UnsubscribeInvalidation(subscription_id));
    ASSERT_FALSE(events.empty());
    EXPECT_TRUE(events.front().from_broadcast);
    EXPECT_EQ(events.front().key_or_prefix, "adapter:inbound:key");
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

TEST(StorageEngineSetSemanticsTest, SetFailsWhenL2DownEvenIfQueueAcceptsInStrictMode) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_strict_queue_blocked";

    auto backend = std::make_unique<FailingSaveBackend>();
    auto* backend_ptr = backend.get();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {1, 2, 3, 4};
    EXPECT_FALSE(engine.Set("persist:queue", data));
    EXPECT_GE(backend_ptr->save_calls.load(std::memory_order_relaxed), 1U);

    StorageEngine::Shutdown();
}

TEST(StorageEngineSetSemanticsTest, QueueOnlySuccessCanBeEnabledByConfigForRollback) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_queue_only";
    config.enable_strict_write_guarantee = false;

    auto backend = std::make_unique<FailingSaveBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {1, 2, 3, 4};
    EXPECT_TRUE(engine.Set("persist:queue", data));

    auto result = engine.Get("persist:queue");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, data);

  StorageEngine::Shutdown();
}

TEST(StorageEngineAccessControlTest, EnforcesTokenAndProducesAuditEntries) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.enable_access_control = true;
    config.require_auth_for_reads = true;
    config.access_control_token = "secret-token";
    config.l2_path = "/tmp/mir2_storage_engine_access_control_test";

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const StorageEngine::AccessContext denied_ctx{
        .principal = "alice",
        .access_token = "bad-token",
        .trusted = false};
    const StorageEngine::AccessContext allowed_ctx{
        .principal = "alice",
        .access_token = "secret-token",
        .trusted = false};

    EXPECT_FALSE(engine.SetWithAccess("secure:key", {1, 1, 1}, denied_ctx));
    EXPECT_TRUE(engine.SetWithAccess("secure:key", {2, 2, 2}, allowed_ctx));
    EXPECT_FALSE(engine.GetWithAccess("secure:key", denied_ctx).has_value());
    EXPECT_TRUE(engine.GetWithAccess("secure:key", allowed_ctx).has_value());

    const auto audit = engine.GetRecentAuditEntries(16);
    ASSERT_GE(audit.size(), 3u);
    bool has_denied = false;
    bool has_allowed = false;
    for (const auto& entry : audit) {
        if (entry.operation == "set" && !entry.success) {
            has_denied = true;
        }
        if (entry.operation == "set" && entry.success) {
            has_allowed = true;
        }
    }
    EXPECT_TRUE(has_denied);
    EXPECT_TRUE(has_allowed);

    StorageEngine::Shutdown();
}

TEST(StorageEngineSetSemanticsTest, RuntimeConfigCanDisableStrictWriteGuarantee) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_runtime_strict_toggle";
    config.enable_strict_write_guarantee = true;

    auto backend = std::make_unique<FailingSaveBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {3, 1, 4, 1, 5, 9};
    EXPECT_FALSE(engine.Set("runtime:strict:key1", data));

    StorageEngine::RuntimeTunableConfig runtime_cfg;
    runtime_cfg.enable_strict_write_guarantee = false;
    ASSERT_TRUE(engine.ApplyRuntimeConfig(runtime_cfg));

    EXPECT_TRUE(engine.Set("runtime:strict:key2", data));
    auto result = engine.Get("runtime:strict:key2");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, data);

    StorageEngine::Shutdown();
}

TEST(StorageEngineSetSemanticsTest,
     RuntimeConfigCanDisableNewWritePathAndRestoreLegacyBatchSetBehavior) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_runtime_new_write_path";
    config.enable_strict_write_guarantee = false;
    config.enable_new_write_path = true;

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();

    std::vector<std::pair<std::string, std::vector<uint8_t>>> kvs_new_path = {
        {"runtime:newpath:good", {1, 2, 3}},
        {"", {9}},
    };
    EXPECT_FALSE(engine.BatchSet(kvs_new_path));
    auto new_path_good = engine.Get("runtime:newpath:good");
    ASSERT_TRUE(new_path_good.has_value());
    EXPECT_EQ(new_path_good->data, (std::vector<uint8_t>{1, 2, 3}));

    StorageEngine::RuntimeTunableConfig runtime_cfg;
    runtime_cfg.enable_new_write_path = false;
    ASSERT_TRUE(engine.ApplyRuntimeConfig(runtime_cfg));

    std::vector<std::pair<std::string, std::vector<uint8_t>>> kvs_legacy_path = {
        {"runtime:legacy:good", {4, 5, 6}},
        {"", {8}},
    };
    EXPECT_FALSE(engine.BatchSet(kvs_legacy_path));
    EXPECT_FALSE(engine.Get("runtime:legacy:good").has_value());

    StorageEngine::Shutdown();
}

TEST(StorageEngineSetSemanticsTest, CriticalPrefixSetAutoUsesSyncPath) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_critical_prefix_sync";
    config.enable_strict_write_guarantee = false;
    config.auto_sync_interval_ms = 60000;
    config.critical_key_prefixes = {"critical:"};

    auto backend = std::make_unique<SaveCountingBackend>();
    auto* backend_ptr = backend.get();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {7, 7, 7};

    EXPECT_TRUE(engine.Set("normal:key", data));
    const uint32_t save_calls_after_normal =
        backend_ptr->save_calls.load(std::memory_order_relaxed);

    EXPECT_TRUE(engine.Set("critical:key", data));
    EXPECT_GE(backend_ptr->save_calls.load(std::memory_order_relaxed),
              save_calls_after_normal + 1);

    StorageEngine::Shutdown();
}

TEST(StorageEngineSetSemanticsTest,
     SetSyncSucceedsWithL2DurabilityWhenQueueRejectedAndBackendCompensationFails) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    const std::string l2_path =
        "/tmp/mir2_storage_engine_setsync_l2_durable_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    StorageEngine::Config config;
    config.l2_path = l2_path;
    config.enable_strict_write_guarantee = false;
    config.enable_outbox = false;
    config.auto_sync_interval_ms = 60000;

    auto backend = std::make_unique<UnhealthyBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {4, 2, 4, 2};
    EXPECT_TRUE(engine.SetSync("setsync:l2_only:key", data, Priority::NORMAL));

    auto loaded = engine.Get("setsync:l2_only:key");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->data, data);

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
}

TEST(StorageEngineSetSemanticsTest,
     SetSyncFailsWhenOutboxEnabledAndQueueRejectedAndBackendCompensationFails) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    const std::string l2_path =
        "/tmp/mir2_storage_engine_setsync_outbox_strict_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    StorageEngine::Config config;
    config.l2_path = l2_path;
    config.enable_strict_write_guarantee = false;
    config.enable_outbox = true;
    config.outbox_max_items = 1;
    config.auto_sync_interval_ms = 60000;

    auto backend = std::make_unique<UnhealthyBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    const std::vector<uint8_t> data = {4, 2, 4, 2};
    ASSERT_TRUE(engine.SetSync("setsync:outbox:prime", data, Priority::NORMAL));

    EXPECT_FALSE(engine.SetSync("setsync:outbox:full:key", data, Priority::NORMAL));

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
}

TEST(StorageEngineDurableOutboxTest, ReplaysPendingWritesAfterRestartWhenBackendRecovers) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    const std::string l2_path =
        "/tmp/mir2_storage_engine_pr3_outbox_replay_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    StorageEngine::Config config;
    config.l2_path = l2_path;
    config.enable_strict_write_guarantee = false;
    config.enable_outbox = true;
    config.auto_sync_interval_ms = 10;
    config.batch_size = 1;

    auto state = std::make_shared<DurableToggleBackendState>();
    state->healthy.store(false, std::memory_order_relaxed);

    auto backend1 = std::make_unique<DurableToggleBackend>(state);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend1), config));

    auto& engine = StorageEngine::Instance();
    const std::string key = "outbox:replay:key";
    const std::vector<uint8_t> payload = {9, 8, 7, 6};
    EXPECT_TRUE(engine.Set(key, payload, Priority::NORMAL));

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    StorageEngine::Shutdown();

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        EXPECT_EQ(state->store.size(), 0U);
    }

    state->healthy.store(true, std::memory_order_relaxed);
    auto backend2 = std::make_unique<DurableToggleBackend>(state);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend2), config));

    bool persisted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto it = state->store.find(key);
            if (it != state->store.end()) {
                persisted = true;
                EXPECT_EQ(it->second.second, payload);
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(persisted);

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
}

TEST(StorageEngineDurableOutboxTest, ReplayLimitRestrictsRecoveredItemsPerStartup) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    const std::string l2_path =
        "/tmp/mir2_storage_engine_pr3_outbox_limit_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    StorageEngine::Config config;
    config.l2_path = l2_path;
    config.enable_strict_write_guarantee = false;
    config.enable_outbox = true;
    config.outbox_replay_limit = 1;
    config.auto_sync_interval_ms = 10;
    config.batch_size = 1;

    auto state = std::make_shared<DurableToggleBackendState>();
    state->healthy.store(false, std::memory_order_relaxed);

    auto backend1 = std::make_unique<DurableToggleBackend>(state);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend1), config));
    auto& engine = StorageEngine::Instance();
    EXPECT_TRUE(engine.Set("outbox:limit:1", std::vector<uint8_t>{1}, Priority::NORMAL));
    EXPECT_TRUE(engine.Set("outbox:limit:2", std::vector<uint8_t>{2}, Priority::NORMAL));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    StorageEngine::Shutdown();

    state->healthy.store(true, std::memory_order_relaxed);
    auto backend2 = std::make_unique<DurableToggleBackend>(state);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend2), config));

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        EXPECT_EQ(state->store.size(), 1U);
    }

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
}

TEST(StorageEngineTtlIsolationTest,
     StartupRecoveryRepairsPersistentTierWithoutScanningTtlTier) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    const std::string l2_path =
        "/tmp/mir2_storage_engine_pr4_tier_recovery_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    StorageEngine::Config config;
    config.l2_path = l2_path;
    config.enable_strict_write_guarantee = false;
    config.enable_outbox = false;
    config.auto_sync_interval_ms = 60000;
    config.batch_size = 64;
    config.critical_key_prefixes = {"critical:"};

    auto state = std::make_shared<DurableToggleBackendState>();
    state->healthy.store(false, std::memory_order_relaxed);

    auto backend1 = std::make_unique<DurableToggleBackend>(state);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend1), config));
    auto& engine = StorageEngine::Instance();

    EXPECT_TRUE(engine.Set("normal:ttl:key", std::vector<uint8_t>{1, 2, 3}));
    EXPECT_TRUE(engine.Set("critical:persistent:key", std::vector<uint8_t>{9, 8, 7}));

    StorageEngine::Shutdown();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        EXPECT_TRUE(state->store.empty());
    }

    state->healthy.store(true, std::memory_order_relaxed);
    auto backend2 = std::make_unique<DurableToggleBackend>(state);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend2), config));
    auto& recovered_engine = StorageEngine::Instance();
    EXPECT_TRUE(recovered_engine.PerformStartupRecovery());

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        EXPECT_EQ(state->store.size(), 1U);
        auto it = state->store.find("critical:persistent:key");
        ASSERT_NE(it, state->store.end());
        EXPECT_EQ(it->second.second, std::vector<uint8_t>({9, 8, 7}));
        EXPECT_EQ(state->store.count("normal:ttl:key"), 0U);
    }

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
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

TEST(StorageEngineSyncWritePrefixTest,
     SyncWritePrefixRoutesToSyncPathWhileNonPrefixWriteIsRejected) {
    if (StorageEngine::IsInitialized()) {
        StorageEngine::Shutdown();
    }

    StorageEngine::Config config;
    config.l2_path = "/dev/null/mir2_storage_engine_sync_prefix";
    config.enable_strict_write_guarantee = false;
    config.queue_capacity = 0;
    config.sync_write_key_prefixes = {"sync:"};
    config.critical_key_prefixes = {"persist:"};

    auto backend = std::make_unique<SaveCountingBackend>();
    auto* backend_ptr = backend.get();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    EXPECT_TRUE(engine.Set("sync:entity:1", std::vector<uint8_t>{1, 2, 3}));
    EXPECT_EQ(backend_ptr->save_calls.load(std::memory_order_relaxed), 1U);

    EXPECT_FALSE(engine.Set("normal:entity:1", std::vector<uint8_t>{4, 5, 6}));
    EXPECT_EQ(backend_ptr->save_calls.load(std::memory_order_relaxed), 1U);

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
