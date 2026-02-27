#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "config/config_manager.h"
#include "storage_engine/backends/postgres/postgres_database.h"

using namespace mir2::db;
using namespace mir2::common;
using mir2::common::CharacterData;
using mir2::common::ErrorCode;

namespace {

std::string EnvOrDefault(const char* key, const char* default_value) {
  const char* value = std::getenv(key);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return default_value;
}

mir2::config::DatabaseConfig BuildDbConfigFromEnv() {
  mir2::config::DatabaseConfig cfg;
  cfg.host = EnvOrDefault("MIR2_DB_HOST", "127.0.0.1");
  cfg.port = static_cast<uint16_t>(std::stoi(EnvOrDefault("MIR2_DB_PORT", "5432")));
  cfg.user = EnvOrDefault("MIR2_DB_USER", "mir2");
  cfg.password = EnvOrDefault("MIR2_DB_PASSWORD", "mir2_password");
  cfg.database = EnvOrDefault("MIR2_DB_NAME", "mir2_game");
  cfg.pool_size = 1;
  return cfg;
}

void EnsureCharactersTableExists(const std::shared_ptr<PgConnectionPool>& pool) {
  auto conn = pool->Acquire();
  ASSERT_NE(conn, nullptr);
  PgConnectionGuard guard(*pool, conn);
  pqxx::work txn(*conn);
  txn.exec("CREATE TABLE IF NOT EXISTS characters (id BIGSERIAL PRIMARY KEY)");
  txn.commit();
}

}  // namespace

static_assert(
    std::is_same_v<
        decltype(std::declval<PostgresDatabase>().load_characters_by_account(
            std::declval<uint64_t>())),
        DbResult<std::vector<CharacterData>>>,
    "load_characters_by_account signature must keep uint64_t account_id");

static_assert(
    std::is_same_v<
        decltype(std::declval<PostgresDatabase>().generate_id(
            std::declval<const std::string&>())),
        DbResult<uint64_t>>,
    "generate_id return type must keep uint64_t");

TEST(PostgresDatabaseTest, InitializeFailsWhenPoolNotReady) {
  auto pool = std::make_shared<PgConnectionPool>();
  PostgresDatabase db(pool, 1);

  auto result = db.initialize();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error_code, ErrorCode::DATABASE_ERROR);
}

TEST(PostgresDatabaseTest, IsOpenFalseWhenPoolNotReady) {
  auto pool = std::make_shared<PgConnectionPool>();
  PostgresDatabase db(pool, 1);

  EXPECT_FALSE(db.is_open());
}

TEST(PostgresDatabaseTest, GenerateIdWorksWithoutConnection) {
  auto pool = std::make_shared<PgConnectionPool>();
  PostgresDatabase db(pool, 2);

  auto result = db.generate_id("character");
  ASSERT_TRUE(result);
  EXPECT_GT(result.value, 0u);
}

TEST(PostgresDatabaseTest, CharacterOperationsReturnNotImplemented) {
  auto pool = std::make_shared<PgConnectionPool>();
  PostgresDatabase db(pool, 0);

  auto save_result = db.save_character(CharacterData{});
  EXPECT_FALSE(save_result);
  EXPECT_EQ(save_result.error_code, ErrorCode::NOT_IMPLEMENTED);

  auto load_result = db.load_character(1);
  EXPECT_FALSE(load_result);
  EXPECT_EQ(load_result.error_code, ErrorCode::NOT_IMPLEMENTED);

  auto load_name_result = db.load_character_by_name("name");
  EXPECT_FALSE(load_name_result);
  EXPECT_EQ(load_name_result.error_code, ErrorCode::NOT_IMPLEMENTED);

  auto load_account_result = db.load_characters_by_account(1001);
  EXPECT_FALSE(load_account_result);
  EXPECT_EQ(load_account_result.error_code, ErrorCode::NOT_IMPLEMENTED);

  auto delete_result = db.delete_character(1);
  EXPECT_FALSE(delete_result);
  EXPECT_EQ(delete_result.error_code, ErrorCode::NOT_IMPLEMENTED);

  auto exists_result = db.character_name_exists("name");
  EXPECT_FALSE(exists_result);
  EXPECT_EQ(exists_result.error_code, ErrorCode::NOT_IMPLEMENTED);
}

TEST(PostgresDatabaseTest, DatabaseOperationsReturnErrorWhenClosed) {
  auto pool = std::make_shared<PgConnectionPool>();
  PostgresDatabase db(pool, 0);

  auto begin_result = db.begin_transaction();
  EXPECT_FALSE(begin_result);
  EXPECT_EQ(begin_result.error_code, ErrorCode::DATABASE_ERROR);

  auto equip_result = db.save_equipment(1, {});
  EXPECT_FALSE(equip_result);
  EXPECT_EQ(equip_result.error_code, ErrorCode::DATABASE_ERROR);

  auto inv_result = db.save_inventory(1, {});
  EXPECT_FALSE(inv_result);
  EXPECT_EQ(inv_result.error_code, ErrorCode::DATABASE_ERROR);

  auto skills_result = db.save_skills(1, {});
  EXPECT_FALSE(skills_result);
  EXPECT_EQ(skills_result.error_code, ErrorCode::DATABASE_ERROR);

  auto account_result = db.load_account("user");
  EXPECT_FALSE(account_result);
  EXPECT_EQ(account_result.error_code, ErrorCode::DATABASE_ERROR);

  auto create_account_result = db.create_account(AccountData{});
  EXPECT_FALSE(create_account_result);
  EXPECT_EQ(create_account_result.error_code, ErrorCode::DATABASE_ERROR);
}

TEST(PostgresDatabaseTest, GetNextCharacterIdFallsBackWhenPoolNotReady) {
  auto pool = std::make_shared<PgConnectionPool>();
  PostgresDatabase db(pool, 3);

  auto result = db.get_next_character_id();
  ASSERT_TRUE(result);
  EXPECT_GT(result.value, 0u);
}

TEST(PostgresDatabaseIntegrationTest, LoadAccountMissingUserDoesNotLeakConnection) {
  const auto db_config = BuildDbConfigFromEnv();
  auto pool = std::make_shared<PgConnectionPool>();
  if (!pool->Initialize(db_config)) {
    GTEST_SKIP() << "PostgreSQL unavailable for postgres database integration test";
  }

  PostgresDatabase db(pool, 7);
  ASSERT_TRUE(db.initialize());

  const auto username =
      "postgres_db_missing_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

  EXPECT_EQ(pool->InUseCount(), 0u);
  auto result = db.load_account(username);
  EXPECT_FALSE(result);
  EXPECT_EQ(pool->InUseCount(), 0u);

  // Repeat to ensure connection can be reacquired and released consistently.
  auto second = db.load_account(username);
  EXPECT_FALSE(second);
  EXPECT_EQ(pool->InUseCount(), 0u);
}

TEST(PostgresDatabaseIntegrationTest, GetNextCharacterIdUsesDatabaseSequence) {
  const auto db_config = BuildDbConfigFromEnv();
  auto pool = std::make_shared<PgConnectionPool>();
  if (!pool->Initialize(db_config)) {
    GTEST_SKIP() << "PostgreSQL unavailable for postgres database integration test";
  }
  EnsureCharactersTableExists(pool);

  PostgresDatabase db(pool, 8);
  ASSERT_TRUE(db.initialize());

  EXPECT_EQ(pool->InUseCount(), 0u);
  auto first = db.get_next_character_id();
  ASSERT_TRUE(first);
  EXPECT_GT(first.value, 0u);
  EXPECT_EQ(pool->InUseCount(), 0u);

  auto second = db.get_next_character_id();
  ASSERT_TRUE(second);
  EXPECT_GT(second.value, first.value);
  EXPECT_EQ(pool->InUseCount(), 0u);
}
