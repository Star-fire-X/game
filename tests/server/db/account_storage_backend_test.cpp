#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <pqxx/pqxx>

#include "config/config_manager.h"
#include "storage_engine/backends/account_storage_backend.h"
#include "storage_engine/backends/common/account_storage_codec.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"
#include "storage_engine/interfaces/storage_backend.h"

namespace mir2::db {
namespace {

std::string EnvOrDefault(const char* key, const char* default_value) {
  const char* value = std::getenv(key);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return default_value;
}

config::DatabaseConfig BuildDbConfigFromEnv() {
  config::DatabaseConfig cfg;
  cfg.host = EnvOrDefault("MIR2_DB_HOST", "127.0.0.1");
  cfg.port = static_cast<uint16_t>(std::stoi(EnvOrDefault("MIR2_DB_PORT", "5432")));
  cfg.user = EnvOrDefault("MIR2_DB_USER", "mir2");
  cfg.password = EnvOrDefault("MIR2_DB_PASSWORD", "mir2_password");
  cfg.database = EnvOrDefault("MIR2_DB_NAME", "mir2_game");
  cfg.pool_size = 2;
  return cfg;
}

AccountData MakeAccount(const std::string& username) {
  AccountData account;
  account.username = username;
  account.password_hash = "$2b$12$account.storage.backend.test";
  account.email = username + "@example.com";
  account.created_at = 1700000000000;
  account.last_login = 1700000001000;
  account.banned = false;
  return account;
}

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

  StorageResult SaveBatch(const BatchItems& items) override {
    ++save_batch_calls;
    last_save_batch_count = items.size();
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
  size_t last_save_batch_count = 0;
  std::string last_loaded_key;
  std::string last_saved_key;
  uint64_t last_saved_version = 0;
  std::vector<uint8_t> last_saved_data;
  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> load_result;
};

class AtomicCountingKvBackend : public CountingKvBackend,
                                public mir2::storage_engine::IAtomicBatchStorageBackend {
 public:
  StorageResult SaveBatchAtomic(const BatchItems& items) override {
    ++save_batch_atomic_calls;
    last_save_batch_atomic_count = items.size();
    return StorageResult{true, "", 0};
  }

  uint32_t save_batch_atomic_calls = 0;
  size_t last_save_batch_atomic_count = 0;
};

void DeleteAccountByUsername(const std::shared_ptr<PgConnectionPool>& pool,
                             const std::string& username) {
  if (!pool || !pool->IsReady()) {
    return;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    return;
  }
  PgConnectionGuard guard(*pool, conn);
  pqxx::work txn(*conn);
  txn.exec("DELETE FROM accounts WHERE username = $1", pqxx::params{username});
  txn.commit();
}

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

TEST(AccountStorageBackendTest, SaveAccountKeyDoesNotDelegateToKvBackend) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);

  AccountData account = MakeAccount("alice");
  const auto payload = EncodeAccountData(account);
  auto save_result =
      backend.Save(BuildAccountStorageKey("alice"), 100u, payload);

  EXPECT_FALSE(save_result.success);
  EXPECT_EQ(save_result.error_message, "account pool not ready");
  EXPECT_EQ(kv_ptr->save_calls, 0u);
}

TEST(AccountStorageBackendTest, SaveBatchAtomicRejectsMixedAccountAndKvKeys) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);

  AccountData account = MakeAccount("bob");
  const auto payload = EncodeAccountData(account);
  mir2::storage_engine::IStorageBackend::BatchItems items = {
      {BuildAccountStorageKey("bob"), 101u, payload},
      {"player:2:bag", 77u, std::vector<uint8_t>{1, 2}},
  };

  auto result = backend.SaveBatchAtomic(items);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message, "cross-store atomic batch unsupported");
  EXPECT_EQ(kv_ptr->save_batch_calls, 0u);
  EXPECT_EQ(kv_ptr->save_calls, 0u);
}

TEST(AccountStorageBackendTest, SaveBatchAtomicFallsBackToKvSaveBatchWhenAtomicUnsupported) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);

  mir2::storage_engine::IStorageBackend::BatchItems items = {
      {"player:3:gold", 1u, std::vector<uint8_t>{3}},
      {"player:4:gold", 2u, std::vector<uint8_t>{4}},
  };

  auto result = backend.SaveBatchAtomic(items);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(kv_ptr->save_batch_calls, 1u);
  EXPECT_EQ(kv_ptr->last_save_batch_count, 2u);
}

TEST(AccountStorageBackendTest, SaveBatchAtomicUsesKvAtomicWhenSupported) {
  config::DatabaseConfig db_config;
  auto kv_backend = std::make_unique<AtomicCountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  AccountStorageBackend backend(std::move(kv_backend), db_config, nullptr);

  mir2::storage_engine::IStorageBackend::BatchItems items = {
      {"player:5:gold", 5u, std::vector<uint8_t>{5}},
      {"player:6:gold", 6u, std::vector<uint8_t>{6}},
  };

  auto result = backend.SaveBatchAtomic(items);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(kv_ptr->save_batch_atomic_calls, 1u);
  EXPECT_EQ(kv_ptr->last_save_batch_atomic_count, 2u);
  EXPECT_EQ(kv_ptr->save_batch_calls, 0u);
}

TEST(AccountStorageBackendIntegrationTest, SaveAndLoadAccountAreConsistentInAccountsTable) {
  const auto db_config = BuildDbConfigFromEnv();
  auto pool = std::make_shared<PgConnectionPool>();
  if (!pool->Initialize(db_config)) {
    GTEST_SKIP() << "PostgreSQL unavailable for account storage backend integration test";
  }

  auto kv_backend = std::make_unique<CountingKvBackend>();
  auto* kv_ptr = kv_backend.get();
  AccountStorageBackend backend(std::move(kv_backend), db_config, pool);
  ASSERT_TRUE(backend.Initialize());

  const std::string username =
      "account_backend_it_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string key = BuildAccountStorageKey(username);
  DeleteAccountByUsername(pool, username);

  AccountData account = MakeAccount(username);
  account.password_hash = "$2b$12$storage.backend.integration";
  const auto payload = EncodeAccountData(account);

  auto save_result = backend.Save(key, 123u, payload);
  ASSERT_TRUE(save_result.success) << save_result.error_message;
  EXPECT_EQ(kv_ptr->save_calls, 0u);

  auto loaded = backend.Load(key);
  ASSERT_TRUE(loaded.has_value());
  auto decoded = DecodeAccountData(loaded->second);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->username, username);
  EXPECT_EQ(decoded->password_hash, account.password_hash);
  EXPECT_EQ(decoded->email, account.email);

  DeleteAccountByUsername(pool, username);
}

}  // namespace
}  // namespace mir2::db
