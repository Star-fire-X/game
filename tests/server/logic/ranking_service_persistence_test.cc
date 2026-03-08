#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include <entt/entt.hpp>
#include <pqxx/pqxx>

#include "config/config_manager.h"
#include "logic/services/ranking_service.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"

namespace mir2::logic::test {
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

class RankingServicePersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto db_config = BuildDbConfigFromEnv();
    db_pool_ = std::make_shared<db::PgConnectionPool>();
    if (!db_pool_->Initialize(db_config)) {
      GTEST_SKIP() << "PostgreSQL unavailable for ranking persistence tests";
    }
    CleanupSeedRows();
  }

  void TearDown() override {
    CleanupSeedRows();
  }

  void CleanupSeedRows() {
    if (!db_pool_ || !db_pool_->IsReady()) {
      return;
    }

    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        return;
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      txn.exec("DELETE FROM characters WHERE name LIKE 'rank_pg_%'");
      txn.exec("DELETE FROM accounts WHERE username LIKE 'rank_pg_%'");
      txn.commit();
    } catch (...) {
    }
  }

  uint64_t InsertCharacter(const std::string& key,
                           int level,
                           int64_t gold) {
    const auto conn = db_pool_->Acquire();
    EXPECT_NE(conn, nullptr);
    if (!conn) {
      return 0;
    }

    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    const std::string username = "rank_pg_acc_" + key;
    const std::string name = "rank_pg_char_" + key;
    const pqxx::result account_rows = txn.exec(
        "INSERT INTO accounts (username, password_hash) VALUES ($1, $2) RETURNING id",
        pqxx::params{username, "test_hash"});
    EXPECT_FALSE(account_rows.empty());
    if (account_rows.empty()) {
      txn.abort();
      return 0;
    }
    const uint64_t account_id = account_rows.front()["id"].as<uint64_t>(0);

    const pqxx::result character_rows = txn.exec(
        "INSERT INTO characters ("
        "account_id, name, char_class, gender, level, experience, hp, max_hp, mp, max_mp, "
        "attack, defense, magic_attack, magic_defense, speed, gold, map_id, pos_x, pos_y) "
        "VALUES ($1, $2, 0, 0, $3, 0, 100, 100, 50, 50, 10, 10, 10, 10, 5, $4, 1, 100, 100) "
        "RETURNING id",
        pqxx::params{account_id, name, level, gold});
    EXPECT_FALSE(character_rows.empty());
    if (character_rows.empty()) {
      txn.abort();
      return 0;
    }
    txn.commit();
    return character_rows.front()["id"].as<uint64_t>(0);
  }

  entt::registry registry_;
  std::shared_ptr<db::PgConnectionPool> db_pool_;
};

TEST_F(RankingServicePersistenceTest, LevelRankingUsesPersistedCharacters) {
  const uint64_t low_id = InsertCharacter("low", 800000, 1000);
  const uint64_t high_id = InsertCharacter("high", 900000, 9999999);
  ASSERT_NE(low_id, 0u);
  ASSERT_NE(high_id, 0u);

  RankingService ranking_service(registry_, db_pool_);
  const auto ranking = ranking_service.GetRanking(mir2::proto::RankingType::LEVEL, 1, 200);
  ASSERT_GE(ranking.entries.size(), 2u);

  const auto high_it = std::find_if(
      ranking.entries.begin(),
      ranking.entries.end(),
      [high_id](const RankingEntryView& entry) { return entry.entity_id == high_id; });
  const auto low_it = std::find_if(
      ranking.entries.begin(),
      ranking.entries.end(),
      [low_id](const RankingEntryView& entry) { return entry.entity_id == low_id; });
  ASSERT_NE(high_it, ranking.entries.end());
  ASSERT_NE(low_it, ranking.entries.end());
  EXPECT_LT(high_it->rank, low_it->rank);
  EXPECT_GT(high_it->value, low_it->value);

  const auto my_rank = ranking_service.GetMyRank(mir2::proto::RankingType::LEVEL, low_id);
  ASSERT_TRUE(my_rank.has_value());
  EXPECT_EQ(my_rank->entity_id, low_id);
  EXPECT_EQ(my_rank->rank, low_it->rank);
  EXPECT_EQ(my_rank->value, low_it->value);
}

TEST_F(RankingServicePersistenceTest, GoldRankingUsesPersistedCharacters) {
  const uint64_t low_id = InsertCharacter("gold_low", 10, 7000000);
  const uint64_t high_id = InsertCharacter("gold_high", 11, 9000000);
  ASSERT_NE(low_id, 0u);
  ASSERT_NE(high_id, 0u);

  RankingService ranking_service(registry_, db_pool_);
  const auto ranking = ranking_service.GetRanking(mir2::proto::RankingType::GOLD, 1, 200);
  ASSERT_GE(ranking.entries.size(), 2u);

  const auto high_it = std::find_if(
      ranking.entries.begin(),
      ranking.entries.end(),
      [high_id](const RankingEntryView& entry) { return entry.entity_id == high_id; });
  const auto low_it = std::find_if(
      ranking.entries.begin(),
      ranking.entries.end(),
      [low_id](const RankingEntryView& entry) { return entry.entity_id == low_id; });
  ASSERT_NE(high_it, ranking.entries.end());
  ASSERT_NE(low_it, ranking.entries.end());
  EXPECT_LT(high_it->rank, low_it->rank);
  EXPECT_GT(high_it->value, low_it->value);
}

}  // namespace
}  // namespace mir2::logic::test
