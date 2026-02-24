#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <pqxx/pqxx>

#include "config/config_manager.h"
#include "storage_engine/backends/account_storage_backend.h"
#include "storage_engine/backends/common/account_storage_codec.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"
#include "storage_engine/test_backend_mocks.h"

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

class AccountStorageBackendCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_config_ = BuildDbConfigFromEnv();
    pool_ = std::make_shared<PgConnectionPool>();
    if (!pool_->Initialize(db_config_)) {
      GTEST_SKIP() << "PostgreSQL unavailable for account cache tests";
    }

    AccountStorageBackend::AccountCacheOptions cache_options;
    cache_options.ttl_seconds = 1;
    cache_options.max_entries = 128;

    auto kv_backend = std::make_unique<mir2::storage_engine::test::NoopStorageBackend>();
    backend_ = std::make_unique<AccountStorageBackend>(
        std::move(kv_backend), db_config_, pool_, cache_options);
    ASSERT_TRUE(backend_->Initialize());

    username_ =
        "account_cache_test_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    account_key_ = BuildAccountStorageKey(username_);
    DeleteAccount();
  }

  void TearDown() override {
    DeleteAccount();
  }

  void DeleteAccount() {
    if (!pool_ || !pool_->IsReady()) {
      return;
    }
    auto conn = pool_->Acquire();
    if (!conn) {
      return;
    }
    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);
    txn.exec("DELETE FROM accounts WHERE username = $1", pqxx::params{username_});
    txn.commit();
  }

  void UpsertRawAccount(const std::string& password_hash) {
    auto conn = pool_->Acquire();
    ASSERT_NE(conn, nullptr);
    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);
    txn.exec(
        "INSERT INTO accounts (username, password_hash, email, created_at, last_login, banned) "
        "VALUES ($1, $2, $3, NOW(), NOW(), FALSE) "
        "ON CONFLICT (username) DO UPDATE SET password_hash = EXCLUDED.password_hash, "
        "email = EXCLUDED.email, last_login = NOW(), banned = FALSE",
        pqxx::params{username_, password_hash, username_ + "@example.com"});
    txn.commit();
  }

  std::optional<AccountData> LoadDecodedAccount() {
    auto loaded = backend_->Load(account_key_);
    if (!loaded) {
      return std::nullopt;
    }
    return DecodeAccountData(loaded->second);
  }

  config::DatabaseConfig db_config_;
  std::shared_ptr<PgConnectionPool> pool_;
  std::unique_ptr<AccountStorageBackend> backend_;
  std::string username_;
  std::string account_key_;
};

TEST_F(AccountStorageBackendCacheTest, CacheHitThenMissAfterTtlExpiry) {
  UpsertRawAccount("$2b$12$cache.hash.v1");

  auto first = LoadDecodedAccount();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->password_hash, "$2b$12$cache.hash.v1");

  // Update DB directly; immediate read should still hit cache.
  UpsertRawAccount("$2b$12$cache.hash.v2");
  auto second = LoadDecodedAccount();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->password_hash, "$2b$12$cache.hash.v1");

  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  auto third = LoadDecodedAccount();
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->password_hash, "$2b$12$cache.hash.v2");
}

TEST_F(AccountStorageBackendCacheTest, SaveRefreshesCachedAccountValue) {
  UpsertRawAccount("$2b$12$cache.write.before");

  auto first = LoadDecodedAccount();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->password_hash, "$2b$12$cache.write.before");

  AccountData updated = *first;
  updated.password_hash = "$2b$12$cache.write.saved";
  const auto payload = EncodeAccountData(updated);
  auto save_result = backend_->Save(account_key_, 777u, payload);
  ASSERT_TRUE(save_result.success) << save_result.error_message;

  // Mutate DB again after save; immediate read should still return cached save value.
  UpsertRawAccount("$2b$12$cache.write.db_mutated");
  auto second = LoadDecodedAccount();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->password_hash, "$2b$12$cache.write.saved");

  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  auto third = LoadDecodedAccount();
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->password_hash, "$2b$12$cache.write.db_mutated");
}

TEST_F(AccountStorageBackendCacheTest, LoadAccountVersionIsNotDefaultOne) {
  UpsertRawAccount("$2b$12$cache.version.raw");

  auto loaded = backend_->Load(account_key_);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_GT(loaded->first, 1u);
}

}  // namespace
}  // namespace mir2::db
