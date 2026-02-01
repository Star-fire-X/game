/**
 * @file entity_broadcast_service.cc
 * @brief Entity enter/leave broadcast service implementation.
 */

#include "handlers/movement/entity_broadcast_service.h"

#include <algorithm>
#include <limits>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "ecs/components/character_components.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/npc_component.h"
#include "game/npc/npc_manager.h"
#include "game_generated.h"
#include "network/network_manager.h"

namespace legend2::handlers {

namespace {

int32_t ClampNonNegative(int32_t value) {
  return value < 0 ? 0 : value;
}

uint16_t ClampLevel(int level) {
  if (level < 1) {
    return 1;
  }
  if (level > std::numeric_limits<uint16_t>::max()) {
    return std::numeric_limits<uint16_t>::max();
  }
  return static_cast<uint16_t>(level);
}

}  // namespace

EntityBroadcastService::EntityBroadcastService(mir2::network::NetworkManager& network,
                                               entt::registry& registry)
    : network_(network),
      registry_(registry) {}

void EntityBroadcastService::HandleAOIEvent(mir2::game::map::AOIEventType event_type,
                                            entt::entity watcher,
                                            entt::entity target,
                                            int32_t x,
                                            int32_t y) {
  const auto client_id = ResolveClientId(watcher);
  if (!client_id) {
    return;
  }

  if (event_type == mir2::game::map::AOIEventType::kEnter) {
    auto payload = BuildEntityEnter(target, x, y);
    if (!payload.empty()) {
      SendToClient(*client_id,
                   static_cast<uint16_t>(mir2::common::MsgId::kEntityEnter),
                   payload);
    }
    return;
  }

  if (event_type == mir2::game::map::AOIEventType::kLeave) {
    auto payload = BuildEntityLeave(target);
    if (!payload.empty()) {
      SendToClient(*client_id,
                   static_cast<uint16_t>(mir2::common::MsgId::kEntityLeave),
                   payload);
    }
  }
}

std::optional<uint64_t> EntityBroadcastService::ResolveClientId(entt::entity entity) const {
  const auto* identity = registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
  if (!identity || identity->id == 0) {
    return std::nullopt;
  }
  return identity->id;
}

mir2::proto::EntityType EntityBroadcastService::ResolveEntityType(entt::entity entity) const {
  if (registry_.all_of<mir2::ecs::MonsterIdentityComponent>(entity)) {
    return mir2::proto::EntityType::MONSTER;
  }
  if (registry_.all_of<mir2::ecs::NpcStateComponent>(entity)) {
    return mir2::proto::EntityType::NPC;
  }
  if (registry_.all_of<mir2::ecs::CharacterIdentityComponent>(entity)) {
    return mir2::proto::EntityType::PLAYER;
  }
  return mir2::proto::EntityType::NONE;
}

uint64_t EntityBroadcastService::ResolveEntityId(entt::entity entity,
                                                 mir2::proto::EntityType type) const {
  if (type == mir2::proto::EntityType::PLAYER) {
    if (const auto* identity = registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity)) {
      if (identity->id != 0) {
        return identity->id;
      }
    }
  }
  return static_cast<uint64_t>(entity);
}

std::vector<uint8_t> EntityBroadcastService::BuildEntityEnter(entt::entity entity,
                                                              int32_t x,
                                                              int32_t y) const {
  const auto entity_type = ResolveEntityType(entity);
  if (entity_type == mir2::proto::EntityType::NONE) {
    return {};
  }

  uint64_t entity_id = ResolveEntityId(entity, entity_type);
  uint8_t direction = 0;
  if (const auto* state = registry_.try_get<mir2::ecs::CharacterStateComponent>(entity)) {
    direction = static_cast<uint8_t>(state->direction);
    if (x < 0 || y < 0) {
      x = state->position.x;
      y = state->position.y;
    }
  }

  uint32_t template_id = 0;
  std::string name;
  int32_t hp = 0;
  int32_t max_hp = 0;
  uint16_t level = 1;

  if (entity_type == mir2::proto::EntityType::PLAYER) {
    if (const auto* identity = registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity)) {
      if (!identity->name.empty()) {
        name = identity->name;
      }
      if (identity->id != 0) {
        entity_id = identity->id;
      }
    }
  } else if (entity_type == mir2::proto::EntityType::MONSTER) {
    if (const auto* identity = registry_.try_get<mir2::ecs::MonsterIdentityComponent>(entity)) {
      template_id = identity->monster_template_id;
    }
    if (const auto* attributes =
            registry_.try_get<mir2::ecs::CharacterAttributesComponent>(entity)) {
      hp = ClampNonNegative(attributes->hp);
      max_hp = ClampNonNegative(attributes->max_hp);
      level = ClampLevel(attributes->level);
    }
    if (const auto* identity = registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity)) {
      if (!identity->name.empty()) {
        name = identity->name;
      }
    }
  } else if (entity_type == mir2::proto::EntityType::NPC) {
    auto npc_data = mir2::game::npc::NpcManager::Instance().GetNpcData(entity_id);
    if (npc_data) {
      if (!npc_data->name.empty()) {
        name = npc_data->name;
      }
      if (npc_data->id != 0) {
        template_id = static_cast<uint32_t>(npc_data->id);
      } else {
        template_id = npc_data->template_id;
      }
    } else {
      template_id = static_cast<uint32_t>(entity_id);
    }
  }

  flatbuffers::FlatBufferBuilder builder;
  auto name_offset = name.empty() ? 0 : builder.CreateString(name);
  const auto enter = mir2::proto::CreateEntityEnter(
      builder, entity_id, entity_type, x, y, direction,
      template_id, name_offset, hp, max_hp, level);
  builder.Finish(enter);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> EntityBroadcastService::BuildEntityLeave(entt::entity entity) const {
  const auto entity_type = ResolveEntityType(entity);
  if (entity_type == mir2::proto::EntityType::NONE) {
    return {};
  }

  const uint64_t entity_id = ResolveEntityId(entity, entity_type);
  flatbuffers::FlatBufferBuilder builder;
  const auto leave = mir2::proto::CreateEntityLeave(builder, entity_id, entity_type);
  builder.Finish(leave);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

void EntityBroadcastService::SendToClient(uint64_t client_id,
                                          uint16_t msg_id,
                                          const std::vector<uint8_t>& payload) const {
  if (network_.GetSession(client_id)) {
    network_.Send(client_id, msg_id, payload);
    return;
  }

  const auto sessions = network_.GetAllSessions();
  if (sessions.empty()) {
    return;
  }

  const auto routed_payload = mir2::common::BuildRoutedMessage(client_id, msg_id, payload);
  const uint16_t routed_msg_id =
      static_cast<uint16_t>(mir2::common::InternalMsgId::kRoutedMessage);

  for (const auto& session : sessions) {
    if (!session) {
      continue;
    }
    network_.Send(session->GetSessionId(), routed_msg_id, routed_payload);
  }
}

}  // namespace legend2::handlers
