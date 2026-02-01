#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "game/map/scene_manager.h"
#include "handlers/npc/npc_command_handler.h"

namespace {

using mir2::ecs::CharacterStateComponent;
using mir2::game::map::SceneManager;
using mir2::handlers::NpcCommandHandler;

SceneManager::MapConfig BuildMapConfig(int32_t map_id, int32_t width, int32_t height) {
  SceneManager::MapConfig config{};
  config.map_id = map_id;
  config.width = width;
  config.height = height;
  config.grid_size = 10;
  config.load_walkability = false;
  return config;
}

TEST(NpcTeleportTest, QaMapMoveParsesTeleportCommand) {
  entt::registry registry;
  SceneManager scene_manager;
  NpcCommandHandler handler(registry, scene_manager);

  const auto player = registry.create();
  const std::vector<std::string> args = {"2", "120", "88"};
  auto cmd = handler.HandleCommand(player, "QA_MAPMOVE", args);

  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->target_map_id, 2);
  EXPECT_EQ(cmd->target_x, 120);
  EXPECT_EQ(cmd->target_y, 88);
}

TEST(NpcTeleportTest, QaMapRandomSelectsWalkablePosition) {
  entt::registry registry;
  SceneManager scene_manager;
  scene_manager.GetOrCreateMap(BuildMapConfig(1, 20, 20));

  const auto player = registry.create();
  auto& state = registry.emplace<CharacterStateComponent>(player);
  state.map_id = 1;
  state.position = {5, 6};
  ASSERT_TRUE(scene_manager.AddEntityToMap(1, player, 5, 6));

  NpcCommandHandler handler(registry, scene_manager);
  auto cmd = handler.HandleCommand(player, "QA_MAPRANDOM", {});

  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->target_map_id, 1);
  EXPECT_GE(cmd->target_x, 0);
  EXPECT_GE(cmd->target_y, 0);

  auto* map = scene_manager.GetMap(1);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(map->IsWalkable(cmd->target_x, cmd->target_y));
}

}  // namespace
