#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "core/utils.h"
#include "game/map/map_attributes.h"
#include "game/map/scroll_teleport.h"

namespace {

using mir2::game::map::MapAttributes;
using mir2::game::map::ScrollTeleport;

TEST(ScrollTeleportTest, TownScrollUsesHomeMapWhenNotPK) {
  entt::registry registry;
  const auto entity = registry.create();

  MapAttributes attrs;
  attrs.home_map = "1";
  attrs.home_x = 10;
  attrs.home_y = 20;
  attrs.pk_village_map = "2";
  attrs.pk_village_x = 30;
  attrs.pk_village_y = 40;

  auto cmd = ScrollTeleport::UseTownScroll(entity, "1", 0, attrs);
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->target_map_id, 1);
  EXPECT_EQ(cmd->target_x, 10);
  EXPECT_EQ(cmd->target_y, 20);
}

TEST(ScrollTeleportTest, TownScrollUsesPkVillageForHighPK) {
  entt::registry registry;
  const auto entity = registry.create();

  MapAttributes attrs;
  attrs.home_map = "1";
  attrs.home_x = 10;
  attrs.home_y = 20;
  attrs.pk_village_map = "5";
  attrs.pk_village_x = 60;
  attrs.pk_village_y = 70;

  auto cmd = ScrollTeleport::UseTownScroll(entity, "1", 2, attrs);
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->target_map_id, 5);
  EXPECT_EQ(cmd->target_x, 60);
  EXPECT_EQ(cmd->target_y, 70);
}

TEST(ScrollTeleportTest, DungeonScrollBlockedByNoRandomMove) {
  entt::registry registry;
  const auto entity = registry.create();

  MapAttributes attrs;
  attrs.no_random_move = true;

  auto cmd = ScrollTeleport::UseDungeonScroll(entity, "1", 0, false, attrs);
  EXPECT_FALSE(cmd.has_value());
}

TEST(ScrollTeleportTest, DungeonScrollBlockedBySiegeCooldown) {
  entt::registry registry;
  const auto entity = registry.create();

  MapAttributes attrs;
  attrs.no_random_move = false;

  const uint64_t now = static_cast<uint64_t>(mir2::core::GetCurrentTimestampMs());
  auto cmd = ScrollTeleport::UseDungeonScroll(entity, "1", now, true, attrs);
  EXPECT_FALSE(cmd.has_value());
}

TEST(ScrollTeleportTest, DungeonScrollUsesInjectedWalkabilityChecker) {
  entt::registry registry;
  const auto entity = registry.create();

  MapAttributes attrs;
  attrs.no_random_move = false;

  auto cmd = ScrollTeleport::UseDungeonScroll(
      entity,
      "3",
      0,
      false,
      attrs,
      8,
      8,
      [](int32_t x, int32_t y) { return x == 2 && y == 5; });
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->target_map_id, 3);
  EXPECT_EQ(cmd->target_x, 2);
  EXPECT_EQ(cmd->target_y, 5);
}

TEST(ScrollTeleportTest, DungeonScrollWithoutCheckerReturnsEmpty) {
  entt::registry registry;
  const auto entity = registry.create();

  MapAttributes attrs;
  attrs.no_random_move = false;

  auto cmd = ScrollTeleport::UseDungeonScroll(
      entity,
      "3",
      0,
      false,
      attrs,
      8,
      8);
  EXPECT_FALSE(cmd.has_value());
}

}  // namespace
