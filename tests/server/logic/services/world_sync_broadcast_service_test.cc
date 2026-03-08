#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <string>

#include "common/enums.h"
#include "common/time_utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/npc_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/combat_events.h"
#include "ecs/events/map_events.h"
#include "ecs/events/skill_events.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "game/map/scene_manager.h"
#include "logic/mock_response_sender.h"
#include "logic/services/session_role_store.h"
#include "logic/services/world_sync_broadcast_service.h"

namespace mir2::logic::test {
namespace {

class WorldSyncBroadcastServiceTest : public ::testing::Test {
 protected:
  game::map::SceneManager::MapConfig BuildMapConfig(int32_t map_id) {
    game::map::SceneManager::MapConfig config;
    config.map_id = map_id;
    config.width = 256;
    config.height = 256;
    config.load_walkability = false;
    return config;
  }

  entt::entity AddPlayerEntity(uint64_t role_id) {
    const auto entity = registry_.create();
    auto& identity = registry_.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = role_id;
    identity.name = "player_" + std::to_string(role_id);
    auto& state = registry_.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = 1;
    state.position = {100, 100};
    auto& attrs = registry_.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attrs.level = 10;
    attrs.hp = 120;
    attrs.max_hp = 150;
    attrs.mp = 60;
    attrs.max_mp = 80;
    attrs.gold = 999;
    return entity;
  }

  entt::entity AddNpcEntity(uint64_t npc_id,
                            uint32_t map_id,
                            int32_t x,
                            int32_t y,
                            uint32_t template_id = 2001) {
    const auto entity = registry_.create();
    auto& identity = registry_.emplace<mir2::ecs::NpcIdentityComponent>(entity);
    identity.npc_id = npc_id;
    identity.template_id = template_id;
    identity.map_id = map_id;
    auto& state = registry_.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = map_id;
    state.position = {x, y};
    auto& attrs = registry_.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attrs.level = 1;
    attrs.hp = 1;
    attrs.max_hp = 1;
    attrs.mp = 0;
    attrs.max_mp = 0;
    registry_.emplace<mir2::ecs::NpcStateComponent>(entity);
    return entity;
  }

  entt::entity AddMonsterEntity(uint32_t monster_template_id,
                                uint32_t spawn_point_id,
                                uint32_t map_id,
                                int32_t x,
                                int32_t y) {
    const auto entity = registry_.create();
    auto& identity = registry_.emplace<mir2::ecs::MonsterIdentityComponent>(entity);
    identity.monster_template_id = monster_template_id;
    identity.spawn_point_id = spawn_point_id;
    auto& state = registry_.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = map_id;
    state.position = {x, y};
    auto& attrs = registry_.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attrs.level = 1;
    attrs.hp = 30;
    attrs.max_hp = 30;
    attrs.mp = 0;
    attrs.max_mp = 0;
    return entity;
  }

  void AddEntityToMap(entt::entity entity, int32_t map_id, int32_t x, int32_t y) {
    ASSERT_NE(scene_manager_.GetOrCreateMap(BuildMapConfig(map_id)), nullptr);
    ASSERT_TRUE(scene_manager_.AddEntityToMap(map_id, entity, x, y));
  }

  entt::registry registry_;
  mir2::ecs::EventBus event_bus_{registry_};
  MockResponseSender response_sender_;
  RoleStore role_store_;
  game::map::SceneManager scene_manager_;
};

TEST_F(WorldSyncBroadcastServiceTest, CrossMapEventSendsChangeMap) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_);
  const auto entity = AddPlayerEntity(/*role_id=*/1001);
  role_store_.BindClientRole(/*client_id=*/9001, /*player_id=*/1001);

  mir2::ecs::events::MapChangeEvent event{
      entity, /*old_map_id=*/1, /*new_map_id=*/2, /*new_x=*/88, /*new_y=*/99};
  event_bus_.Publish(event);

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kChangeMap);
      });
  ASSERT_NE(it, responses.end());
  EXPECT_EQ(it->client_id, 9001u);

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChangeMap>(nullptr));
  const auto* payload =
      flatbuffers::GetRoot<mir2::proto::ChangeMap>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->map_id(), 2u);
  EXPECT_EQ(payload->x(), 88);
  EXPECT_EQ(payload->y(), 99);
}

TEST_F(WorldSyncBroadcastServiceTest, SameMapEventSendsTeleport) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_);
  const auto entity = AddPlayerEntity(/*role_id=*/1002);
  role_store_.BindClientRole(/*client_id=*/9002, /*player_id=*/1002);

  mir2::ecs::events::MapChangeEvent event{
      entity, /*old_map_id=*/3, /*new_map_id=*/3, /*new_x=*/45, /*new_y=*/67};
  event_bus_.Publish(event);

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kTeleport);
      });
  ASSERT_NE(it, responses.end());
  EXPECT_EQ(it->client_id, 9002u);

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::Teleport>(nullptr));
  const auto* payload =
      flatbuffers::GetRoot<mir2::proto::Teleport>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->map_id(), 3u);
  EXPECT_EQ(payload->x(), 45);
  EXPECT_EQ(payload->y(), 67);
}

TEST_F(WorldSyncBroadcastServiceTest, MissingRoleBindingIsSkipped) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_);
  const auto entity = AddPlayerEntity(/*role_id=*/1003);

  mir2::ecs::events::MapChangeEvent event{
      entity, /*old_map_id=*/1, /*new_map_id=*/2, /*new_x=*/10, /*new_y=*/20};
  event_bus_.Publish(event);

  EXPECT_TRUE(response_sender_.GetCapturedResponses().empty());
}

TEST_F(WorldSyncBroadcastServiceTest, BuffAppliedEventSendsBuffAdd) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_);
  const auto entity = AddPlayerEntity(/*role_id=*/2001);
  role_store_.BindClientRole(/*client_id=*/9201, /*player_id=*/2001);

  auto& effects = registry_.emplace<mir2::ecs::EffectListComponent>(entity);
  mir2::ecs::ActiveEffect effect;
  effect.skill_id = 7007;
  effects.effects.push_back(effect);

  mir2::ecs::events::BuffAppliedEvent event;
  event.target = entity;
  event.source = entity;
  event.category = mir2::ecs::EffectCategory::STAT_BUFF;
  event.skill_id = 7007;
  event.duration_ms = 5000;
  event_bus_.Publish(event);

  const auto responses = response_sender_.GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kBuffAdd));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::BuffAdd>(nullptr));
  const auto* payload =
      flatbuffers::GetRoot<mir2::proto::BuffAdd>(responses[0].payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), 2001u);
  EXPECT_EQ(payload->buff_id(), 7007u);
  EXPECT_EQ(payload->stack_count(), 1u);
}

TEST_F(WorldSyncBroadcastServiceTest, BuffRemovedEventSendsBuffRemove) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_);
  const auto entity = AddPlayerEntity(/*role_id=*/2002);
  role_store_.BindClientRole(/*client_id=*/9202, /*player_id=*/2002);

  mir2::ecs::events::BuffRemovedEvent event;
  event.target = entity;
  event.category = mir2::ecs::EffectCategory::STAT_BUFF;
  event.skill_id = 7008;
  event.expired = true;
  event_bus_.Publish(event);

  const auto responses = response_sender_.GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kBuffRemove));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::BuffRemove>(nullptr));
  const auto* payload =
      flatbuffers::GetRoot<mir2::proto::BuffRemove>(responses[0].payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), 2002u);
  EXPECT_EQ(payload->buff_id(), 7008u);
}

TEST_F(WorldSyncBroadcastServiceTest, DeathEventTriggersRespawnOnTick) {
  WorldSyncBroadcastService::Config config;
  config.respawn_delay_ms = 1;
  config.state_sync_interval_ms = 60 * 1000;
  WorldSyncBroadcastService service(
      response_sender_, event_bus_, role_store_, /*scene_manager=*/nullptr, config);
  const auto entity = AddPlayerEntity(/*role_id=*/2003);
  role_store_.BindClientRole(/*client_id=*/9203, /*player_id=*/2003);

  auto& attrs = registry_.get<mir2::ecs::CharacterAttributesComponent>(entity);
  attrs.hp = 0;
  attrs.max_hp = 150;
  attrs.mp = 0;
  attrs.max_mp = 80;

  mir2::ecs::events::EntityDeathEvent death_event;
  death_event.entity = entity;
  death_event.map_id = 1;
  death_event.position = {100, 100};
  event_bus_.Publish(death_event);

  service.Tick(mir2::common::now_ms() + 10);

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kRespawn);
      });
  ASSERT_NE(it, responses.end());

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::Respawn>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::Respawn>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), 2003u);
  EXPECT_GT(payload->hp(), 0);
}

TEST_F(WorldSyncBroadcastServiceTest, TickSendsStateSync) {
  WorldSyncBroadcastService::Config config;
  config.state_sync_interval_ms = 1;
  WorldSyncBroadcastService service(
      response_sender_, event_bus_, role_store_, /*scene_manager=*/nullptr, config);
  AddPlayerEntity(/*role_id=*/2004);
  role_store_.BindClientRole(/*client_id=*/9204, /*player_id=*/2004);

  service.Tick(mir2::common::now_ms());

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, responses.end());

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
}

TEST_F(WorldSyncBroadcastServiceTest, RequestImmediateStateSyncForRoleSendsImmediately) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_);
  AddPlayerEntity(/*role_id=*/2010);
  role_store_.BindClientRole(/*client_id=*/9210, /*player_id=*/2010);

  EXPECT_TRUE(service.RequestImmediateStateSyncForRole(/*role_id=*/2010));

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, responses.end());
  EXPECT_EQ(it->client_id, 9210u);
}

TEST_F(WorldSyncBroadcastServiceTest, StateSyncUsesNpcBusinessIdForNpcEntities) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_, &scene_manager_);
  const auto player = AddPlayerEntity(/*role_id=*/3001);
  const auto npc = AddNpcEntity(/*npc_id=*/7001, /*map_id=*/1, /*x=*/101, /*y=*/100);
  role_store_.BindClientRole(/*client_id=*/9301, /*player_id=*/3001);
  AddEntityToMap(player, /*map_id=*/1, /*x=*/100, /*y=*/100);
  AddEntityToMap(npc, /*map_id=*/1, /*x=*/101, /*y=*/100);

  ASSERT_TRUE(service.RequestImmediateStateSyncForRole(/*role_id=*/3001));

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, responses.end());

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::StateSync>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->entities(), nullptr);
  ASSERT_EQ(payload->entities()->size(), 1u);
  EXPECT_EQ(payload->entities()->Get(0)->entity_type(), mir2::proto::EntityType::NPC);
  EXPECT_EQ(payload->entities()->Get(0)->entity_id(), 7001u);
}

TEST_F(WorldSyncBroadcastServiceTest, StateSyncIncludesMonsterEntities) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_, &scene_manager_);
  const auto player = AddPlayerEntity(/*role_id=*/3010);
  const auto monster =
      AddMonsterEntity(/*monster_template_id=*/9001, /*spawn_point_id=*/10, /*map_id=*/1, 101, 100);
  role_store_.BindClientRole(/*client_id=*/9310, /*player_id=*/3010);
  AddEntityToMap(player, /*map_id=*/1, /*x=*/100, /*y=*/100);
  AddEntityToMap(monster, /*map_id=*/1, /*x=*/101, /*y=*/100);

  ASSERT_TRUE(service.RequestImmediateStateSyncForRole(/*role_id=*/3010));

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, responses.end());

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::StateSync>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->entities(), nullptr);
  ASSERT_EQ(payload->entities()->size(), 1u);
  EXPECT_EQ(payload->entities()->Get(0)->entity_type(), mir2::proto::EntityType::MONSTER);
  EXPECT_EQ(payload->entities()->Get(0)->entity_id(),
            static_cast<uint64_t>(entt::to_integral(monster)));
}

TEST_F(WorldSyncBroadcastServiceTest, AoiEnterSendsEntityEnterForNpc) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_, &scene_manager_);
  const auto player = AddPlayerEntity(/*role_id=*/3002);
  const auto npc = AddNpcEntity(/*npc_id=*/7002, /*map_id=*/1, /*x=*/140, /*y=*/100);
  role_store_.BindClientRole(/*client_id=*/9302, /*player_id=*/3002);
  AddEntityToMap(player, /*map_id=*/1, /*x=*/100, /*y=*/100);
  AddEntityToMap(npc, /*map_id=*/1, /*x=*/140, /*y=*/100);
  response_sender_.Clear();

  service.HandleAoiEvent(player, npc, mir2::game::map::AOIEventType::kEnter, 120, 100);

  const auto responses = response_sender_.GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, 9302u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kEntityEnter));
}

TEST_F(WorldSyncBroadcastServiceTest, AoiEnterSendsEntityEnterForMonster) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_, &scene_manager_);
  const auto player = AddPlayerEntity(/*role_id=*/3011);
  const auto monster =
      AddMonsterEntity(/*monster_template_id=*/9002, /*spawn_point_id=*/11, /*map_id=*/1, 140, 100);
  role_store_.BindClientRole(/*client_id=*/9311, /*player_id=*/3011);
  AddEntityToMap(player, /*map_id=*/1, /*x=*/100, /*y=*/100);
  AddEntityToMap(monster, /*map_id=*/1, /*x=*/140, /*y=*/100);
  response_sender_.Clear();

  service.HandleAoiEvent(player, monster, mir2::game::map::AOIEventType::kEnter, 120, 100);

  const auto responses = response_sender_.GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, 9311u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kEntityEnter));
}

TEST_F(WorldSyncBroadcastServiceTest, AoiLeaveSendsEntityLeaveForNpc) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_, &scene_manager_);
  const auto player = AddPlayerEntity(/*role_id=*/3003);
  const auto npc = AddNpcEntity(/*npc_id=*/7003, /*map_id=*/1, /*x=*/110, /*y=*/100);
  role_store_.BindClientRole(/*client_id=*/9303, /*player_id=*/3003);
  AddEntityToMap(player, /*map_id=*/1, /*x=*/100, /*y=*/100);
  AddEntityToMap(npc, /*map_id=*/1, /*x=*/110, /*y=*/100);
  response_sender_.Clear();

  service.HandleAoiEvent(player, npc, mir2::game::map::AOIEventType::kLeave, 140, 100);

  const auto responses = response_sender_.GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, 9303u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kEntityLeave));
}

TEST_F(WorldSyncBroadcastServiceTest, DifferentMapNpcDoesNotAppearInStateSync) {
  WorldSyncBroadcastService service(response_sender_, event_bus_, role_store_, &scene_manager_);
  const auto player = AddPlayerEntity(/*role_id=*/3004);
  const auto npc = AddNpcEntity(/*npc_id=*/7004, /*map_id=*/2, /*x=*/100, /*y=*/100);
  role_store_.BindClientRole(/*client_id=*/9304, /*player_id=*/3004);
  AddEntityToMap(player, /*map_id=*/1, /*x=*/100, /*y=*/100);
  AddEntityToMap(npc, /*map_id=*/2, /*x=*/100, /*y=*/100);

  ASSERT_TRUE(service.RequestImmediateStateSyncForRole(/*role_id=*/3004));

  const auto responses = response_sender_.GetCapturedResponses();
  const auto it = std::find_if(
      responses.begin(), responses.end(), [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, responses.end());

  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::StateSync>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->entities(), nullptr);
  EXPECT_EQ(payload->entities()->size(), 0u);
}

}  // namespace
}  // namespace mir2::logic::test
