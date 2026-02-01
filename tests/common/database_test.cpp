#include <gtest/gtest.h>

#include "common/character_data.h"
#include "common/database.h"

using namespace mir2::common;

namespace {

CharacterData make_character(uint32_t id, const std::string& name) {
    CharacterData data;
    data.id = id;
    data.account_id = "test_account";
    data.name = name;
    data.char_class = CharacterClass::WARRIOR;
    data.gender = Gender::MALE;
    data.stats.level = 10;
    data.stats.hp = 120;
    data.stats.max_hp = 120;
    data.stats.mp = 60;
    data.stats.max_mp = 60;
    data.stats.attack = 20;
    data.stats.defense = 15;
    data.stats.magic_attack = 8;
    data.stats.magic_defense = 6;
    data.stats.speed = 12;
    data.stats.gold = 1000;
    data.map_id = 1;
    data.position = {100, 100};
    data.created_at = 1704067200000;
    data.last_login = 1704067200000;
    return data;
}

}  // namespace

class SQLiteDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = std::make_unique<SQLiteDatabase>(":memory:");
        ASSERT_TRUE(db_->initialize());
    }

    void TearDown() override {
        db_->close();
    }

    std::unique_ptr<SQLiteDatabase> db_;
};

TEST_F(SQLiteDatabaseTest, InitializeAndClose) {
    EXPECT_TRUE(db_->is_open());
    db_->close();
    EXPECT_FALSE(db_->is_open());
}

TEST_F(SQLiteDatabaseTest, SaveAndLoadCharacter) {
    auto char_data = make_character(1, "TestWarrior");
    ASSERT_TRUE(db_->save_character(char_data));

    auto load_result = db_->load_character(1);
    ASSERT_TRUE(load_result);
    EXPECT_EQ(load_result.value.name, "TestWarrior");
    EXPECT_EQ(load_result.value.stats.level, 10);
    EXPECT_EQ(load_result.value.stats.gold, 1000);
}

TEST_F(SQLiteDatabaseTest, LoadCharacterByName) {
    auto char_data = make_character(1, "TestWarrior");
    ASSERT_TRUE(db_->save_character(char_data));

    auto load_result = db_->load_character_by_name("TestWarrior");
    ASSERT_TRUE(load_result);
    EXPECT_EQ(load_result.value.id, 1u);
}

TEST_F(SQLiteDatabaseTest, LoadNonExistentCharacter) {
    auto result = db_->load_character(999);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error_code, ErrorCode::CHARACTER_NOT_FOUND);
}

TEST_F(SQLiteDatabaseTest, UpdateCharacter) {
    auto char_data = make_character(1, "TestWarrior");
    ASSERT_TRUE(db_->save_character(char_data));

    char_data.stats.level = 20;
    char_data.stats.gold = 5000;
    ASSERT_TRUE(db_->save_character(char_data));

    auto load_result = db_->load_character(1);
    ASSERT_TRUE(load_result);
    EXPECT_EQ(load_result.value.stats.level, 20);
    EXPECT_EQ(load_result.value.stats.gold, 5000);
}

TEST_F(SQLiteDatabaseTest, DeleteCharacter) {
    auto char_data = make_character(1, "TestWarrior");
    ASSERT_TRUE(db_->save_character(char_data));

    ASSERT_TRUE(db_->delete_character(1));

    auto load_result = db_->load_character(1);
    ASSERT_FALSE(load_result);
    EXPECT_EQ(load_result.error_code, ErrorCode::CHARACTER_NOT_FOUND);
}

TEST_F(SQLiteDatabaseTest, CharacterNameExists) {
    auto char_data = make_character(1, "TestWarrior");
    ASSERT_TRUE(db_->save_character(char_data));

    auto exists_result = db_->character_name_exists("TestWarrior");
    ASSERT_TRUE(exists_result);
    EXPECT_TRUE(exists_result.value);

    auto not_exists = db_->character_name_exists("NonExistent");
    ASSERT_TRUE(not_exists);
    EXPECT_FALSE(not_exists.value);
}

TEST_F(SQLiteDatabaseTest, LoadCharactersByAccount) {
    for (int i = 1; i <= 3; ++i) {
        auto char_data = make_character(static_cast<uint32_t>(i), "Char" + std::to_string(i));
        ASSERT_TRUE(db_->save_character(char_data));
    }

    auto result = db_->load_characters_by_account("test_account");
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value.size(), 3u);
}

TEST_F(SQLiteDatabaseTest, GetNextCharacterId) {
    auto id1 = db_->get_next_character_id();
    ASSERT_TRUE(id1);
    EXPECT_EQ(id1.value, 1u);

    auto char_data = make_character(id1.value, "TestWarrior");
    ASSERT_TRUE(db_->save_character(char_data));

    auto id2 = db_->get_next_character_id();
    ASSERT_TRUE(id2);
    EXPECT_EQ(id2.value, 2u);
}

TEST_F(SQLiteDatabaseTest, TransactionCommit) {
    ASSERT_TRUE(db_->begin_transaction());
    ASSERT_TRUE(db_->save_character(make_character(1, "TestWarrior")));
    ASSERT_TRUE(db_->commit());

    auto load_result = db_->load_character(1);
    ASSERT_TRUE(load_result);
}

TEST_F(SQLiteDatabaseTest, TransactionRollback) {
    ASSERT_TRUE(db_->begin_transaction());
    ASSERT_TRUE(db_->save_character(make_character(1, "TestWarrior")));
    ASSERT_TRUE(db_->rollback());

    auto load_result = db_->load_character(1);
    EXPECT_FALSE(load_result);
}

TEST_F(SQLiteDatabaseTest, SaveAndLoadWithJsonData) {
    auto char_data = make_character(1, "TestWarrior");
    char_data.equipment_json = R"({"weapon":{"id":1001,"durability":100}})";
    char_data.inventory_json = R"([{"slot":0,"id":2001,"quantity":10}])";
    char_data.skills_json = R"([{"id":3001,"level":5}])";

    ASSERT_TRUE(db_->save_character(char_data));

    auto load_result = db_->load_character(1);
    ASSERT_TRUE(load_result);
    EXPECT_EQ(load_result.value.equipment_json, char_data.equipment_json);
    EXPECT_EQ(load_result.value.inventory_json, char_data.inventory_json);
    EXPECT_EQ(load_result.value.skills_json, char_data.skills_json);
}

TEST(SQLiteDatabaseStandaloneTest, ErrorsWhenDatabaseNotOpen) {
    SQLiteDatabase db(":memory:");
    auto load_result = db.load_character(1);
    EXPECT_FALSE(load_result);
    EXPECT_EQ(load_result.error_code, ErrorCode::DATABASE_ERROR);
}

TEST_F(SQLiteDatabaseTest, NotImplementedOperations) {
    auto equip_result = db_->save_equipment(1, {});
    EXPECT_FALSE(equip_result);
    EXPECT_EQ(equip_result.error_code, ErrorCode::NOT_IMPLEMENTED);

    auto inv_result = db_->save_inventory(1, {});
    EXPECT_FALSE(inv_result);
    EXPECT_EQ(inv_result.error_code, ErrorCode::NOT_IMPLEMENTED);

    auto skills_result = db_->save_skills(1, {});
    EXPECT_FALSE(skills_result);
    EXPECT_EQ(skills_result.error_code, ErrorCode::NOT_IMPLEMENTED);

    auto account_result = db_->load_account("user");
    EXPECT_FALSE(account_result);
    EXPECT_EQ(account_result.error_code, ErrorCode::NOT_IMPLEMENTED);

    auto create_result = db_->create_account(mir2::db::AccountData{});
    EXPECT_FALSE(create_result);
    EXPECT_EQ(create_result.error_code, ErrorCode::NOT_IMPLEMENTED);

    auto id_result = db_->generate_id("character");
    EXPECT_FALSE(id_result);
    EXPECT_EQ(id_result.error_code, ErrorCode::NOT_IMPLEMENTED);
}
