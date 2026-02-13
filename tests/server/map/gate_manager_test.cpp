/**
 * @file gate_manager_test.cpp
 * @brief GateManager 单元测试
 */

#include <gtest/gtest.h>

#include "game/map/gate_manager.h"

namespace {

using mir2::game::map::GateInfo;
using mir2::game::map::GateManager;

}  // namespace

TEST(GateManagerTest, AddGateAndTrigger) {
  GateManager manager;
  GateInfo gate{
      1,
      "1",
      10,
      20,
      "2",
      30,
      40,
      false,
      0
  };
  manager.AddGate(gate);

  auto result = manager.CheckGateTrigger("1", 10, 20);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->gate_id, 1u);
  EXPECT_EQ(result->target_map, "2");
  EXPECT_EQ(result->target_x, 30);
  EXPECT_EQ(result->target_y, 40);
}

TEST(GateManagerTest, CheckGateTriggerReturnsNulloptWhenMissing) {
  GateManager manager;
  GateInfo gate{
      2,
      "3",
      5,
      6,
      "4",
      7,
      8,
      false,
      0
  };
  manager.AddGate(gate);

  EXPECT_FALSE(manager.CheckGateTrigger("3", 5, 7).has_value());
  EXPECT_FALSE(manager.CheckGateTrigger("9", 5, 6).has_value());
}

TEST(GateManagerTest, LargeCoordinatesDoNotCollideWithLowBits) {
  GateManager manager;

  GateInfo high_coord_gate{
      10,
      "3",
      70000,
      70000,
      "8",
      1,
      2,
      false,
      0
  };
  GateInfo low_bits_gate{
      11,
      "3",
      4464,  // 70000 & 0xFFFF
      4464,  // 70000 & 0xFFFF
      "9",
      3,
      4,
      false,
      0
  };

  manager.AddGate(high_coord_gate);
  manager.AddGate(low_bits_gate);

  auto high_result = manager.CheckGateTrigger("3", 70000, 70000);
  ASSERT_TRUE(high_result.has_value());
  EXPECT_EQ(high_result->gate_id, 10u);
  EXPECT_EQ(high_result->target_map, "8");

  auto low_result = manager.CheckGateTrigger("3", 4464, 4464);
  ASSERT_TRUE(low_result.has_value());
  EXPECT_EQ(low_result->gate_id, 11u);
  EXPECT_EQ(low_result->target_map, "9");
}

TEST(GateManagerTest, NumericMapIdLookupAvoidsStringFormattingInHotPath) {
  GateManager manager;
  GateInfo gate{
      21,
      "3",
      100,
      200,
      "7",
      300,
      400,
      false,
      0
  };
  manager.AddGate(gate);

  auto result = manager.CheckGateTrigger(3, 100, 200);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->gate_id, 21u);
  EXPECT_EQ(result->target_map, "7");
}

TEST(GateManagerTest, NumericFallbackSupportsLeadingZeroMapIds) {
  GateManager manager;
  GateInfo gate{
      22,
      "003",
      11,
      22,
      "8",
      33,
      44,
      false,
      0
  };
  manager.AddGate(gate);

  // 精确字符串查找（原始配置值）仍然可用。
  auto exact = manager.CheckGateTrigger("003", 11, 22);
  ASSERT_TRUE(exact.has_value());
  EXPECT_EQ(exact->gate_id, 22u);

  // 数值路径/规范化字符串路径也可命中同一条门配置。
  auto numeric = manager.CheckGateTrigger(3, 11, 22);
  ASSERT_TRUE(numeric.has_value());
  EXPECT_EQ(numeric->gate_id, 22u);

  auto normalized_string = manager.CheckGateTrigger("3", 11, 22);
  ASSERT_TRUE(normalized_string.has_value());
  EXPECT_EQ(normalized_string->gate_id, 22u);
}
