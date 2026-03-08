#include "logic/services/world_sync_broadcast_service.h"

#include <algorithm>
#include <flatbuffers/flatbuffers.h>

#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

#include "common/enums.h"
#include "common/time_utils.h"
#include "combat_generated.h"
#include "config/config_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/npc_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/combat_events.h"
#include "ecs/events/map_events.h"
#include "ecs/events/skill_events.h"
#include "ecs/systems/combat_system.h"
#include "game/map/map_instance.h"
#include "game/map/scene_manager.h"
#include "game_generated.h"
#include "log/logger.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic {
namespace {

constexpr int64_t kMinStateSyncIntervalMs = 200;

mir2::proto::Profession ToProtoProfession(mir2::common::CharacterClass char_class) {
  switch (char_class) {
    case mir2::common::CharacterClass::WARRIOR:
      return mir2::proto::Profession::WARRIOR;
    case mir2::common::CharacterClass::MAGE:
      return mir2::proto::Profession::WIZARD;
    case mir2::common::CharacterClass::TAOIST:
      return mir2::proto::Profession::TAOIST;
    default:
      return mir2::proto::Profession::NONE;
  }
}

mir2::proto::EntityType ResolveEntityType(const entt::registry& registry,
                                          entt::entity entity) {
  if (!registry.valid(entity)) {
    return mir2::proto::EntityType::NONE;
  }
  if (registry.all_of<mir2::ecs::CharacterIdentityComponent>(entity)) {
    return mir2::proto::EntityType::PLAYER;
  }
  if (registry.all_of<mir2::ecs::MonsterIdentityComponent>(entity)) {
    return mir2::proto::EntityType::MONSTER;
  }
  if (registry.all_of<mir2::ecs::NpcStateComponent>(entity)) {
    return mir2::proto::EntityType::NPC;
  }
  return mir2::proto::EntityType::NONE;
}

uint64_t ResolveNetworkEntityId(const entt::registry& registry,
                                entt::entity entity,
                                mir2::proto::EntityType entity_type) {
  if (entity_type == mir2::proto::EntityType::PLAYER) {
    if (const auto* identity =
            registry.try_get<mir2::ecs::CharacterIdentityComponent>(entity)) {
      if (identity->id != 0) {
        return identity->id;
      }
    }
  }
  if (entity_type == mir2::proto::EntityType::NPC) {
    if (const auto* identity =
            registry.try_get<mir2::ecs::NpcIdentityComponent>(entity)) {
      if (identity->npc_id != 0) {
        return identity->npc_id;
      }
    }
    return 0;
  }
  return static_cast<uint64_t>(entt::to_integral(entity));
}

uint16_t ClampStackCount(size_t stack_count) {
  return static_cast<uint16_t>(
      std::min(stack_count, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
}

uint32_t ClampDurationMs(int64_t duration_ms) {
  if (duration_ms <= 0) {
    return 0;
  }
  return static_cast<uint32_t>(std::min<int64_t>(
      duration_ms, static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
}

std::vector<uint8_t> BuildChangeMapPayload(uint32_t map_id, int32_t x, int32_t y) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message = mir2::proto::CreateChangeMap(builder, map_id, x, y);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTeleportPayload(uint32_t map_id, int32_t x, int32_t y) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message = mir2::proto::CreateTeleport(builder, map_id, x, y);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildBuffAddPayload(uint64_t entity_id,
                                         mir2::proto::EntityType entity_type,
                                         uint32_t buff_id,
                                         uint32_t duration_ms,
                                         uint16_t stack_count) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message = mir2::proto::CreateBuffAdd(
      builder, entity_id, entity_type, buff_id, duration_ms, stack_count);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildBuffRemovePayload(uint64_t entity_id,
                                            mir2::proto::EntityType entity_type,
                                            uint32_t buff_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message = mir2::proto::CreateBuffRemove(
      builder, entity_id, entity_type, buff_id);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildRespawnPayload(uint64_t entity_id,
                                         mir2::proto::EntityType entity_type,
                                         int32_t x,
                                         int32_t y,
                                         int32_t hp,
                                         int32_t mp) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message =
      mir2::proto::CreateRespawn(builder, entity_id, entity_type, x, y, hp, mp);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildEntityEnterPayload(uint64_t entity_id,
                                             mir2::proto::EntityType entity_type,
                                             int32_t x,
                                             int32_t y,
                                             uint8_t direction,
                                             uint32_t template_id,
                                             const std::string& name,
                                             int32_t hp,
                                             int32_t max_hp,
                                             uint16_t level) {
  flatbuffers::FlatBufferBuilder builder;
  const auto name_offset = builder.CreateString(name);
  const auto message = mir2::proto::CreateEntityEnter(
      builder,
      entity_id,
      entity_type,
      x,
      y,
      direction,
      template_id,
      name_offset,
      hp,
      max_hp,
      level);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildEntityLeavePayload(uint64_t entity_id,
                                             mir2::proto::EntityType entity_type) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message = mir2::proto::CreateEntityLeave(builder, entity_id, entity_type);
  builder.Finish(message);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

WorldSyncBroadcastService::WorldSyncBroadcastService(
    ResponseSender& response_sender,
    mir2::ecs::EventBus& event_bus,
    RoleStore& role_store,
    mir2::game::map::SceneManager* scene_manager)
    : WorldSyncBroadcastService(
          response_sender, event_bus, role_store, scene_manager, Config{}) {}

WorldSyncBroadcastService::WorldSyncBroadcastService(ResponseSender& response_sender,
                                                     mir2::ecs::EventBus& event_bus,
                                                     RoleStore& role_store,
                                                     mir2::game::map::SceneManager* scene_manager,
                                                     Config config)
    : response_sender_(response_sender),
      event_bus_(event_bus),
      role_store_(role_store),
      scene_manager_(scene_manager),
      config_(config) {
  subscriptions_.push_back(event_bus.SubscribeScoped<mir2::ecs::events::MapChangeEvent>(
      [this](mir2::ecs::events::MapChangeEvent& event) {
        auto& registry = event_bus_.Registry();
        if (event.entity == entt::null || !registry.valid(event.entity)) {
          SYSLOG_WARN("WorldSyncBroadcastService ignores invalid map event entity");
          return;
        }

        if (event.new_map_id < 0) {
          SYSLOG_WARN("WorldSyncBroadcastService ignores invalid target map_id={}",
                      event.new_map_id);
          return;
        }

        const auto* identity =
            registry.try_get<mir2::ecs::CharacterIdentityComponent>(event.entity);
        if (!identity || identity->id == 0) {
          SYSLOG_WARN("WorldSyncBroadcastService missing character identity for entity={}",
                      static_cast<uint64_t>(entt::to_integral(event.entity)));
          return;
        }

        const auto client_id_opt = role_store_.GetClientIdByRoleId(identity->id);
        if (!client_id_opt.has_value()) {
          SYSLOG_DEBUG("WorldSyncBroadcastService no online client for role_id={}",
                       identity->id);
          return;
        }

        const bool is_cross_map = event.old_map_id != event.new_map_id;
        const uint16_t msg_id = static_cast<uint16_t>(
            is_cross_map ? mir2::common::MsgId::kChangeMap
                         : mir2::common::MsgId::kTeleport);
        auto payload = is_cross_map
                           ? BuildChangeMapPayload(static_cast<uint32_t>(event.new_map_id),
                                                   event.new_x,
                                                   event.new_y)
                           : BuildTeleportPayload(static_cast<uint32_t>(event.new_map_id),
                                                  event.new_x,
                                                  event.new_y);
        response_sender_.Send(*client_id_opt, msg_id, payload);
        BroadcastStateSyncForEntity(event.entity);
      }));

  subscriptions_.push_back(event_bus.SubscribeScoped<mir2::ecs::events::BuffAppliedEvent>(
      [this](mir2::ecs::events::BuffAppliedEvent& event) {
        auto& registry = event_bus_.Registry();
        if (event.target == entt::null || !registry.valid(event.target)) {
          return;
        }

        if (event.skill_id == 0) {
          return;
        }

        const mir2::proto::EntityType entity_type =
            ResolveEntityType(registry, event.target);
        if (entity_type == mir2::proto::EntityType::NONE) {
          return;
        }
        const uint64_t network_entity_id =
            ResolveNetworkEntityId(registry, event.target, entity_type);

        uint16_t stack_count = 1;
        if (const auto* effects =
                registry.try_get<mir2::ecs::EffectListComponent>(event.target)) {
          size_t stacks = 0;
          for (const auto& effect : effects->effects) {
            if (effect.skill_id == event.skill_id) {
              ++stacks;
            }
          }
          stack_count = ClampStackCount(std::max<size_t>(stacks, 1));
        }

        const auto payload = BuildBuffAddPayload(
            network_entity_id,
            entity_type,
            event.skill_id,
            ClampDurationMs(event.duration_ms),
            stack_count);
        const auto recipients = ResolveAoiRecipients(event.target, /*include_self=*/true);
        for (uint64_t client_id : recipients) {
          response_sender_.Send(
              client_id,
              static_cast<uint16_t>(mir2::common::MsgId::kBuffAdd),
              payload);
        }
      }));

  subscriptions_.push_back(event_bus.SubscribeScoped<mir2::ecs::events::BuffRemovedEvent>(
      [this](mir2::ecs::events::BuffRemovedEvent& event) {
        auto& registry = event_bus_.Registry();
        if (event.target == entt::null || !registry.valid(event.target)) {
          return;
        }

        if (event.skill_id == 0) {
          return;
        }

        const mir2::proto::EntityType entity_type =
            ResolveEntityType(registry, event.target);
        if (entity_type == mir2::proto::EntityType::NONE) {
          return;
        }
        const uint64_t network_entity_id =
            ResolveNetworkEntityId(registry, event.target, entity_type);

        const auto payload =
            BuildBuffRemovePayload(network_entity_id, entity_type, event.skill_id);
        const auto recipients = ResolveAoiRecipients(event.target, /*include_self=*/true);
        for (uint64_t client_id : recipients) {
          response_sender_.Send(
              client_id,
              static_cast<uint16_t>(mir2::common::MsgId::kBuffRemove),
              payload);
        }
      }));

  subscriptions_.push_back(event_bus.SubscribeScoped<mir2::ecs::events::EntityDeathEvent>(
      [this](mir2::ecs::events::EntityDeathEvent& event) {
        auto& registry = event_bus_.Registry();
        if (event.entity == entt::null || !registry.valid(event.entity)) {
          return;
        }
        const auto* identity =
            registry.try_get<mir2::ecs::CharacterIdentityComponent>(event.entity);
        if (!identity || identity->id == 0) {
          return;
        }
        const int64_t now_ms = mir2::common::now_ms();
        pending_respawns_[identity->id] = PendingRespawn{
            .entity = event.entity,
            .due_ms = now_ms + std::max<int64_t>(0, config_.respawn_delay_ms)};
      }));

  subscriptions_.push_back(event_bus.SubscribeScoped<mir2::ecs::events::EntityRespawnEvent>(
      [this](mir2::ecs::events::EntityRespawnEvent& event) {
        auto& registry = event_bus_.Registry();
        if (event.entity == entt::null || !registry.valid(event.entity)) {
          return;
        }
        const auto* identity =
            registry.try_get<mir2::ecs::CharacterIdentityComponent>(event.entity);
        const auto* state =
            registry.try_get<mir2::ecs::CharacterStateComponent>(event.entity);
        const auto* attrs =
            registry.try_get<mir2::ecs::CharacterAttributesComponent>(event.entity);
        if (!identity || !state || !attrs || identity->id == 0) {
          return;
        }

        const auto recipients = ResolveAoiRecipients(event.entity, /*include_self=*/true);
        if (recipients.empty()) {
          return;
        }

        const auto entity_type = ResolveEntityType(registry, event.entity);
        const auto respawn_payload = BuildRespawnPayload(
            identity->id,
            entity_type,
            state->position.x,
            state->position.y,
            attrs->hp,
            attrs->mp);
        for (uint64_t client_id : recipients) {
          response_sender_.Send(
              client_id,
              static_cast<uint16_t>(mir2::common::MsgId::kRespawn),
              respawn_payload);
        }

        const auto enter_payload = BuildEntityEnterPayload(
            identity->id,
            entity_type,
            state->position.x,
            state->position.y,
            static_cast<uint8_t>(state->direction),
            /*template_id=*/0,
            identity->name,
            attrs->hp,
            attrs->max_hp,
            static_cast<uint16_t>(std::max(attrs->level, 1)));
        for (uint64_t client_id : recipients) {
          response_sender_.Send(
              client_id,
              static_cast<uint16_t>(mir2::common::MsgId::kEntityEnter),
              enter_payload);
        }

        BroadcastStateSyncForEntity(event.entity);
      }));
}

std::vector<uint64_t> WorldSyncBroadcastService::ResolveAoiRecipients(
    entt::entity entity, bool include_self) const {
  auto& registry = event_bus_.Registry();
  if (entity == entt::null || !registry.valid(entity)) {
    return {};
  }

  std::vector<entt::entity> viewers;
  if (scene_manager_) {
    if (const auto* state =
            registry.try_get<mir2::ecs::CharacterStateComponent>(entity)) {
      if (auto* map = scene_manager_->GetMap(static_cast<int32_t>(state->map_id))) {
        viewers = map->GetEntitiesInViewOf(entity);
      }
    }
  }
  if (include_self) {
    viewers.push_back(entity);
  }

  std::vector<uint64_t> recipients;
  recipients.reserve(viewers.size());
  std::unordered_set<uint64_t> dedup;

  for (entt::entity viewer : viewers) {
    if (viewer == entt::null || !registry.valid(viewer)) {
      continue;
    }
    const auto* identity =
        registry.try_get<mir2::ecs::CharacterIdentityComponent>(viewer);
    if (!identity || identity->id == 0) {
      continue;
    }
    const auto client_id_opt = role_store_.GetClientIdByRoleId(identity->id);
    if (!client_id_opt.has_value()) {
      continue;
    }
    if (dedup.insert(*client_id_opt).second) {
      recipients.push_back(*client_id_opt);
    }
  }

  return recipients;
}

void WorldSyncBroadcastService::BroadcastStateSyncForEntity(entt::entity self_entity) {
  auto& registry = event_bus_.Registry();
  if (self_entity == entt::null || !registry.valid(self_entity)) {
    return;
  }

  const auto* identity =
      registry.try_get<mir2::ecs::CharacterIdentityComponent>(self_entity);
  const auto* state =
      registry.try_get<mir2::ecs::CharacterStateComponent>(self_entity);
  const auto* attrs =
      registry.try_get<mir2::ecs::CharacterAttributesComponent>(self_entity);
  if (!identity || !state || !attrs || identity->id == 0) {
    return;
  }

  const auto client_id_opt = role_store_.GetClientIdByRoleId(identity->id);
  if (!client_id_opt.has_value()) {
    return;
  }

  std::vector<entt::entity> nearby_entities;
  if (scene_manager_) {
    if (auto* map = scene_manager_->GetMap(static_cast<int32_t>(state->map_id))) {
      nearby_entities = map->GetEntitiesInViewOf(self_entity);
    }
  }
  if (nearby_entities.empty()) {
    auto view = registry.view<mir2::ecs::CharacterStateComponent>();
    for (entt::entity entity : view) {
      if (entity == self_entity) {
        continue;
      }
      const auto& other_state = view.get<mir2::ecs::CharacterStateComponent>(entity);
      if (other_state.map_id == state->map_id) {
        nearby_entities.push_back(entity);
      }
    }
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto name_offset = builder.CreateString(identity->name);
  const auto player_offset = mir2::proto::CreatePlayerInfo(
      builder,
      identity->id,
      name_offset,
      ToProtoProfession(identity->char_class),
      static_cast<uint16_t>(std::max(attrs->level, 1)),
      attrs->hp,
      attrs->max_hp,
      attrs->mp,
      attrs->max_mp,
      state->map_id,
      state->position.x,
      state->position.y,
      static_cast<uint64_t>(std::max(attrs->gold, 0)));

  std::vector<flatbuffers::Offset<mir2::proto::EntitySnapshot>> snapshots;
  snapshots.reserve(nearby_entities.size());
  for (entt::entity entity : nearby_entities) {
    if (entity == self_entity || entity == entt::null || !registry.valid(entity)) {
      continue;
    }
    const auto* entity_state =
        registry.try_get<mir2::ecs::CharacterStateComponent>(entity);
    if (!entity_state) {
      continue;
    }
    const auto entity_type = ResolveEntityType(registry, entity);
    if (entity_type == mir2::proto::EntityType::NONE) {
      continue;
    }
    const uint64_t network_entity_id =
        ResolveNetworkEntityId(registry, entity, entity_type);
    if (network_entity_id == 0) {
      continue;
    }
    const auto* entity_attrs =
        registry.try_get<mir2::ecs::CharacterAttributesComponent>(entity);
    const int hp = entity_attrs ? entity_attrs->hp : 0;
    const int max_hp = entity_attrs ? entity_attrs->max_hp : 0;
    const int mp = entity_attrs ? entity_attrs->mp : 0;
    const int max_mp = entity_attrs ? entity_attrs->max_mp : 0;
    snapshots.push_back(mir2::proto::CreateEntitySnapshot(
        builder,
        network_entity_id,
        entity_type,
        entity_state->position.x,
        entity_state->position.y,
        static_cast<uint8_t>(entity_state->direction),
        hp,
        max_hp,
        mp,
        max_mp));
  }

  const auto entities_offset = builder.CreateVector(snapshots);
  const auto sync_offset =
      mir2::proto::CreateStateSync(builder, player_offset, entities_offset);
  builder.Finish(sync_offset);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  response_sender_.Send(*client_id_opt,
                        static_cast<uint16_t>(mir2::common::MsgId::kStateSync),
                        payload);
}

bool WorldSyncBroadcastService::RequestImmediateStateSyncForRole(uint64_t role_id) {
  if (role_id == 0) {
    return false;
  }

  auto& registry = event_bus_.Registry();
  auto view = registry.view<mir2::ecs::CharacterIdentityComponent>();
  for (entt::entity entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (identity.id != role_id) {
      continue;
    }
    BroadcastStateSyncForEntity(entity);
    return true;
  }
  return false;
}

void WorldSyncBroadcastService::HandleAoiEvent(
    entt::entity watcher,
    entt::entity target,
    mir2::game::map::AOIEventType event_type,
    int32_t x,
    int32_t y) {
  auto& registry = event_bus_.Registry();
  if (watcher == entt::null || target == entt::null ||
      !registry.valid(watcher) || !registry.valid(target)) {
    return;
  }

  const auto* watcher_identity =
      registry.try_get<mir2::ecs::CharacterIdentityComponent>(watcher);
  if (!watcher_identity || watcher_identity->id == 0) {
    return;
  }

  const auto client_id_opt = role_store_.GetClientIdByRoleId(watcher_identity->id);
  if (!client_id_opt.has_value()) {
    return;
  }

  const auto entity_type = ResolveEntityType(registry, target);
  if (entity_type != mir2::proto::EntityType::NPC &&
      entity_type != mir2::proto::EntityType::MONSTER) {
    return;
  }

  const auto* state =
      registry.try_get<mir2::ecs::CharacterStateComponent>(target);
  if (!state) {
    return;
  }

  uint64_t network_entity_id = 0;
  uint32_t template_id = 0;
  if (entity_type == mir2::proto::EntityType::NPC) {
    const auto* npc_identity =
        registry.try_get<mir2::ecs::NpcIdentityComponent>(target);
    if (!npc_identity || npc_identity->npc_id == 0) {
      return;
    }
    network_entity_id = npc_identity->npc_id;
    template_id = npc_identity->template_id;
  } else {
    const auto* monster_identity =
        registry.try_get<mir2::ecs::MonsterIdentityComponent>(target);
    if (!monster_identity || monster_identity->monster_template_id == 0) {
      return;
    }
    network_entity_id = static_cast<uint64_t>(entt::to_integral(target));
    template_id = monster_identity->monster_template_id;
  }

  if (event_type == mir2::game::map::AOIEventType::kEnter) {
    const auto* attrs =
        registry.try_get<mir2::ecs::CharacterAttributesComponent>(target);
    const auto payload = BuildEntityEnterPayload(
        network_entity_id,
        entity_type,
        x,
        y,
        static_cast<uint8_t>(state->direction),
        template_id,
        "",
        attrs ? attrs->hp : 0,
        attrs ? attrs->max_hp : 0,
        static_cast<uint16_t>(std::max(attrs ? attrs->level : 1, 1)));
    response_sender_.Send(
        *client_id_opt,
        static_cast<uint16_t>(mir2::common::MsgId::kEntityEnter),
        payload);
    return;
  }

  if (event_type == mir2::game::map::AOIEventType::kLeave) {
    const auto payload = BuildEntityLeavePayload(network_entity_id, entity_type);
    response_sender_.Send(
        *client_id_opt,
        static_cast<uint16_t>(mir2::common::MsgId::kEntityLeave),
        payload);
  }
}

void WorldSyncBroadcastService::ProcessPendingRespawns(int64_t now_ms) {
  if (pending_respawns_.empty()) {
    return;
  }

  auto& registry = event_bus_.Registry();
  for (auto it = pending_respawns_.begin(); it != pending_respawns_.end();) {
    if (it->second.due_ms > now_ms) {
      ++it;
      continue;
    }

    const entt::entity entity = it->second.entity;
    if (entity == entt::null || !registry.valid(entity)) {
      it = pending_respawns_.erase(it);
      continue;
    }

    auto* state = registry.try_get<mir2::ecs::CharacterStateComponent>(entity);
    auto* identity = registry.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (!state || !identity || identity->id == 0) {
      it = pending_respawns_.erase(it);
      continue;
    }

    const auto& combat_config = config::ConfigManager::Instance().GetCombatConfig();
    uint32_t target_map_id = combat_config.default_respawn_map_id != 0
        ? combat_config.default_respawn_map_id
        : state->map_id;
    mir2::common::Position respawn_pos = combat_config.default_respawn_position;
    if (respawn_pos.x < 0 || respawn_pos.y < 0) {
      respawn_pos = state->position;
    }

    const uint32_t old_map_id = state->map_id;
    bool map_changed = old_map_id != target_map_id;
    if (scene_manager_) {
      bool moved = false;
      if (map_changed) {
        moved = scene_manager_->MoveEntityToMap(
            static_cast<int32_t>(old_map_id),
            static_cast<int32_t>(target_map_id),
            entity,
            respawn_pos.x,
            respawn_pos.y);
      } else {
        moved = scene_manager_->UpdateEntityPosition(
            static_cast<int32_t>(target_map_id), entity, respawn_pos.x, respawn_pos.y);
      }
      if (!moved) {
        SYSLOG_WARN(
            "WorldSyncBroadcastService failed to move respawn entity role_id={} map={} pos=({}, {})",
            identity->id,
            target_map_id,
            respawn_pos.x,
            respawn_pos.y);
        target_map_id = state->map_id;
        respawn_pos = state->position;
        map_changed = false;
      }
    }

    state->map_id = target_map_id;
    mir2::ecs::CombatSystem::Respawn(registry,
                                     entity,
                                     respawn_pos,
                                     combat_config.default_respawn_hp_percent,
                                     combat_config.default_respawn_mp_percent,
                                     &event_bus_);
    if (map_changed) {
      mir2::ecs::events::MapChangeEvent map_event{
          entity,
          static_cast<int32_t>(old_map_id),
          static_cast<int32_t>(target_map_id),
          respawn_pos.x,
          respawn_pos.y};
      event_bus_.Publish(map_event);
    }

    it = pending_respawns_.erase(it);
  }
}

void WorldSyncBroadcastService::Tick(int64_t now_ms) {
  ProcessPendingRespawns(now_ms);

  const int64_t interval_ms =
      std::max<int64_t>(kMinStateSyncIntervalMs, config_.state_sync_interval_ms);
  if (now_ms < next_state_sync_ms_) {
    return;
  }
  next_state_sync_ms_ = now_ms + interval_ms;

  auto view = event_bus_.Registry().view<mir2::ecs::CharacterIdentityComponent,
                                         mir2::ecs::CharacterStateComponent,
                                         mir2::ecs::CharacterAttributesComponent>();
  for (entt::entity entity : view) {
    BroadcastStateSyncForEntity(entity);
  }
}

}  // namespace mir2::logic
