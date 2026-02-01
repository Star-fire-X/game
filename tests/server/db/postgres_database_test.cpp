#include <gtest/gtest.h>

#include "db/postgres_database.h"

using namespace mir2::db;
using namespace mir2::common;
using mir2::common::CharacterData;
using mir2::common::ErrorCode;

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

    auto load_account_result = db.load_characters_by_account("account");
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

TEST(PostgresDatabaseTest, GetNextCharacterIdUsesSnowflake) {
    auto pool = std::make_shared<PgConnectionPool>();
    PostgresDatabase db(pool, 3);

    auto result = db.get_next_character_id();
    ASSERT_TRUE(result);
    EXPECT_GT(result.value, 0u);
}
