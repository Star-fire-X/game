/**
 * @file map_attributes_test.cpp
 * @brief MapAttributes 单元测试
 */

#include <gtest/gtest.h>

#include "game/map/map_attributes.h"
#include "game/map/map_instance.h"

namespace {

using mir2::game::map::MapAttributes;
using mir2::game::map::MapInstance;
using mir2::game::map::SafeZone;

}  // namespace

TEST(SafeZoneTest, ContainsReturnsTrueForPointInside) {
  SafeZone zone{100, 100, 50};
  EXPECT_TRUE(zone.Contains(120, 120));
  EXPECT_FALSE(zone.Contains(200, 200));
}

TEST(MapAttributesTest, LevelRequirementCheck) {
  MapInstance map(1, 100, 100);
  MapAttributes attrs;
  attrs.min_level = 10;
  attrs.max_level = 20;
  map.SetAttributes(attrs);

  EXPECT_FALSE(map.CheckLevelRequirement(9));
  EXPECT_TRUE(map.CheckLevelRequirement(10));
  EXPECT_TRUE(map.CheckLevelRequirement(20));
  EXPECT_FALSE(map.CheckLevelRequirement(21));
}

TEST(MapAttributesTest, CanPKRespectsSafeZoneOverride) {
  MapInstance map(1, 100, 100);
  MapAttributes attrs;
  attrs.is_pk_zone = true;
  attrs.is_safe_zone = false;
  map.SetAttributes(attrs);
  EXPECT_TRUE(map.CanPK());

  attrs.is_safe_zone = true;
  map.SetAttributes(attrs);
  EXPECT_FALSE(map.CanPK());
}
