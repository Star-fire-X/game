#include <gtest/gtest.h>

#include "game/npc/npc_entity.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::game::npc::NpcConfig;
using mir2::game::npc::NpcEntity;
using mir2::game::npc::NpcTeleportTarget;
using mir2::game::npc::NpcType;

TEST(NpcEntityTest, ApplyConfigSetsFields) {
    NpcEntity npc(1);

    NpcConfig config;
    config.id = 42;
    config.template_id = 7;
    config.name = "Gatekeeper";
    config.type = NpcType::kTeleport;
    config.map_id = 1001;
    config.x = 12;
    config.y = 34;
    config.direction = 3;
    config.enabled = false;
    config.script_id = "npc_gate_script";
    config.store_id = 11;
    config.guild_id = 22;
    config.teleport_target = NpcTeleportTarget{2002, 45, 67};

    npc.ApplyConfig(config);

    EXPECT_EQ(npc.GetId(), 42u);
    EXPECT_EQ(npc.GetName(), "Gatekeeper");
    EXPECT_EQ(npc.GetType(), NpcType::kTeleport);
    EXPECT_EQ(npc.GetMapId(), 1001u);
    EXPECT_EQ(npc.GetX(), 12);
    EXPECT_EQ(npc.GetY(), 34);
    EXPECT_EQ(npc.GetDirection(), 3u);
    EXPECT_FALSE(npc.IsEnabled());
    EXPECT_EQ(npc.GetScriptId(), "npc_gate_script");
    EXPECT_EQ(npc.GetStoreId(), 11u);
    EXPECT_EQ(npc.GetGuildId(), 22u);
    ASSERT_TRUE(npc.HasTeleportTarget());
    EXPECT_EQ(npc.GetTeleportTarget()->map_id, 2002);
    EXPECT_EQ(npc.GetTeleportTarget()->x, 45);
    EXPECT_EQ(npc.GetTeleportTarget()->y, 67);
}

TEST(NpcEntityTest, ApplyConfigKeepsIdWhenConfigIdZero) {
    NpcEntity npc(9);

    NpcConfig config;
    config.id = 0;
    config.name = "Healer";
    config.type = NpcType::kQuest;

    npc.ApplyConfig(config);

    EXPECT_EQ(npc.GetId(), 9u);
    EXPECT_EQ(npc.GetName(), "Healer");
    EXPECT_EQ(npc.GetType(), NpcType::kQuest);
}

TEST(NpcEntityTest, SetPositionAndEnabledUpdateState) {
    NpcEntity npc(3);

    npc.SetPosition(55, 66);
    npc.SetEnabled(false);

    EXPECT_EQ(npc.GetX(), 55);
    EXPECT_EQ(npc.GetY(), 66);
    EXPECT_FALSE(npc.IsEnabled());

    npc.SetEnabled(true);
    EXPECT_TRUE(npc.IsEnabled());
}

}  // namespace
