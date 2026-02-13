#include "logic/handlers/character/bonus_point_handler.h"

#include "ecs/components/bonus_point_component.h"
#include "ecs/systems/bonus_point_system.h"

#include <flatbuffers/flatbuffers.h>

#include "game_generated.h"

namespace mir2::logic {

Task<void> HandleBonusPointReq(const HandlerContext& context,
                               const uint8_t* payload,
                               size_t payload_size) {
  if (!context.world || !context.registry || context.entity == entt::null) {
    co_return;
  }

  auto& registry = *context.registry;
  const auto entity = context.entity;

  std::string attribute_name;
  uint8_t action = 0;
  if (payload && payload_size > 0) {
    flatbuffers::Verifier verifier(payload, payload_size);
    if (verifier.VerifyBuffer<mir2::proto::BonusPointReq>(nullptr)) {
      if (const auto* req = flatbuffers::GetRoot<mir2::proto::BonusPointReq>(payload)) {
        if (req->attribute_name()) {
          attribute_name = req->attribute_name()->str();
        }
        action = req->action();
      }
    }
  }

  ecs::BonusPointSystem bonus_system(registry);

  bonus_system.RecalcAvailablePoints(entity);

  if (action == 1) {
    bonus_system.ResetPoints(entity);
  } else {
    (void)bonus_system.AllocatePoint(entity, attribute_name);
  }

  auto* bonus = registry.try_get<ecs::BonusPointComponent>(entity);
  if (bonus) {
    (void)bonus->remaining();
  }
  co_return;
}

}  // namespace mir2::logic
