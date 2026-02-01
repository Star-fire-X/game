/**
 * @file monster_drop_system_test.cc
 * @brief 怪物掉落系统单元测试
 */

#include <gtest/gtest.h>
#include <entt/entt.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "game/entity/monster_drop_config.h"

#define private public
#include "ecs/systems/monster_drop_system.h"
#undef private

namespace mir2::ecs {
namespace {

class MonsterDropSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_.clear();
        auto base = std::filesystem::temp_directory_path();
        auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir_ = base / ("mir2_drop_test_" + unique);
        std::filesystem::create_directories(temp_dir_);
        file_counter_ = 0;
    }

    void TearDown() override {
        registry_.clear();
        std::error_code ec;
        if (!temp_dir_.empty()) {
            std::filesystem::remove_all(temp_dir_, ec);
        }
    }

    std::filesystem::path WriteConfig(const std::string& contents) {
        auto path = temp_dir_ / ("drops_" + std::to_string(file_counter_++) + ".yaml");
        std::ofstream out(path);
        out << contents;
        return path;
    }

    entt::registry registry_;
    std::filesystem::path temp_dir_;
    int file_counter_ = 0;
};

bool HasDropItem(const std::vector<game::entity::DropItem>& items, uint32_t item_id) {
    return std::any_of(items.begin(), items.end(),
                       [item_id](const auto& item) { return item.item_id == item_id; });
}

TEST_F(MonsterDropSystemTest, DropItemConfig) {
    game::entity::DropItem item;
    item.item_id = 1001;
    item.drop_rate = 0.5f;
    
    EXPECT_EQ(item.item_id, 1001);
    EXPECT_FLOAT_EQ(item.drop_rate, 0.5f);
}

TEST_F(MonsterDropSystemTest, DropSystem_LoadTables) {
    MonsterDropSystem system;
    const auto path = WriteConfig(R"(drop_tables:
  - monster_template_id: 100
    items:
      - item_id: 2001
        drop_rate: 1.2
        min_count: 1
        max_count: 2
        rarity: 3
        boss_bonus: 0.5
      - item_id: 0
        drop_rate: 0.5
  - monster_template_id: 0
    items:
      - item_id: 3000
)");

    system.LoadDropTables(path.string());

    ASSERT_EQ(system.drop_tables_.size(), 1u);
    const auto& table = system.drop_tables_.at(100);
    ASSERT_EQ(table.items.size(), 1u);

    const auto& item = table.items.front();
    EXPECT_EQ(item.item_id, 2001u);
    EXPECT_FLOAT_EQ(item.drop_rate, 1.0f);
    EXPECT_EQ(item.min_count, 1);
    EXPECT_EQ(item.max_count, 2);
    EXPECT_EQ(item.rarity, 3);
    EXPECT_FLOAT_EQ(item.boss_bonus, 0.5f);
}

TEST_F(MonsterDropSystemTest, DropSystem_SelectItems) {
    MonsterDropSystem system;
    game::entity::MonsterDropTable table;
    table.monster_template_id = 1;

    game::entity::DropItem always;
    always.item_id = 10;
    always.drop_rate = 1.0f;

    game::entity::DropItem never;
    never.item_id = 20;
    never.drop_rate = 0.0f;

    game::entity::DropItem half;
    half.item_id = 30;
    half.drop_rate = 0.5f;

    table.items = {always, never, half};

    int half_hits = 0;
    constexpr int kIterations = 500;
    for (int i = 0; i < kIterations; ++i) {
        const auto drops = system.SelectDropItems(table);
        EXPECT_TRUE(HasDropItem(drops, always.item_id));
        EXPECT_FALSE(HasDropItem(drops, never.item_id));
        if (HasDropItem(drops, half.item_id)) {
            ++half_hits;
        }
    }

    EXPECT_GT(half_hits, kIterations / 4);
    EXPECT_LT(half_hits, kIterations * 3 / 4);
}

}  // namespace
}  // namespace mir2::ecs
