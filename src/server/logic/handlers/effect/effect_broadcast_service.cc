#include "logic/handlers/effect/effect_broadcast_service.h"

#include <flatbuffers/flatbuffers.h>

#include "combat_generated.h"
#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "ecs/components/character_components.h"
#include "game/map/aoi_manager.h"
#include "logic/services/session_role_store.h"
#include "log/logger.h"
#include "network/network_manager.h"

#include <entt/entt.hpp>

namespace mir2::logic {

EffectBroadcastService::EffectBroadcastService(mir2::network::NetworkManager& network,
                                               mir2::game::map::AOIManager& aoi_manager,
                                               entt::registry& registry,
                                               mir2::logic::RoleStore& role_store)
    : network_(network),
      aoi_manager_(aoi_manager),
      registry_(registry),
      role_store_(role_store) {}

void EffectBroadcastService::BroadcastSkillEffect(uint64_t caster_id, uint64_t target_id,
                                                  uint32_t skill_id, uint8_t effect_type,
                                                  const std::string& effect_id,
                                                  const std::string& sound_id,
                                                  int x, int y, uint32_t duration_ms) {
  auto viewers = aoi_manager_.GetEntitiesInView(x, y);
  if (viewers.empty()) {
    SYSLOG_DEBUG("EffectBroadcastService no viewers at position ({}, {})", x, y);
    return;
  }

  // Validate effect_type
  if (effect_type > static_cast<uint8_t>(mir2::proto::EffectType::MAX)) {
    SYSLOG_WARN("EffectBroadcastService invalid effect_type={} at ({}, {})", effect_type, x, y);
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  auto effect_id_str = builder.CreateString(effect_id);
  auto sound_id_str = builder.CreateString(sound_id);
  auto msg = mir2::proto::CreateSkillEffect(
      builder,
      caster_id,
      target_id,
      skill_id,
      static_cast<mir2::proto::EffectType>(effect_type),
      effect_id_str,
      sound_id_str,
      x,
      y,
      duration_ms);
  builder.Finish(msg);

  const uint8_t* data = builder.GetBufferPointer();
  const std::vector<uint8_t> payload(data, data + builder.GetSize());

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSkillEffect);

  size_t sent_count = 0;
  size_t skipped_count = 0;

  for (uint64_t entity_id : viewers) {
    const entt::entity entity = static_cast<entt::entity>(entity_id);
    if (!registry_.valid(entity)) {
      continue;
    }

    const auto* identity = registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (!identity || identity->id == 0) {
      continue;
    }

    // Bug Fix 1.1: identity->id is the role/character ID, not client session ID
    // Use RoleStore to find the client_id associated with this role
    const uint32_t role_id = identity->id;
    const auto client_id_opt = role_store_.GetClientIdByRoleId(role_id);

    if (!client_id_opt.has_value()) {
      // Role is not currently connected to any client on this server
      // This could mean the character is offline or on another server
      ++skipped_count;
      continue;
    }

    const uint64_t client_id = *client_id_opt;

    // Bug Fix 1.2: Only send to clients that have active sessions on THIS server
    // Do NOT broadcast routed messages to all local clients
    if (network_.GetSession(client_id)) {
      network_.Send(client_id, msg_id, payload);
      ++sent_count;
    } else {
      // Client session not found on this server
      // For cross-server scenarios, the client might be on another logic server
      // In this case, we skip sending (proper cross-server routing should be
      // handled by a dedicated service/gateway mechanism, not here)
      ++skipped_count;
    }
  }

  SYSLOG_DEBUG(
      "EffectBroadcastService sent skill_effect skill_id={} viewers={} sent={} skipped={}",
      skill_id, viewers.size(), sent_count, skipped_count);
}

}  // namespace mir2::logic
