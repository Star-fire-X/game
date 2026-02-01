#include "client/handlers/combat_handler.h"

#include "combat_generated.h"
#include "item_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::game::handlers {

namespace {

mir2::common::SkillResult ToCommonSkillResult(mir2::proto::SkillResult result) {
    switch (result) {
        case mir2::proto::SkillResult::HIT:
            return mir2::common::SkillResult::HIT;
        case mir2::proto::SkillResult::MISS:
            return mir2::common::SkillResult::MISS;
        case mir2::proto::SkillResult::RESISTED:
            return mir2::common::SkillResult::RESISTED;
        case mir2::proto::SkillResult::BLOCKED:
            return mir2::common::SkillResult::BLOCKED;
        case mir2::proto::SkillResult::IMMUNE:
            return mir2::common::SkillResult::IMMUNE;
        default:
            return mir2::common::SkillResult::HIT;
    }
}

bool TryLockCallbackOwner(const CombatHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

} // namespace

CombatHandler::CombatHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void CombatHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Combat handlers are bound per-instance to capture callbacks.
}

void CombatHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();
    manager.register_handler(mir2::common::MsgId::kAttackRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleAttackResponse(packet);
                                 }
                             });
    manager.register_handler(mir2::common::MsgId::kSkillRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleSkillResponse(packet);
                                 }
                             });
    manager.register_handler(mir2::common::MsgId::kPickupItemRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandlePickupItemResponse(packet);
                                 }
                             });
}

void CombatHandler::HandleAttackResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty attack response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::AttackRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Attack response verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::AttackRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Attack response parse failed");
        }
        return;
    }

    if (callbacks_.on_attack_response) {
        callbacks_.on_attack_response(rsp->code(),
                                      rsp->attacker_id(),
                                      rsp->target_id(),
                                      rsp->damage(),
                                      rsp->target_hp(),
                                      rsp->target_dead());
    }
}

void CombatHandler::HandleSkillResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty skill response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SkillRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid skill response payload");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::SkillRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Skill response parse failed");
        }
        return;
    }

    const auto result = ToCommonSkillResult(rsp->result());
    if (callbacks_.on_skill_result) {
        callbacks_.on_skill_result(rsp->code(),
                                   rsp->skill_id(),
                                   rsp->caster_id(),
                                   rsp->target_id(),
                                   result,
                                   rsp->damage(),
                                   rsp->healing(),
                                   rsp->target_dead());
    }

    if (callbacks_.on_skill_cooldown && rsp->skill_id() != 0) {
        callbacks_.on_skill_cooldown(rsp->skill_id(), rsp->cooldown_ms());
    }
}

void CombatHandler::HandlePickupItemResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty pickup item response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PickupItemRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid pickup item response payload");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::PickupItemRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Pickup item response parse failed");
        }
        return;
    }

    if (callbacks_.on_pickup_item_response) {
        callbacks_.on_pickup_item_response(rsp->code(), rsp->item_id());
    }
}

} // namespace mir2::game::handlers
