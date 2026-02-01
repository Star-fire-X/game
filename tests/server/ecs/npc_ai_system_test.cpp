#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/components/npc_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"

#define private public
#include "ecs/systems/npc_ai_system.h"
#undef private

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::DirtyComponent;
using mir2::ecs::EventBus;
using mir2::ecs::InventoryOwnerComponent;
using mir2::ecs::ItemComponent;
using mir2::ecs::NpcAIComponent;
using mir2::ecs::NpcStateComponent;
using mir2::ecs::TransformComponent;
using mir2::ecs::dirty_tracker::is_dirty;
using mir2::ecs::events::NpcGetGoldEvent;
using mir2::ecs::events::NpcGetGoldResultEvent;
using mir2::ecs::events::NpcGetPlayerLevelEvent;
using mir2::ecs::events::NpcGetPlayerLevelResultEvent;
using mir2::ecs::events::NpcGetPlayerNameEvent;
using mir2::ecs::events::NpcGetPlayerNameResultEvent;
using mir2::ecs::events::NpcHasItemEvent;
using mir2::ecs::events::NpcHasItemResultEvent;
using mir2::game::npc::NpcAISystem;
using mir2::game::npc::NpcState;

struct MockEventBus {
    MockEventBus() : bus(registry) {}

    entt::registry registry;
    EventBus bus;
};

entt::entity CreateNpc(entt::registry& registry, int32_t x = 0, int32_t y = 0) {
    const auto entity = registry.create();

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.map_id = 1;
    transform.position.x = x;
    transform.position.y = y;

    auto& state = registry.emplace<NpcStateComponent>(entity);
    state.current_state = NpcState::Idle;
    state.previous_state = NpcState::Idle;

    registry.emplace<NpcAIComponent>(entity);
    return entity;
}

entt::entity CreatePlayer(entt::registry& registry) {
    return registry.create();
}

void AddAttributes(entt::registry& registry, entt::entity entity,
                   int level, int gold) {
    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.level = level;
    attributes.gold = gold;
}

void AddIdentity(entt::registry& registry, entt::entity entity,
                 const std::string& name) {
    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.name = name;
}

entt::entity CreateItem(entt::registry& registry, entt::entity owner,
                        uint32_t item_id, int count, int slot_index) {
    const auto item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.count = count;

    auto& owner_component = registry.emplace<InventoryOwnerComponent>(item);
    owner_component.owner = owner;
    owner_component.slot_index = slot_index;
    return item;
}

}  // namespace

TEST(NpcAISystemTest, DefaultConstructorDoesNotRegisterQueryHandlers) {
    MockEventBus mock_bus;
    NpcAISystem system;

    NpcHasItemResultEvent captured{};
    int result_count = 0;
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent& event) {
            captured = event;
            ++result_count;
        });

    bool handled = false;
    bool result = true;
    NpcHasItemEvent query{};
    query.player = entt::entity{42};
    query.npc_id = 7;
    query.request_id = 1;
    query.item_id = 200;
    query.count = 1;
    query.result = &result;
    query.handled = &handled;

    mock_bus.bus.Publish(query);

    EXPECT_EQ(result_count, 0);
    EXPECT_FALSE(handled);
    EXPECT_TRUE(result);
    EXPECT_EQ(captured.request_id, 0u);
}

TEST(NpcAISystemTest, ParameterizedConstructorRegistersQueryHandlers) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    NpcHasItemResultEvent captured{};
    int result_count = 0;
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent& event) {
            captured = event;
            ++result_count;
        });

    bool handled = false;
    bool result = false;
    NpcHasItemEvent query{};
    query.player = entt::entity{999};
    query.npc_id = 9;
    query.request_id = 33;
    query.item_id = 1;
    query.count = 1;
    query.result = &result;
    query.handled = &handled;

    mock_bus.bus.Publish(query);

    EXPECT_EQ(result_count, 1);
    EXPECT_TRUE(handled);
    EXPECT_FALSE(result);
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.request_id, 33u);
    EXPECT_EQ(captured.error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
}

TEST(NpcAISystemTest, RegisterQueryHandlersIdempotent) {
    MockEventBus mock_bus;
    NpcAISystem system;

    system.RegisterQueryHandlers(mock_bus.registry, mock_bus.bus);
    system.RegisterQueryHandlers(mock_bus.registry, mock_bus.bus);

    int result_count = 0;
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent&) { ++result_count; });

    NpcHasItemEvent query{};
    query.player = entt::entity{1};
    query.npc_id = 2;
    query.item_id = 10;
    query.count = 1;

    mock_bus.bus.Publish(query);

    EXPECT_EQ(result_count, 1);
}

TEST(NpcAISystemTest, UpdateIgnoresNonPositiveDelta) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    auto& state = registry.get<NpcStateComponent>(npc);
    state.state_timer = 1.25f;

    system.Update(registry, 0.0f);

    EXPECT_EQ(state.state_timer, 1.25f);
    EXPECT_EQ(state.current_state, NpcState::Idle);
}

TEST(NpcAISystemTest, UpdateSkipsPendingInteractionWhenDeltaIsNonPositive) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    const auto player = CreatePlayer(registry);

    system.OnPlayerInteract(npc, player);
    system.Update(registry, 0.0f);

    const auto& state = registry.get<NpcStateComponent>(npc);
    EXPECT_EQ(state.current_state, NpcState::Idle);
    EXPECT_EQ(state.interacting_player, static_cast<entt::entity>(entt::null));
}

TEST(NpcAISystemTest, UpdateProcessesPendingInteraction) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    const auto player = CreatePlayer(registry);

    system.OnPlayerInteract(npc, player);
    system.Update(registry, 0.1f);

    const auto& state = registry.get<NpcStateComponent>(npc);
    EXPECT_EQ(state.current_state, NpcState::Interact);
    EXPECT_EQ(state.previous_state, NpcState::Idle);
    EXPECT_EQ(state.interacting_player, player);
}

TEST(NpcAISystemTest, PendingInteractionsClearedAfterUpdate) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    const auto player = CreatePlayer(registry);

    system.OnPlayerInteract(npc, player);
    ASSERT_EQ(system.pending_interactions_.size(), 1u);

    system.Update(registry, 0.1f);

    EXPECT_TRUE(system.pending_interactions_.empty());
}

TEST(NpcAISystemTest, InteractionIgnoredForInvalidNpc) {
    entt::registry registry;
    NpcAISystem system;

    system.OnPlayerInteract(entt::entity{123}, entt::entity{456});
    system.Update(registry, 0.1f);

    EXPECT_TRUE(system.runtime_.empty());
}

TEST(NpcAISystemTest, InteractionRequiresComponents) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = registry.create();
    registry.emplace<NpcStateComponent>(npc);
    registry.emplace<TransformComponent>(npc);

    system.OnPlayerInteract(npc, entt::entity{1});
    system.Update(registry, 0.1f);

    EXPECT_TRUE(system.runtime_.empty());
}

TEST(NpcAISystemTest, UpdateTransitionsToPatrolWhenIdleDurationElapsed) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    auto& ai = registry.get<NpcAIComponent>(npc);
    ai.enable_patrol = true;
    ai.idle_duration = 0.0f;
    ai.patrol_points = {{1, 0}};

    system.Update(registry, 0.1f);

    const auto& state = registry.get<NpcStateComponent>(npc);
    EXPECT_EQ(state.current_state, NpcState::Patrol);
    EXPECT_EQ(state.previous_state, NpcState::Idle);
}

TEST(NpcAISystemTest, UpdateDoesNotAdvanceWhenDead) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    auto& state = registry.get<NpcStateComponent>(npc);
    state.current_state = NpcState::Dead;

    system.Update(registry, 0.5f);

    EXPECT_EQ(state.current_state, NpcState::Dead);
    EXPECT_FLOAT_EQ(state.state_timer, 0.0f);
}

TEST(NpcAISystemTest, UpdateMovesNpcAndMarksDirty) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry, 0, 0);
    auto& ai = registry.get<NpcAIComponent>(npc);
    ai.enable_patrol = true;
    ai.idle_duration = 0.0f;
    ai.patrol_speed = 10.0f;
    ai.patrol_points = {{1, 0}};

    system.Update(registry, 0.2f);
    system.Update(registry, 0.2f);

    const auto& transform = registry.get<TransformComponent>(npc);
    EXPECT_EQ(transform.position, (mir2::common::Position{1, 0}));

    const auto* dirty = registry.try_get<DirtyComponent>(npc);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->state_dirty);
    EXPECT_TRUE(is_dirty(registry, npc));
}

TEST(NpcAISystemTest, CacheDoesNotResetPatrolIndexWhenConfigUnchanged) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    auto& ai = registry.get<NpcAIComponent>(npc);
    ai.enable_patrol = true;
    ai.idle_duration = 100.0f;
    ai.patrol_points = {{1, 0}, {2, 0}};

    system.Update(registry, 0.1f);

    ai.current_patrol_index = 1;
    system.Update(registry, 0.1f);

    EXPECT_EQ(ai.current_patrol_index, 1u);
}

TEST(NpcAISystemTest, CacheResetsPatrolIndexWhenPointsChanged) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    auto& ai = registry.get<NpcAIComponent>(npc);
    ai.enable_patrol = true;
    ai.idle_duration = 100.0f;
    ai.patrol_points = {{1, 0}, {2, 0}};

    system.Update(registry, 0.1f);

    ai.current_patrol_index = 1;
    ai.patrol_points.push_back({3, 0});

    system.Update(registry, 0.1f);

    EXPECT_EQ(ai.current_patrol_index, 0u);
}

TEST(NpcAISystemTest, CacheResetsPatrolIndexWhenPatrolDisabled) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);
    auto& ai = registry.get<NpcAIComponent>(npc);
    ai.enable_patrol = true;
    ai.idle_duration = 100.0f;
    ai.patrol_points = {{1, 0}, {2, 0}};

    system.Update(registry, 0.1f);

    ai.current_patrol_index = 1;
    ai.enable_patrol = false;

    system.Update(registry, 0.1f);

    EXPECT_EQ(ai.current_patrol_index, 0u);
}

TEST(NpcAISystemTest, HandleHasItemInvalidPlayer) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    NpcHasItemResultEvent captured{};
    int result_count = 0;
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent& event) {
            captured = event;
            ++result_count;
        });

    bool handled = false;
    bool result = true;
    NpcHasItemEvent query{};
    query.player = entt::entity{999};
    query.npc_id = 1;
    query.item_id = 100;
    query.count = 1;
    query.result = &result;
    query.handled = &handled;

    mock_bus.bus.Publish(query);

    EXPECT_EQ(result_count, 1);
    EXPECT_TRUE(handled);
    EXPECT_FALSE(result);
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
}

TEST(NpcAISystemTest, HandleHasItemInvalidParameters) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    std::vector<NpcHasItemResultEvent> results;
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent& event) { results.push_back(event); });

    const auto player = CreatePlayer(mock_bus.registry);

    bool handled_first = false;
    NpcHasItemEvent first{};
    first.player = player;
    first.npc_id = 1;
    first.item_id = 0;
    first.count = 1;
    first.handled = &handled_first;

    bool handled_second = false;
    NpcHasItemEvent second{};
    second.player = player;
    second.npc_id = 1;
    second.item_id = 10;
    second.count = 0;
    second.handled = &handled_second;

    mock_bus.bus.Publish(first);
    mock_bus.bus.Publish(second);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(handled_first);
    EXPECT_TRUE(handled_second);
    EXPECT_FALSE(results[0].success);
    EXPECT_FALSE(results[1].success);
    EXPECT_EQ(results[0].error_code, mir2::common::ErrorCode::INVALID_ACTION);
    EXPECT_EQ(results[1].error_code, mir2::common::ErrorCode::INVALID_ACTION);
}

TEST(NpcAISystemTest, HandleHasItemSuccessAndResultPointer) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    NpcHasItemResultEvent captured{};
    int result_count = 0;
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent& event) {
            captured = event;
            ++result_count;
        });

    const auto player = CreatePlayer(mock_bus.registry);
    CreateItem(mock_bus.registry, player, 200, 2, 0);

    bool handled = false;
    bool result = false;
    NpcHasItemEvent query{};
    query.player = player;
    query.npc_id = 5;
    query.item_id = 200;
    query.count = 2;
    query.result = &result;
    query.handled = &handled;

    mock_bus.bus.Publish(query);

    EXPECT_EQ(result_count, 1);
    EXPECT_TRUE(handled);
    EXPECT_TRUE(result);
    EXPECT_TRUE(captured.success);
    EXPECT_TRUE(captured.has_item);
    EXPECT_EQ(captured.error_code, mir2::common::ErrorCode::SUCCESS);
}

TEST(NpcAISystemTest, HandleHasItemReturnsFalseWhenItemMissing) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    NpcHasItemResultEvent captured{};
    mock_bus.bus.Subscribe<NpcHasItemResultEvent>(
        [&](const NpcHasItemResultEvent& event) { captured = event; });

    const auto player = CreatePlayer(mock_bus.registry);

    NpcHasItemEvent query{};
    query.player = player;
    query.npc_id = 6;
    query.item_id = 300;
    query.count = 1;

    mock_bus.bus.Publish(query);

    EXPECT_TRUE(captured.success);
    EXPECT_FALSE(captured.has_item);
    EXPECT_EQ(captured.error_code, mir2::common::ErrorCode::SUCCESS);
}

TEST(NpcAISystemTest, HandleGetGoldFailureAndSuccess) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    std::vector<NpcGetGoldResultEvent> results;
    mock_bus.bus.Subscribe<NpcGetGoldResultEvent>(
        [&](const NpcGetGoldResultEvent& event) { results.push_back(event); });

    NpcGetGoldEvent invalid{};
    invalid.player = entt::entity{999};
    invalid.npc_id = 1;

    const auto player_missing = CreatePlayer(mock_bus.registry);
    NpcGetGoldEvent missing_attrs{};
    missing_attrs.player = player_missing;
    missing_attrs.npc_id = 1;

    const auto player = CreatePlayer(mock_bus.registry);
    AddAttributes(mock_bus.registry, player, 10, 1234);
    int amount = 0;
    bool handled = false;
    NpcGetGoldEvent success{};
    success.player = player;
    success.npc_id = 1;
    success.amount = &amount;
    success.handled = &handled;

    mock_bus.bus.Publish(invalid);
    mock_bus.bus.Publish(missing_attrs);
    mock_bus.bus.Publish(success);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_FALSE(results[0].success);
    EXPECT_EQ(results[0].error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    EXPECT_FALSE(results[1].success);
    EXPECT_EQ(results[1].error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    EXPECT_TRUE(results[2].success);
    EXPECT_EQ(results[2].amount, 1234);
    EXPECT_EQ(amount, 1234);
    EXPECT_TRUE(handled);
}

TEST(NpcAISystemTest, HandleGetPlayerNameFailureAndSuccess) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    std::vector<NpcGetPlayerNameResultEvent> results;
    mock_bus.bus.Subscribe<NpcGetPlayerNameResultEvent>(
        [&](const NpcGetPlayerNameResultEvent& event) { results.push_back(event); });

    NpcGetPlayerNameEvent invalid{};
    invalid.player = entt::entity{999};
    invalid.npc_id = 1;

    const auto player_missing = CreatePlayer(mock_bus.registry);
    NpcGetPlayerNameEvent missing_identity{};
    missing_identity.player = player_missing;
    missing_identity.npc_id = 1;

    const auto player = CreatePlayer(mock_bus.registry);
    AddIdentity(mock_bus.registry, player, "Hero");
    std::string name;
    bool handled = false;
    NpcGetPlayerNameEvent success{};
    success.player = player;
    success.npc_id = 1;
    success.name = &name;
    success.handled = &handled;

    mock_bus.bus.Publish(invalid);
    mock_bus.bus.Publish(missing_identity);
    mock_bus.bus.Publish(success);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_FALSE(results[0].success);
    EXPECT_EQ(results[0].error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    EXPECT_FALSE(results[1].success);
    EXPECT_EQ(results[1].error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    EXPECT_TRUE(results[2].success);
    EXPECT_EQ(results[2].name, "Hero");
    EXPECT_EQ(name, "Hero");
    EXPECT_TRUE(handled);
}

TEST(NpcAISystemTest, HandleGetPlayerLevelFailureAndSuccess) {
    MockEventBus mock_bus;
    NpcAISystem system(mock_bus.registry, mock_bus.bus);

    std::vector<NpcGetPlayerLevelResultEvent> results;
    mock_bus.bus.Subscribe<NpcGetPlayerLevelResultEvent>(
        [&](const NpcGetPlayerLevelResultEvent& event) { results.push_back(event); });

    NpcGetPlayerLevelEvent invalid{};
    invalid.player = entt::entity{999};
    invalid.npc_id = 1;

    const auto player_missing = CreatePlayer(mock_bus.registry);
    NpcGetPlayerLevelEvent missing_attrs{};
    missing_attrs.player = player_missing;
    missing_attrs.npc_id = 1;

    const auto player = CreatePlayer(mock_bus.registry);
    AddAttributes(mock_bus.registry, player, 7, 0);
    int level = 0;
    bool handled = false;
    NpcGetPlayerLevelEvent success{};
    success.player = player;
    success.npc_id = 1;
    success.level = &level;
    success.handled = &handled;

    mock_bus.bus.Publish(invalid);
    mock_bus.bus.Publish(missing_attrs);
    mock_bus.bus.Publish(success);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_FALSE(results[0].success);
    EXPECT_EQ(results[0].error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    EXPECT_FALSE(results[1].success);
    EXPECT_EQ(results[1].error_code, mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    EXPECT_TRUE(results[2].success);
    EXPECT_EQ(results[2].level, 7);
    EXPECT_EQ(level, 7);
    EXPECT_TRUE(handled);
}

TEST(NpcAISystemTest, MultipleNpcUpdateProcessesEach) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc_one = CreateNpc(registry, 0, 0);
    const auto npc_two = CreateNpc(registry, 5, 5);
    const auto player_one = CreatePlayer(registry);
    const auto player_two = CreatePlayer(registry);

    system.OnPlayerInteract(npc_one, player_one);
    system.OnPlayerInteract(npc_two, player_two);
    system.Update(registry, 0.1f);

    const auto& state_one = registry.get<NpcStateComponent>(npc_one);
    const auto& state_two = registry.get<NpcStateComponent>(npc_two);

    EXPECT_EQ(state_one.current_state, NpcState::Interact);
    EXPECT_EQ(state_two.current_state, NpcState::Interact);
    EXPECT_EQ(state_one.interacting_player, player_one);
    EXPECT_EQ(state_two.interacting_player, player_two);
}

TEST(NpcAISystemTest, CleanupRuntimesRemovesDestroyedEntities) {
    entt::registry registry;
    NpcAISystem system;

    const auto npc = CreateNpc(registry);

    system.Update(registry, 0.1f);
    ASSERT_EQ(system.runtime_.size(), 1u);

    registry.destroy(npc);

    system.Update(registry, 0.1f);

    EXPECT_TRUE(system.runtime_.empty());
}
