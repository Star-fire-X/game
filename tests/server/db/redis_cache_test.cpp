#include <gtest/gtest.h>

#include <unordered_set>

#include "db/redis_cache.h"
#include "db/redis_manager.h"
#include "server/config/config_manager.h"
#include "server/db/redis_test_utils.h"

using namespace mir2::db;
using namespace mir2::db::test_utils;
using namespace mir2::common;

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
    data.stats.gold = 999;
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

class RedisCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<FakeRedisServer>();
        ASSERT_TRUE(server_->is_ready());
        manager_ = make_manager(server_->port());
        ASSERT_TRUE(manager_);
        cache_ = std::make_unique<RedisCache>(manager_);
    }

    void TearDown() override {
        if (manager_) {
            manager_->Shutdown();
        }
        cache_.reset();
        manager_.reset();
        server_.reset();
    }

    std::unique_ptr<FakeRedisServer> server_;
    std::shared_ptr<RedisManager> manager_;
    std::unique_ptr<RedisCache> cache_;
};

TEST_F(RedisCacheTest, SessionSetGetDelete) {
    EXPECT_TRUE(cache_->set_session("user123", "token456", 10));

    std::string token;
    EXPECT_TRUE(cache_->get_session("user123", token));
    EXPECT_EQ(token, "token456");

    EXPECT_TRUE(cache_->delete_session("user123"));
    EXPECT_FALSE(cache_->get_session("user123", token));
}

TEST_F(RedisCacheTest, CacheCharacterRoundTrip) {
    auto data = make_character(1, "TestChar");
    EXPECT_TRUE(cache_->cache_character(data));

    CharacterData loaded;
    EXPECT_TRUE(cache_->get_character(1, loaded));
    EXPECT_EQ(loaded, data);
}

TEST_F(RedisCacheTest, CacheEquipmentRoundTrip) {
    std::vector<EquipmentSlotData> equip;
    EquipmentSlotData item;
    item.slot = 0;
    item.item_template_id = 1001;
    item.instance_id = 123456;
    item.durability = 100;
    item.enhancement_level = 5;
    equip.push_back(item);

    EXPECT_TRUE(cache_->cache_equipment(1, equip));

    std::vector<EquipmentSlotData> loaded;
    EXPECT_TRUE(cache_->get_equipment(1, loaded));
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].item_template_id, 1001u);
    EXPECT_EQ(loaded[0].instance_id, 123456u);
}

TEST_F(RedisCacheTest, CacheInventoryRoundTrip) {
    std::vector<InventorySlotData> inv;
    InventorySlotData item;
    item.slot = 0;
    item.item_template_id = 2001;
    item.instance_id = 98765;
    item.quantity = 10;
    item.durability = 80;
    item.enhancement_level = 2;
    inv.push_back(item);

    EXPECT_TRUE(cache_->cache_inventory(1, inv));

    std::vector<InventorySlotData> loaded;
    EXPECT_TRUE(cache_->get_inventory(1, loaded));
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].item_template_id, 2001u);
    EXPECT_EQ(loaded[0].quantity, 10);
}

TEST_F(RedisCacheTest, MapPlayersAddRemove) {
    EXPECT_TRUE(cache_->add_player_to_map(1, 100));
    EXPECT_TRUE(cache_->add_player_to_map(1, 200));

    auto players = cache_->get_map_players(1);
    std::unordered_set<uint32_t> player_set(players.begin(), players.end());
    EXPECT_TRUE(player_set.count(100));
    EXPECT_TRUE(player_set.count(200));

    EXPECT_TRUE(cache_->remove_player_from_map(1, 100));
    players = cache_->get_map_players(1);
    player_set = {players.begin(), players.end()};
    EXPECT_FALSE(player_set.count(100));
    EXPECT_TRUE(player_set.count(200));
}

TEST(RedisCacheStandaloneTest, ReturnsFalseWhenNotReady) {
    RedisCache cache(nullptr);
    CharacterData data;
    EXPECT_FALSE(cache.cache_character(data));
    EXPECT_FALSE(cache.set_session("user", "token", 1));
}

TEST_F(RedisCacheTest, CacheHitRateSample) {
    for (uint32_t i = 0; i < 50; ++i) {
        EXPECT_TRUE(cache_->cache_character(make_character(i, "Char" + std::to_string(i))));
    }

    int hits = 0;
    int misses = 0;
    for (uint32_t i = 0; i < 60; ++i) {
        CharacterData data;
        if (cache_->get_character(i, data)) {
            ++hits;
        } else {
            ++misses;
        }
    }

    EXPECT_EQ(hits, 50);
    EXPECT_EQ(misses, 10);
}
