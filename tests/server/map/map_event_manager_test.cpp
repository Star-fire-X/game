/**
 * @file map_event_manager_test.cpp
 * @brief MapEventManager 单元测试
 */

#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/area_events.h"
#include "game/map/map_instance.h"

#define private public
#include "game/map/map_event_manager.h"
#undef private

namespace {

using mir2::game::map::AreaEffectType;
using mir2::game::map::MapEventManager;
using mir2::game::map::MapInstance;

}  // namespace

TEST(MapEventManagerTest, AddEventsCreateRecords) {
  MapEventManager manager;

  uint32_t fire_id = manager.AddFireEvent(10, 20, 5.0f, 12);
  uint32_t holy_id = manager.AddHolyCurtainEvent(3, 4, 4.0f, entt::entity{7}, 8);
  uint32_t mine_id = manager.AddMineEvent(6, 7, 2);

  ASSERT_EQ(manager.active_events_.size(), 3u);

  const auto& fire = manager.active_events_[0];
  EXPECT_EQ(fire.event_id, fire_id);
  EXPECT_EQ(fire.trigger_id, fire_id);
  EXPECT_EQ(fire.effect.effect_id, fire_id);
  EXPECT_EQ(fire.effect.type, AreaEffectType::kFire);
  EXPECT_EQ(fire.effect.center_x, 10);
  EXPECT_EQ(fire.effect.center_y, 20);
  EXPECT_EQ(fire.effect.radius, 0);
  EXPECT_FLOAT_EQ(fire.effect.tick_interval, 1.0f);
  EXPECT_EQ(fire.effect.damage_per_tick, 12);
  EXPECT_FLOAT_EQ(fire.effect.duration, 5.0f);
  EXPECT_FALSE(fire.closed);

  const auto& holy = manager.active_events_[1];
  EXPECT_EQ(holy.event_id, holy_id);
  EXPECT_EQ(holy.trigger_id, holy_id);
  EXPECT_EQ(holy.effect.effect_id, holy_id);
  EXPECT_EQ(holy.effect.type, AreaEffectType::kHolyCurtain);
  EXPECT_EQ(holy.effect.center_x, 3);
  EXPECT_EQ(holy.effect.center_y, 4);
  EXPECT_EQ(holy.effect.radius, 0);
  EXPECT_FLOAT_EQ(holy.effect.tick_interval, 1.0f);
  EXPECT_EQ(holy.effect.damage_per_tick, 8);
  EXPECT_FLOAT_EQ(holy.effect.duration, 4.0f);
  EXPECT_EQ(holy.effect.caster, entt::entity{7});
  EXPECT_FALSE(holy.closed);

  const auto& mine = manager.active_events_[2];
  EXPECT_EQ(mine.event_id, mine_id);
  EXPECT_EQ(mine.trigger_id, mine_id);
  EXPECT_EQ(mine.effect.effect_id, mine_id);
  EXPECT_EQ(mine.effect.type, AreaEffectType::kMine);
  EXPECT_EQ(mine.effect.center_x, 6);
  EXPECT_EQ(mine.effect.center_y, 7);
  EXPECT_EQ(mine.effect.radius, 2);
  EXPECT_FLOAT_EQ(mine.effect.tick_interval, 1.0f);
  EXPECT_FLOAT_EQ(mine.effect.duration, 0.0f);
  EXPECT_FALSE(mine.closed);
}

TEST(MapEventManagerTest, RemoveEventMovesToClosedList) {
  MapEventManager manager;
  entt::registry registry;

  uint32_t fire_id = manager.AddFireEvent(1, 2, 5.0f, 10);
  manager.Update(1.0f, registry, nullptr);

  manager.RemoveEvent(fire_id);

  EXPECT_TRUE(manager.active_events_.empty());
  ASSERT_EQ(manager.closed_list_.size(), 1u);
  const auto& closed = manager.closed_list_[0];
  EXPECT_EQ(closed.event_id, fire_id);
  EXPECT_TRUE(closed.closed);
  EXPECT_FLOAT_EQ(closed.elapsed_time, 0.0f);
}

TEST(MapEventManagerTest, UpdateMovesExpiredEventsToClosedList) {
  MapEventManager manager;
  entt::registry registry;

  uint32_t fire_id = manager.AddFireEvent(1, 1, 1.0f, 3);
  uint32_t mine_id = manager.AddMineEvent(2, 2, 3);

  manager.Update(1.1f, registry, nullptr);

  ASSERT_EQ(manager.active_events_.size(), 1u);
  EXPECT_EQ(manager.active_events_[0].event_id, mine_id);

  ASSERT_EQ(manager.closed_list_.size(), 1u);
  EXPECT_EQ(manager.closed_list_[0].event_id, fire_id);
  EXPECT_TRUE(manager.closed_list_[0].closed);
}

TEST(MapEventManagerTest, CleanupRemovesOldClosedEvents) {
  MapEventManager manager;
  entt::registry registry;

  uint32_t fire_id = manager.AddFireEvent(5, 5, 1.0f, 6);
  manager.RemoveEvent(fire_id);

  manager.Update(299.0f, registry, nullptr);
  manager.CleanupClosedEvents();
  EXPECT_EQ(manager.closed_list_.size(), 1u);

  manager.Update(2.0f, registry, nullptr);
  manager.CleanupClosedEvents();
  EXPECT_TRUE(manager.closed_list_.empty());
}

TEST(MapEventManagerTest, RemoveEventAlsoRemovesRegisteredContinuousEffect) {
  entt::registry registry;
  auto entity = registry.create();
  auto& state =
      registry.emplace<mir2::ecs::CharacterStateComponent>(entity);
  state.position.x = 10;
  state.position.y = 20;

  mir2::ecs::EventBus event_bus(registry);
  int fire_ticks = 0;
  event_bus.Subscribe<mir2::ecs::events::FireBurnTickEvent>(
      [&](const mir2::ecs::events::FireBurnTickEvent&) {
        ++fire_ticks;
      });

  MapInstance map(1, 100, 100);
  map.SetEventBus(&event_bus);
  ASSERT_TRUE(map.AddEntity(entity, 10, 20));

  MapEventManager manager;
  const uint32_t fire_id = manager.AddFireEvent(10, 20, 5.0f, 6);

  manager.Update(1.0f, registry, &map);
  EXPECT_EQ(fire_ticks, 1);

  manager.RemoveEvent(fire_id, &map);
  manager.Update(1.0f, registry, &map);
  EXPECT_EQ(fire_ticks, 1);
}
