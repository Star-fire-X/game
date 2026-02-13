/**
 * @file area_event_processor_test.cpp
 * @brief AreaEventProcessor 单元测试
 */

#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/area_events.h"
#include "game/map/area_event_processor.h"
#include "game/map/area_trigger.h"

namespace {

using mir2::ecs::CharacterStateComponent;
using mir2::ecs::EventBus;
using mir2::ecs::events::AreaDamageTickEvent;
using mir2::ecs::events::AreaEnterEvent;
using mir2::ecs::events::AreaExitEvent;
using mir2::ecs::events::AreaHealTickEvent;
using mir2::ecs::events::FireBurnTickEvent;
using mir2::ecs::events::HolyCurtainTickEvent;
using mir2::ecs::events::MineEvent;
using mir2::game::map::AreaEffectType;
using mir2::game::map::AreaEventProcessor;
using mir2::game::map::AreaTrigger;
using mir2::game::map::ContinuousAreaEffect;

entt::entity CreatePlayer(entt::registry& registry, int32_t x, int32_t y) {
  entt::entity entity = registry.create();
  auto& state = registry.emplace<CharacterStateComponent>(entity);
  state.position.x = x;
  state.position.y = y;
  return entity;
}

void MovePlayer(entt::registry& registry, entt::entity entity, int32_t x, int32_t y) {
  auto* state = registry.try_get<CharacterStateComponent>(entity);
  if (state) {
    state->position.x = x;
    state->position.y = y;
  }
}

}  // namespace

TEST(AreaEventProcessorTest, CheckPlayerEnterExitFiresCallbacksAndEvents) {
  entt::registry registry;
  EventBus event_bus(registry);
  AreaEventProcessor processor;
  processor.SetEventBus(&event_bus);

  int enter_calls = 0;
  int exit_calls = 0;
  entt::entity entered = entt::null;
  entt::entity exited = entt::null;

  AreaTrigger trigger;
  trigger.trigger_id = 100;
  trigger.center_x = 10;
  trigger.center_y = 10;
  trigger.radius = 5;
  trigger.effect_type = AreaEffectType::kDamage;
  trigger.on_enter = [&](entt::entity entity) {
    entered = entity;
    ++enter_calls;
  };
  trigger.on_exit = [&](entt::entity entity) {
    exited = entity;
    ++exit_calls;
  };

  processor.AddTrigger(trigger);

  int enter_events = 0;
  int exit_events = 0;
  AreaEnterEvent captured_enter{};
  AreaExitEvent captured_exit{};
  event_bus.Subscribe<AreaEnterEvent>([&](const AreaEnterEvent& event) {
    captured_enter = event;
    ++enter_events;
  });
  event_bus.Subscribe<AreaExitEvent>([&](const AreaExitEvent& event) {
    captured_exit = event;
    ++exit_events;
  });

  entt::entity player = registry.create();

  processor.CheckPlayerEnterExit(player, -1, -1, 10, 10);

  EXPECT_EQ(enter_calls, 1);
  EXPECT_EQ(entered, player);
  EXPECT_EQ(enter_events, 1);
  EXPECT_EQ(captured_enter.entity, player);
  EXPECT_EQ(captured_enter.trigger_id, trigger.trigger_id);
  EXPECT_EQ(captured_enter.effect_type, trigger.effect_type);

  processor.CheckPlayerEnterExit(player, 10, 10, 30, 30);

  EXPECT_EQ(exit_calls, 1);
  EXPECT_EQ(exited, player);
  EXPECT_EQ(exit_events, 1);
  EXPECT_EQ(captured_exit.entity, player);
  EXPECT_EQ(captured_exit.trigger_id, trigger.trigger_id);
  EXPECT_EQ(captured_exit.effect_type, trigger.effect_type);
}

TEST(AreaEventProcessorTest, UpdateDispatchesDamageAndHealTicksForOverlappingEffects) {
  entt::registry registry;
  EventBus event_bus(registry);
  AreaEventProcessor processor;
  processor.SetEventBus(&event_bus);

  entt::entity player = CreatePlayer(registry, 100, 100);

  int damage_ticks = 0;
  int heal_ticks = 0;
  AreaDamageTickEvent last_damage{};
  AreaHealTickEvent last_heal{};
  event_bus.Subscribe<AreaDamageTickEvent>([&](const AreaDamageTickEvent& event) {
    last_damage = event;
    ++damage_ticks;
  });
  event_bus.Subscribe<AreaHealTickEvent>([&](const AreaHealTickEvent& event) {
    last_heal = event;
    ++heal_ticks;
  });

  ContinuousAreaEffect damage;
  damage.effect_id = 1;
  damage.type = AreaEffectType::kDamage;
  damage.center_x = 100;
  damage.center_y = 100;
  damage.radius = 10;
  damage.tick_interval = 1.0f;
  damage.damage_per_tick = 12;

  ContinuousAreaEffect heal;
  heal.effect_id = 2;
  heal.type = AreaEffectType::kHeal;
  heal.center_x = 100;
  heal.center_y = 100;
  heal.radius = 10;
  heal.tick_interval = 1.0f;
  heal.damage_per_tick = 7;

  processor.AddContinuousEffect(damage);
  processor.AddContinuousEffect(heal);

  processor.Update(1.0f, registry);

  EXPECT_EQ(damage_ticks, 1);
  EXPECT_EQ(heal_ticks, 1);
  EXPECT_EQ(last_damage.entity, player);
  EXPECT_EQ(last_damage.effect_id, damage.effect_id);
  EXPECT_EQ(last_damage.damage, damage.damage_per_tick);
  EXPECT_EQ(last_heal.entity, player);
  EXPECT_EQ(last_heal.effect_id, heal.effect_id);
  EXPECT_EQ(last_heal.heal, heal.damage_per_tick);
}

TEST(AreaEventProcessorTest, LeavingAreaStopsContinuousDamage) {
  entt::registry registry;
  EventBus event_bus(registry);
  AreaEventProcessor processor;
  processor.SetEventBus(&event_bus);

  entt::entity player = CreatePlayer(registry, 50, 50);

  int damage_ticks = 0;
  event_bus.Subscribe<AreaDamageTickEvent>([&](const AreaDamageTickEvent&) {
    ++damage_ticks;
  });

  ContinuousAreaEffect damage;
  damage.effect_id = 5;
  damage.type = AreaEffectType::kDamage;
  damage.center_x = 50;
  damage.center_y = 50;
  damage.radius = 5;
  damage.tick_interval = 1.0f;
  damage.damage_per_tick = 3;

  processor.AddContinuousEffect(damage);

  processor.Update(1.0f, registry);
  EXPECT_EQ(damage_ticks, 1);

  MovePlayer(registry, player, 100, 100);
  processor.Update(1.0f, registry);
  EXPECT_EQ(damage_ticks, 1);
}

TEST(AreaEventProcessorTest, UpdateHonorsTickIntervalAccumulation) {
  entt::registry registry;
  EventBus event_bus(registry);
  AreaEventProcessor processor;
  processor.SetEventBus(&event_bus);

  CreatePlayer(registry, 0, 0);

  int damage_ticks = 0;
  event_bus.Subscribe<AreaDamageTickEvent>([&](const AreaDamageTickEvent&) {
    ++damage_ticks;
  });

  ContinuousAreaEffect damage;
  damage.effect_id = 8;
  damage.type = AreaEffectType::kDamage;
  damage.center_x = 0;
  damage.center_y = 0;
  damage.radius = 10;
  damage.tick_interval = 1.0f;
  damage.damage_per_tick = 1;

  processor.AddContinuousEffect(damage);

  processor.Update(0.4f, registry);
  EXPECT_EQ(damage_ticks, 0);
  processor.Update(0.4f, registry);
  EXPECT_EQ(damage_ticks, 0);
  processor.Update(0.4f, registry);
  EXPECT_EQ(damage_ticks, 1);
}

TEST(AreaEventProcessorTest, UpdateDispatchesFireMineAndHolyCurtainTicks) {
  entt::registry registry;
  EventBus event_bus(registry);
  AreaEventProcessor processor;
  processor.SetEventBus(&event_bus);

  entt::entity player = CreatePlayer(registry, 20, 20);
  entt::entity caster = registry.create();

  int fire_ticks = 0;
  int mine_ticks = 0;
  int holy_ticks = 0;
  FireBurnTickEvent last_fire{};
  MineEvent last_mine{};
  HolyCurtainTickEvent last_holy{};

  event_bus.Subscribe<FireBurnTickEvent>([&](const FireBurnTickEvent& event) {
    last_fire = event;
    ++fire_ticks;
  });
  event_bus.Subscribe<MineEvent>([&](const MineEvent& event) {
    last_mine = event;
    ++mine_ticks;
  });
  event_bus.Subscribe<HolyCurtainTickEvent>([&](const HolyCurtainTickEvent& event) {
    last_holy = event;
    ++holy_ticks;
  });

  ContinuousAreaEffect fire;
  fire.effect_id = 11;
  fire.type = AreaEffectType::kFire;
  fire.center_x = 20;
  fire.center_y = 20;
  fire.radius = 5;
  fire.tick_interval = 1.0f;
  fire.damage_per_tick = 6;
  fire.duration = 3.5f;

  ContinuousAreaEffect mine;
  mine.effect_id = 12;
  mine.type = AreaEffectType::kMine;
  mine.center_x = 20;
  mine.center_y = 20;
  mine.radius = 4;
  mine.tick_interval = 1.0f;

  ContinuousAreaEffect holy;
  holy.effect_id = 13;
  holy.type = AreaEffectType::kHolyCurtain;
  holy.caster = caster;
  holy.center_x = 20;
  holy.center_y = 20;
  holy.radius = 6;
  holy.tick_interval = 1.0f;
  holy.damage_per_tick = 9;

  processor.AddContinuousEffect(fire);
  processor.AddContinuousEffect(mine);
  processor.AddContinuousEffect(holy);

  processor.Update(1.0f, registry);

  EXPECT_EQ(fire_ticks, 1);
  EXPECT_EQ(mine_ticks, 1);
  EXPECT_EQ(holy_ticks, 1);

  EXPECT_EQ(last_fire.trigger_id, fire.effect_id);
  EXPECT_EQ(last_fire.target, player);
  EXPECT_EQ(last_fire.damage, fire.damage_per_tick);
  EXPECT_FLOAT_EQ(last_fire.burn_duration, fire.duration);

  EXPECT_EQ(last_mine.trigger_id, mine.effect_id);
  EXPECT_EQ(last_mine.target, player);
  EXPECT_EQ(last_mine.blast_radius, mine.radius);

  EXPECT_EQ(last_holy.trigger_id, holy.effect_id);
  EXPECT_EQ(last_holy.caster, caster);
  EXPECT_EQ(last_holy.target, player);
  EXPECT_EQ(last_holy.shield_bonus, holy.damage_per_tick);
}
