#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "log/logger.h"
#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterEntityManager;
using mir2::ecs::DirtyComponent;
namespace dirty_tracker = mir2::ecs::dirty_tracker;
using mir2::storage_engine::Priority;
using mir2::storage_engine::StorageEngine;

class OutboxBlockingBackend : public mir2::storage_engine::test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&,
                     uint64_t,
                     const std::vector<uint8_t>&) override {
    sync_save_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatch(const BatchItems&) override {
    const uint32_t call_index =
        batch_save_calls.fetch_add(1, std::memory_order_relaxed);
    if (call_index == 0) {
      return StorageResult{false, "forced first batch save failure", 0};
    }
    return StorageResult{true, "", 0};
  }

  std::atomic<uint32_t> sync_save_calls{0};
  std::atomic<uint32_t> batch_save_calls{0};
};

class BatchCountingBackend : public mir2::storage_engine::test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&,
                     uint64_t,
                     const std::vector<uint8_t>&) override {
    save_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatch(const BatchItems& items) override {
    batch_save_calls.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    batch_sizes_.push_back(items.size());
    return StorageResult{true, "", 0};
  }

  std::vector<size_t> BatchSizes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return batch_sizes_;
  }

  std::atomic<uint32_t> save_calls{0};
  std::atomic<uint32_t> batch_save_calls{0};

 private:
  mutable std::mutex mutex_;
  std::vector<size_t> batch_sizes_;
};

StorageEngine::Config BuildOutboxTestConfig(const std::string& l2_path) {
  StorageEngine::Config config;
  config.l2_path = l2_path;
  config.enable_outbox = true;
  config.outbox_max_items = 1;
  config.auto_sync_interval_ms = 10;
  config.queue_retry_count = 0;
  config.queue_retry_delay_ms = 0;
  config.enable_strict_write_guarantee = false;
  config.sync_write_key_prefixes = {"char:"};
  config.critical_key_prefixes = {"char:"};
  return config;
}

StorageEngine::Config BuildDeferredFlushConfig(const std::string& l2_path) {
  auto config = BuildOutboxTestConfig(l2_path);
  config.auto_sync_interval_ms = 60000;
  config.batch_size = 8;
  return config;
}

std::string BuildTempPath(const char* prefix) {
  return std::string("/tmp/") + prefix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

}  // namespace

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyReturnsNotDirtyWhenClean) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    manager.GetOrCreate(1);

    auto result = manager.SaveIfDirty(1);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kNotDirty);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyReturnsEntityNotFoundForInvalidId) {
    entt::registry registry;
    CharacterEntityManager manager(registry);

    auto result = manager.SaveIfDirty(999);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kEntityNotFound);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyClearsDirtyFlagOnSuccess) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(2);

    dirty_tracker::mark_attributes_dirty(registry, entity);
    ASSERT_TRUE(dirty_tracker::is_dirty(registry, entity));

    auto result = manager.SaveIfDirty(2);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSuccess);
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyStoresUpdatedData) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(3);

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.hp = 42;
    dirty_tracker::mark_attributes_dirty(registry, entity);

    auto result = manager.SaveIfDirty(3);
    auto stored = manager.GetStoredData(3);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSuccess);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->stats.hp, 42);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyReturnsNotDirtyWhenDirtyComponentHasNoFlags) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(4);

    registry.emplace<DirtyComponent>(entity);

    auto result = manager.SaveIfDirty(4);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kNotDirty);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyHandlesMultipleDirtyFlags) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(5);

    dirty_tracker::mark_state_dirty(registry, entity);
    dirty_tracker::mark_attributes_dirty(registry, entity);

    auto result = manager.SaveIfDirty(5);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSuccess);
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveAllDirtyOnlySavesDirtyEntities) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto dirty_entity = manager.GetOrCreate(10);
    auto clean_entity = manager.GetOrCreate(11);

    auto clean_before = manager.GetStoredData(11);
    ASSERT_TRUE(clean_before.has_value());

    auto& dirty_attr = registry.get<CharacterAttributesComponent>(dirty_entity);
    dirty_attr.hp = 88;
    dirty_tracker::mark_attributes_dirty(registry, dirty_entity);

    auto& clean_attr = registry.get<CharacterAttributesComponent>(clean_entity);
    clean_attr.hp = 12;

    manager.SaveAllDirty();

    auto dirty_after = manager.GetStoredData(10);
    auto clean_after = manager.GetStoredData(11);

    ASSERT_TRUE(dirty_after.has_value());
    ASSERT_TRUE(clean_after.has_value());
    EXPECT_EQ(dirty_after->stats.hp, 88);
    EXPECT_EQ(clean_after->stats.hp, clean_before->stats.hp);
}

TEST(CharacterEntityManagerDirtyTest, SaveAllDirtyClearsDirtyFlagOnSuccess) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(12);

    dirty_tracker::mark_state_dirty(registry, entity);

    manager.SaveAllDirty();

    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveAllDirtySkipsEntitiesWithoutIdentity) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = registry.create();

    registry.emplace<DirtyComponent>(entity).state_dirty = true;

    manager.SaveAllDirty();

    EXPECT_FALSE(manager.GetStoredData(1).has_value());
}

TEST(CharacterEntityManagerDirtyTest, UpdateUsesSaveIfDirty) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    manager.SetSaveIntervalSeconds(0.1f);
    auto entity = manager.GetOrCreate(20);
    manager.OnLogin(20);

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.hp = 77;
    dirty_tracker::mark_attributes_dirty(registry, entity);

    manager.Update(0.11f);

    auto stored = manager.GetStoredData(20);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->stats.hp, 77);
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyKeepsDirtyWhenAsyncDurableWriteFails) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(30);

    if (StorageEngine::IsInitialized()) {
      StorageEngine::Shutdown();
    }

    const std::string l2_path = BuildTempPath("mir2_char_manager_outbox_full");
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    auto backend = std::make_unique<OutboxBlockingBackend>();
    auto config = BuildOutboxTestConfig(l2_path);
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    ASSERT_TRUE(engine.Set("normal:key:1", std::vector<uint8_t>{1}, Priority::NORMAL));
    ASSERT_TRUE(engine.Flush(2000));

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.hp = 66;
    dirty_tracker::mark_attributes_dirty(registry, entity);
    ASSERT_TRUE(dirty_tracker::is_dirty(registry, entity));

    auto result = manager.SaveIfDirty(30);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSaveFailed);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
}

TEST(CharacterEntityManagerDirtyTest,
     OnDisconnectPersistsMovedCharacterSnapshotToStorageEngineL2) {
    entt::registry registry;
    CharacterEntityManager manager(registry);

    if (StorageEngine::IsInitialized()) {
      StorageEngine::Shutdown();
    }

    const std::string l2_path = BuildTempPath("mir2_char_manager_disconnect_persist");
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);

    auto backend = std::make_unique<mir2::storage_engine::test::NoopStorageBackend>();
    StorageEngine::Config config;
    config.l2_path = l2_path;
    config.enable_metrics = false;
    config.auto_sync_interval_ms = 10;
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

    auto& engine = StorageEngine::Instance();
    auto entity = manager.GetOrCreate(1000);
    ASSERT_TRUE(registry.valid(entity));

    manager.OnLogin(1000);
    ASSERT_TRUE(manager.SetPosition(1000, 101, 100, 1));
    ASSERT_TRUE(dirty_tracker::is_dirty(registry, entity));

    manager.OnDisconnect(1000);
    ASSERT_TRUE(engine.Flush(2000));

    auto stored = engine.Get("char:1000");
    ASSERT_TRUE(stored.has_value());

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
}

TEST(CharacterEntityManagerDirtyTest,
     OnDisconnectReturnsDirtyPersistResultWithoutFlushAfterSaveLog) {
    entt::registry registry;
    CharacterEntityManager manager(registry);

    if (StorageEngine::IsInitialized()) {
      StorageEngine::Shutdown();
    }

    const std::string l2_path = BuildTempPath("mir2_char_manager_disconnect_result");
    const auto log_dir = std::filesystem::path(l2_path).parent_path() / "logs";
    std::error_code ec;
    std::filesystem::remove_all(l2_path, ec);
    std::filesystem::remove_all(log_dir, ec);

    auto backend = std::make_unique<BatchCountingBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend),
                                          BuildDeferredFlushConfig(l2_path)));
    ASSERT_TRUE(mir2::log::Logger::Instance().Initialize(log_dir.string(), "info", 1, 2));

    const auto entity = manager.GetOrCreate(2000);
    ASSERT_TRUE(registry.valid(entity));
    manager.OnLogin(2000);

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.hp = 77;
    dirty_tracker::mark_attributes_dirty(registry, entity);
    ASSERT_TRUE(dirty_tracker::is_dirty(registry, entity));

    const auto result = manager.OnDisconnect(2000);

    EXPECT_TRUE(result.was_dirty_before_save);
    EXPECT_EQ(result.save_result, CharacterEntityManager::SaveResult::kSuccess);
    mir2::log::Logger::Instance().Shutdown();
    const auto system_log = ReadTextFile(log_dir / "system.log");
    EXPECT_EQ(system_log.find("flush_after_save"), std::string::npos);

    StorageEngine::Shutdown();
    std::filesystem::remove_all(l2_path, ec);
    std::filesystem::remove_all(log_dir, ec);
}
