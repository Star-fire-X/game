#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "db/character_repository.h"
#include "db/redis_manager.h"
#include "server/config/config_manager.h"
#include "server/db/redis_test_utils.h"

using namespace mir2::db;
using namespace mir2::db::test_utils;
using namespace mir2::common;
using ::testing::InSequence;
using ::testing::Return;

namespace {

CharacterData make_character(uint32_t id, const std::string& name) {
    CharacterData data;
    data.id = id;
    data.account_id = "test_account";
    data.name = name;
    data.char_class = CharacterClass::WARRIOR;
    data.stats.level = 10;
    data.stats.hp = 100;
    data.stats.max_hp = 100;
    data.stats.mp = 50;
    data.stats.max_mp = 50;
    data.stats.attack = 20;
    data.stats.defense = 10;
    data.stats.magic_attack = 5;
    data.stats.magic_defense = 5;
    data.stats.speed = 10;
    data.stats.gold = 100;
    return data;
}

std::shared_ptr<RedisManager> make_manager(uint16_t port) {
    auto manager = std::make_shared<RedisManager>();
    mir2::config::RedisConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.password = "";
    config.db = 0;
    if (!manager->Initialize(config)) {
        return nullptr;
    }
    return manager;
}

}  // namespace

class MockDatabase : public IDatabase {
public:
    MOCK_METHOD(DbResult<void>, initialize, (), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, is_open, (), (const, override));
    MOCK_METHOD(DbResult<void>, save_character, (const CharacterData&), (override));
    MOCK_METHOD(DbResult<CharacterData>, load_character, (uint32_t), (override));
    MOCK_METHOD(DbResult<CharacterData>, load_character_by_name, (const std::string&), (override));
    MOCK_METHOD(DbResult<std::vector<CharacterData>>, load_characters_by_account,
                (const std::string&), (override));
    MOCK_METHOD(DbResult<void>, delete_character, (uint32_t), (override));
    MOCK_METHOD(DbResult<bool>, character_name_exists, (const std::string&), (override));
    MOCK_METHOD(DbResult<uint32_t>, get_next_character_id, (), (override));
    MOCK_METHOD(DbResult<void>, begin_transaction, (), (override));
    MOCK_METHOD(DbResult<void>, commit, (), (override));
    MOCK_METHOD(DbResult<void>, rollback, (), (override));
    MOCK_METHOD(DbResult<void>, save_equipment,
                (uint32_t, const std::vector<EquipmentSlotData>&), (override));
    MOCK_METHOD(DbResult<void>, save_inventory,
                (uint32_t, const std::vector<InventorySlotData>&), (override));
    MOCK_METHOD(DbResult<void>, save_skills,
                (uint32_t, const std::vector<CharacterSkillData>&), (override));
    MOCK_METHOD(DbResult<AccountData>, load_account, (const std::string&), (override));
    MOCK_METHOD(DbResult<void>, create_account, (const AccountData&), (override));
    MOCK_METHOD(DbResult<uint64_t>, generate_id, (const std::string&), (override));
};

class CharacterRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<FakeRedisServer>();
        ASSERT_TRUE(server_->is_ready());
        manager_ = make_manager(server_->port());
        ASSERT_TRUE(manager_);
        cache_ = std::make_shared<RedisCache>(manager_);

        mock_db_ = std::make_shared<MockDatabase>();
        repo_ = std::make_unique<CharacterRepository>(mock_db_, cache_);
    }

    void TearDown() override {
        if (manager_) {
            manager_->Shutdown();
        }
        repo_.reset();
        cache_.reset();
        manager_.reset();
        server_.reset();
    }

    std::unique_ptr<FakeRedisServer> server_;
    std::shared_ptr<RedisManager> manager_;
    std::shared_ptr<RedisCache> cache_;
    std::shared_ptr<MockDatabase> mock_db_;
    std::unique_ptr<CharacterRepository> repo_;
};

TEST_F(CharacterRepositoryTest, LoadCharacterFromCache) {
    auto data = make_character(1, "CachedChar");
    ASSERT_TRUE(cache_->cache_character(data));

    EXPECT_CALL(*mock_db_, load_character(::testing::_)).Times(0);

    auto result = repo_->load_character(1);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value.name, "CachedChar");
}

TEST_F(CharacterRepositoryTest, LoadCharacterFromDatabaseWhenCacheMiss) {
    auto data = make_character(1, "DbChar");

    EXPECT_CALL(*mock_db_, load_character(1))
        .WillOnce(Return(DbResult<CharacterData>::ok(data)));

    auto result = repo_->load_character(1);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value.name, "DbChar");

    CharacterData cached;
    EXPECT_TRUE(cache_->get_character(1, cached));
    EXPECT_EQ(cached.name, "DbChar");
}

TEST_F(CharacterRepositoryTest, SaveCharacterUsesCacheWriteBehind) {
    auto data = make_character(2, "WriteBehind");

    EXPECT_CALL(*mock_db_, save_character(::testing::_)).Times(0);

    auto result = repo_->save_character(data);
    ASSERT_TRUE(result);

    CharacterData cached;
    EXPECT_TRUE(cache_->get_character(2, cached));
    EXPECT_EQ(cached.name, "WriteBehind");
}

TEST(CharacterRepositoryStandaloneTest, SaveCharacterFallsBackToDatabaseWhenCacheNotReady) {
    auto db = std::make_shared<MockDatabase>();
    auto cache = std::make_shared<RedisCache>(std::make_shared<RedisManager>());
    CharacterRepository repo(db, cache);

    CharacterData data = make_character(3, "Fallback");
    EXPECT_CALL(*db, save_character(::testing::_))
        .WillOnce(Return(DbResult<void>::ok()));

    auto result = repo.save_character(data);
    EXPECT_TRUE(result);
}

TEST_F(CharacterRepositoryTest, SaveCharacterFullWithTransaction) {
    auto data = make_character(4, "FullSave");
    std::vector<EquipmentSlotData> equip;
    std::vector<InventorySlotData> inv;

    {
        InSequence seq;
        EXPECT_CALL(*mock_db_, begin_transaction())
            .WillOnce(Return(DbResult<void>::ok()));
        EXPECT_CALL(*mock_db_, save_character(::testing::_))
            .WillOnce(Return(DbResult<void>::ok()));
        EXPECT_CALL(*mock_db_, save_equipment(data.id, ::testing::_))
            .WillOnce(Return(DbResult<void>::ok()));
        EXPECT_CALL(*mock_db_, save_inventory(data.id, ::testing::_))
            .WillOnce(Return(DbResult<void>::ok()));
        EXPECT_CALL(*mock_db_, commit())
            .WillOnce(Return(DbResult<void>::ok()));
    }

    auto result = repo_->save_character_full(data, equip, inv);
    ASSERT_TRUE(result);

    CharacterData cached;
    EXPECT_TRUE(cache_->get_character(data.id, cached));
}

TEST_F(CharacterRepositoryTest, SaveCharacterFullRollbackOnFailure) {
    auto data = make_character(5, "FullSaveFail");
    std::vector<EquipmentSlotData> equip;
    std::vector<InventorySlotData> inv;

    EXPECT_CALL(*mock_db_, begin_transaction())
        .WillOnce(Return(DbResult<void>::ok()));
    EXPECT_CALL(*mock_db_, save_character(::testing::_))
        .WillOnce(Return(DbResult<void>::ok()));
    EXPECT_CALL(*mock_db_, save_equipment(data.id, ::testing::_))
        .WillOnce(Return(DbResult<void>::error(ErrorCode::DATABASE_ERROR, "DB error")));
    EXPECT_CALL(*mock_db_, rollback())
        .WillOnce(Return(DbResult<void>::ok()));

    auto result = repo_->save_character_full(data, equip, inv);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error_code, ErrorCode::DATABASE_ERROR);
}

TEST_F(CharacterRepositoryTest, LoadEquipmentReturnsNotImplementedOnCacheMiss) {
    auto result = repo_->load_equipment(999);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error_code, ErrorCode::NOT_IMPLEMENTED);
}

TEST_F(CharacterRepositoryTest, FlushDirtyCharactersPersistsCache) {
    auto data = make_character(6, "Dirty");
    std::vector<EquipmentSlotData> equip;
    std::vector<InventorySlotData> inv;

    ASSERT_TRUE(cache_->cache_character(data));
    ASSERT_TRUE(cache_->cache_equipment(data.id, equip));
    ASSERT_TRUE(cache_->cache_inventory(data.id, inv));

    EXPECT_CALL(*mock_db_, begin_transaction())
        .WillOnce(Return(DbResult<void>::ok()));
    EXPECT_CALL(*mock_db_, save_character(::testing::_))
        .WillOnce(Return(DbResult<void>::ok()));
    EXPECT_CALL(*mock_db_, save_equipment(data.id, ::testing::_))
        .WillOnce(Return(DbResult<void>::ok()));
    EXPECT_CALL(*mock_db_, save_inventory(data.id, ::testing::_))
        .WillOnce(Return(DbResult<void>::ok()));
    EXPECT_CALL(*mock_db_, commit())
        .WillOnce(Return(DbResult<void>::ok()));

    repo_->mark_dirty(data.id);
    repo_->set_flush_interval(std::chrono::seconds(0));
    repo_->flush_dirty_characters();
}
