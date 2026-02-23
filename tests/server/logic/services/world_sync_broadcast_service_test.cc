#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <string>

#include "common/enums.h"
#include "common/time_utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/combat_events.h"
#include "ecs/events/map_events.h"
#include "ecs/events/skill_events.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "logic/mock_response_sender.h"
#include "logic/services/session_role_store.h"
#include "logic/services/world_sync_broadcast_service.h"

namespace mir2::logic::test {
namespace {

class WorldSyncBroadcastServiceTest : public ::testing::Test {
 protected:
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

  entt::registry registry_;
  mir2::ecs::EventBus event_bus_{registry_};
  MockResponseSender response_sender_;
  RoleStore role_store_;
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

}  // namespace
}  // namespace mir2::logic::test
