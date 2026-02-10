#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "config/config_manager.h"
#include "storage_engine/backends/common/account_storage_codec.h"
#include "storage_engine/backends/account_storage_backend.h"
#include "storage_engine/interfaces/storage_backend.h"

namespace mir2::db {
namespace {

class CountingKvBackend : public mir2::storage_engine::IStorageBackend {
 public:
  StorageResult Save(const std::string& key,
                     uint64_t version,
                     const std::vector<uint8_t>& data) override {
    ++save_calls;
    last_saved_key = key;
    last_saved_version = version;
    last_saved_data = data;
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items)
      override {
    save_batch_calls += static_cast<uint32_t>(items.size());
    return StorageResult{true, "", 0};
  }

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
      const std::string& key) override {
    ++load_calls;
    last_loaded_key = key;
    return load_result;
  }

  std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
      override {
    return std::nullopt;
  }

  StorageResult Validate() override {
    return StorageResult{true, "", 0};
  }

  bool IsHealthy() const override { return true; }

  uint32_t load_calls = 0;
  uint32_t save_calls = 0;
  uint32_t save_batch_calls = 0;
  std::string last_loaded_key;
  std::string last_saved_key;
  uint64_t last_saved_version = 0;
  std::vector<uint8_t> last_saved_data;
  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> load_result;
};

TEST(AccountStorageBackendTest, LoadDelegatesNonAccountKeyToKvBackend) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  kv_ptr->load_result = std::make_pair(9u, std::vector<uint8_t>{1, 2, 3});

  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);

  auto result = backend.Load("player:1:gold");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 9u);
  EXPECT_EQ(result->second, (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_EQ(kv_ptr->load_calls, 1u);
  EXPECT_EQ(kv_ptr->last_loaded_key, "player:1:gold");
}

TEST(AccountStorageBackendTest, LoadBypassesKvBackendForAccountKey) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  kv_ptr->load_result = std::make_pair(88u, std::vector<uint8_t>{8, 8, 8});

  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);

  const std::string account_key = BuildAccountStorageKey("alice");
  auto result = backend.Load(account_key);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(kv_ptr->load_calls, 0u);
}

TEST(AccountStorageBackendTest, SaveStillDelegatesToKvBackend) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();

  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);
  const std::vector<uint8_t> payload{0xA, 0xB};

  auto save_result = backend.Save("player:2:bag", 77u, payload);
  EXPECT_TRUE(save_result.success);
  EXPECT_EQ(kv_ptr->save_calls, 1u);
  EXPECT_EQ(kv_ptr->last_saved_key, "player:2:bag");
  EXPECT_EQ(kv_ptr->last_saved_version, 77u);
  EXPECT_EQ(kv_ptr->last_saved_data, payload);
}

}  // namespace
}  // namespace mir2::db
