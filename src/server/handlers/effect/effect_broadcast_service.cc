#include "handlers/effect/effect_broadcast_service.h"

#include <flatbuffers/flatbuffers.h>

#include "combat_generated.h"
#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "ecs/components/character_components.h"
#include "game/map/aoi_manager.h"
#include "network/network_manager.h"

#include <entt/entt.hpp>

namespace legend2::handlers {

EffectBroadcastService::EffectBroadcastService(mir2::network::NetworkManager& network,
                                               mir2::game::map::AOIManager& aoi_manager,
                                               entt::registry& registry)
    : network_(network),
      aoi_manager_(aoi_manager),
      registry_(registry) {}

void EffectBroadcastService::BroadcastSkillEffect(uint64_t caster_id, uint64_t target_id,
                                                  uint32_t skill_id, uint8_t effect_type,
                                                  const std::string& effect_id,
                                                  const std::string& sound_id,
                                                  int x, int y, uint32_t duration_ms) {
    auto viewers = aoi_manager_.GetEntitiesInView(x, y);
    if (viewers.empty()) {
        return;
    }

    flatbuffers::FlatBufferBuilder builder;
    auto effect_id_str = builder.CreateString(effect_id);
    auto sound_id_str = builder.CreateString(sound_id);
    auto msg = mir2::proto::CreateSkillEffect(
        builder, caster_id, target_id, skill_id,
        static_cast<mir2::proto::EffectType>(effect_type),
        effect_id_str, sound_id_str, x, y, duration_ms);
    builder.Finish(msg);

    const uint8_t* data = builder.GetBufferPointer();
    const std::vector<uint8_t> payload(data, data + builder.GetSize());

    const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSkillEffect);
    const uint16_t routed_msg_id = static_cast<uint16_t>(mir2::common::InternalMsgId::kRoutedMessage);
    const auto sessions = network_.GetAllSessions();

    for (uint64_t entity_id : viewers) {
        const entt::entity entity = static_cast<entt::entity>(entity_id);
        if (!registry_.valid(entity)) {
            continue;
        }
        const auto* identity = registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
        if (!identity || identity->id == 0) {
            continue;
        }
        const uint64_t client_id = identity->id;

        if (network_.GetSession(client_id)) {
            network_.Send(client_id, msg_id, payload);
            continue;
        }

        if (sessions.empty()) {
            continue;
        }

        const auto routed_payload = mir2::common::BuildRoutedMessage(client_id, msg_id, payload);
        for (const auto& session : sessions) {
            if (!session) {
                continue;
            }
            network_.Send(session->GetSessionId(), routed_msg_id, routed_payload);
        }
    }
}

}  // namespace legend2::handlers
