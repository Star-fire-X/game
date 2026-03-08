/**
 * @file area_trigger_test.cpp
 * @brief AreaTrigger 单元测试
 */

#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "game/map/area_event_processor.h"
#include "game/map/area_trigger.h"

namespace {

using mir2::game::map::AreaEffectType;
using mir2::game::map::AreaEventProcessor;
using mir2::game::map::AreaTrigger;

}  // namespace

TEST(AreaTriggerTest, EnterExitCallbacksInvoked) {
  AreaEventProcessor processor;

  bool entered = false;
  bool exited = false;
  entt::entity entered_entity = entt::null;
  entt::entity exited_entity = entt::null;

  AreaTrigger trigger;
  trigger.trigger_id = 42;
  trigger.center_x = 0;
  trigger.center_y = 0;
  trigger.radius = 5;
  trigger.effect_type = AreaEffectType::kDamage;
  trigger.on_enter = [&](entt::entity entity) {
    entered = true;
    entered_entity = entity;
  };
  trigger.on_exit = [&](entt::entity entity) {
    exited = true;
    exited_entity = entity;
  };

  processor.AddTrigger(trigger);

  entt::entity player = entt::entity{1};

  processor.CheckPlayerEnterExit(player, -1, -1, 0, 0);
  EXPECT_TRUE(entered);
  EXPECT_EQ(entered_entity, player);
  EXPECT_FALSE(exited);

  processor.CheckPlayerEnterExit(player, 0, 0, 100, 100);
  EXPECT_TRUE(exited);
  EXPECT_EQ(exited_entity, player);
}
