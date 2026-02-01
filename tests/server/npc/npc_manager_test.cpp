#include <gtest/gtest.h>

#include <string>

#include "game/npc/npc_manager.h"

namespace {

using mir2::game::npc::NpcConfig;
using mir2::game::npc::NpcManager;
using mir2::game::npc::NpcType;

NpcConfig MakeConfig(uint64_t id, uint32_t map_id, NpcType type, const std::string& name) {
    NpcConfig config;
    config.id = id;
    config.map_id = map_id;
    config.type = type;
    config.name = name;
    config.x = 10;
    config.y = 20;
    return config;
}

class NpcManagerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        NpcManager::Instance().Clear();
    }

    void TearDown() override {
        NpcManager::Instance().Clear();
    }
};

TEST_F(NpcManagerFixture, CreateNpcReturnsValidPointer) {
    auto& manager = NpcManager::Instance();
    auto npc = manager.CreateNpc(MakeConfig(0, 1, NpcType::kQuest, "QuestGiver"));

    ASSERT_NE(npc, nullptr);
    EXPECT_EQ(manager.TotalCount(), 1u);
    EXPECT_EQ(npc->GetMapId(), 1u);
    EXPECT_EQ(npc->GetName(), "QuestGiver");

    auto fetched = manager.GetNpc(npc->GetId());
    EXPECT_EQ(fetched, npc);

    auto npcs_on_map = manager.GetNpcsOnMap(1);
    ASSERT_EQ(npcs_on_map.size(), 1u);
    EXPECT_EQ(npcs_on_map.front().id, npc->GetId());
}

TEST_F(NpcManagerFixture, CreateNpcRejectsDuplicateId) {
    auto& manager = NpcManager::Instance();
    auto first = manager.CreateNpc(MakeConfig(99, 2, NpcType::kGuard, "Guard"));
    auto second = manager.CreateNpc(MakeConfig(99, 2, NpcType::kGuard, "Guard"));

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(second, nullptr);
    EXPECT_EQ(manager.TotalCount(), 1u);
}

TEST_F(NpcManagerFixture, RemoveNpcClearsFromMapLookup) {
    auto& manager = NpcManager::Instance();

    auto npc1 = manager.CreateNpc(MakeConfig(0, 3, NpcType::kMerchant, "Trader"));
    auto npc2 = manager.CreateNpc(MakeConfig(0, 3, NpcType::kMerchant, "Trader2"));
    auto npc3 = manager.CreateNpc(MakeConfig(0, 4, NpcType::kQuest, "Quest"));

    ASSERT_NE(npc1, nullptr);
    ASSERT_NE(npc2, nullptr);
    ASSERT_NE(npc3, nullptr);

    auto map3_before = manager.GetNpcsOnMap(3);
    EXPECT_EQ(map3_before.size(), 2u);

    manager.RemoveNpc(npc1->GetId());

    EXPECT_EQ(manager.GetNpc(npc1->GetId()), nullptr);
    auto map3_after = manager.GetNpcsOnMap(3);
    EXPECT_EQ(map3_after.size(), 1u);
    EXPECT_EQ(map3_after.front().id, npc2->GetId());

    auto map4 = manager.GetNpcsOnMap(4);
    ASSERT_EQ(map4.size(), 1u);
    EXPECT_EQ(map4.front().id, npc3->GetId());
}

TEST_F(NpcManagerFixture, GetNpcReturnsNullForMissingId) {
    auto& manager = NpcManager::Instance();
    EXPECT_EQ(manager.GetNpc(404), nullptr);
}

}  // namespace
