#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <algorithm>

#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/area_events.h"
#include "ecs/events/attribute_events.h"
#include "ecs/systems/level_up_system.h"
#include "game/event/global_event_manager.h"
#include "game/event/global_event_types.h"
#include "game/map/area_event_processor.h"
#include "game/map/area_trigger.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::CharacterStateComponent;
using mir2::ecs::EventBus;
using mir2::ecs::LevelUpSystem;
using mir2::ecs::events::AreaDamageTickEvent;
using mir2::ecs::events::AreaEnterEvent;
using mir2::ecs::events::AreaExitEvent;
using mir2::ecs::events::ExperienceGainedEvent;
using mir2::ecs::events::LevelUpEvent;
using mir2::game::map::AreaEffectType;
using mir2::game::map::AreaEventProcessor;
using mir2::game::map::AreaTrigger;
using mir2::game::map::ContinuousAreaEffect;
using legend2::game::event::GlobalEventManager;
using legend2::game::event::GlobalEventType;

entt::entity CreatePlayer(entt::registry& registry, int32_t x, int32_t y) {
    const auto entity = registry.create();

    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.id = 2001;
    identity.name = "Ranger";
    identity.char_class = mir2::common::CharacterClass::WARRIOR;
    identity.gender = mir2::common::Gender::FEMALE;

    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.level = 1;
    attributes.experience = 0;
    attributes.max_hp = 120;
    attributes.hp = 120;

    auto& state = registry.emplace<CharacterStateComponent>(entity);
    state.map_id = 1;
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

TEST(MapEventIntegrationTest, DamageZoneStopsAfterExit) {
    entt::registry registry;
    EventBus event_bus(registry);
    AreaEventProcessor processor;
    processor.SetEventBus(&event_bus);

    const auto player = CreatePlayer(registry, 0, 0);
    auto& attributes = registry.get<CharacterAttributesComponent>(player);

    AreaTrigger trigger;
    trigger.trigger_id = 11;
    trigger.center_x = 5;
    trigger.center_y = 5;
    trigger.radius = 3;
    trigger.effect_type = AreaEffectType::kDamage;
    processor.AddTrigger(trigger);

    ContinuousAreaEffect effect;
    effect.effect_id = 21;
    effect.type = AreaEffectType::kDamage;
    effect.center_x = 5;
    effect.center_y = 5;
    effect.radius = 3;
    effect.tick_interval = 1.0f;
    effect.damage_per_tick = 15;
    processor.AddContinuousEffect(effect);

    int enter_events = 0;
    int exit_events = 0;
    int damage_ticks = 0;

    event_bus.Subscribe<AreaEnterEvent>([&](const AreaEnterEvent& event) {
        if (event.entity == player) {
            ++enter_events;
        }
    });
    event_bus.Subscribe<AreaExitEvent>([&](const AreaExitEvent& event) {
        if (event.entity == player) {
            ++exit_events;
        }
    });
    event_bus.Subscribe<AreaDamageTickEvent>([&](const AreaDamageTickEvent& event) {
        if (event.entity != player) {
            return;
        }
        ++damage_ticks;
        attributes.hp = std::max(0, attributes.hp - event.damage);
    });

    MovePlayer(registry, player, 5, 5);
    processor.CheckPlayerEnterExit(player, -1, -1, 5, 5);
    processor.Update(1.0f, registry);

    EXPECT_EQ(enter_events, 1);
    EXPECT_EQ(exit_events, 0);
    EXPECT_EQ(damage_ticks, 1);
    const int hp_after_tick = attributes.hp;

    MovePlayer(registry, player, 20, 20);
    processor.CheckPlayerEnterExit(player, 5, 5, 20, 20);
    processor.Update(1.0f, registry);

    EXPECT_EQ(exit_events, 1);
    EXPECT_EQ(damage_ticks, 1);
    EXPECT_EQ(attributes.hp, hp_after_tick);
}

TEST(MapEventIntegrationTest, AreaEventAppliesDoubleExpThroughGlobalEvent) {
    entt::registry registry;
    EventBus event_bus(registry);
    AreaEventProcessor processor;
    processor.SetEventBus(&event_bus);

    const auto player = CreatePlayer(registry, 0, 0);
    auto& attributes = registry.get<CharacterAttributesComponent>(player);

    auto& global_events = GlobalEventManager::Instance();
    global_events.SetEventBus(&event_bus);
    const uint32_t event_id = 9001;
    global_events.StartEvent(event_id, GlobalEventType::kDoubleExp, 60, 2.0f);

    const int base_exp = 60;

    AreaTrigger trigger;
    trigger.trigger_id = 31;
    trigger.center_x = 0;
    trigger.center_y = 0;
    trigger.radius = 5;
    trigger.effect_type = AreaEffectType::kBuff;
    trigger.on_enter = [&](entt::entity entity) {
        if (entity != player) {
            return;
        }
        const int scaled_exp = static_cast<int>(
            global_events.ApplyExpRate(static_cast<float>(base_exp)));
        LevelUpSystem::GainExperience(registry, entity, scaled_exp, &event_bus);
    };
    processor.AddTrigger(trigger);

    int exp_events = 0;
    int level_up_events = 0;
    int last_exp_amount = 0;

    event_bus.Subscribe<ExperienceGainedEvent>([&](const ExperienceGainedEvent& event) {
        if (event.entity != player) {
            return;
        }
        ++exp_events;
        last_exp_amount = event.amount;
    });
    event_bus.Subscribe<LevelUpEvent>([&](const LevelUpEvent& event) {
        if (event.entity == player) {
            ++level_up_events;
        }
    });

    processor.CheckPlayerEnterExit(player, -1, -1, 0, 0);

    EXPECT_EQ(exp_events, 1);
    EXPECT_EQ(last_exp_amount, base_exp * 2);
    EXPECT_EQ(level_up_events, 1);
    EXPECT_EQ(attributes.level, 2);

    global_events.StopEvent(event_id);
    global_events.SetEventBus(nullptr);
}
