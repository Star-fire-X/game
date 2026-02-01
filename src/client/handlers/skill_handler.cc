#include "client/handlers/skill_handler.h"

#include "combat_generated.h"

#include <charconv>
#include <flatbuffers/flatbuffers.h>
#include <string>

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

uint32_t ParseEffectId(const mir2::proto::SkillEffect* effect) {
    if (!effect) {
        return 0;
    }
    const auto* effect_id = effect->effect_id();
    if (!effect_id) {
        return 0;
    }

    const std::string effect_id_str = effect_id->str();
    if (effect_id_str.empty()) {
        return 0;
    }

    uint32_t parsed = 0;
    const char* start = effect_id_str.data();
    const char* end = start + effect_id_str.size();
    auto [ptr, ec] = std::from_chars(start, end, parsed);
    if (ec != std::errc() || ptr != end) {
        return 0;
    }
    return parsed;
}

} // namespace

SkillHandler::SkillHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void SkillHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();
    manager.register_handler(mir2::common::MsgId::kSkillRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleSkillResponse(packet);
                                 }
                             });
    manager.register_handler(mir2::common::MsgId::kSkillEffect,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleSkillEffect(packet);
                                 }
                             });
}

void SkillHandler::HandleSkillResponse(const NetworkPacket& packet) {
    if (callbacks_.owner.has_value() && callbacks_.owner->expired()) {
        return;
    }

    if (packet.payload.empty()) {
        invoke_callback([this]() {
            if (callbacks_.on_parse_error) {
                callbacks_.on_parse_error("Empty skill response payload");
            }
        });
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SkillRsp>(nullptr)) {
        invoke_callback([this]() {
            if (callbacks_.on_parse_error) {
                callbacks_.on_parse_error("Invalid skill response payload");
            }
        });
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::SkillRsp>(packet.payload.data());
    if (!rsp) {
        invoke_callback([this]() {
            if (callbacks_.on_parse_error) {
                callbacks_.on_parse_error("Skill response parse failed");
            }
        });
        return;
    }

    const auto result = ToCommonSkillResult(rsp->result());
    invoke_callback([this, rsp, result]() {
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
    });

    if (rsp->skill_id() != 0) {
        invoke_callback([this, rsp]() {
            if (callbacks_.on_skill_cooldown) {
                callbacks_.on_skill_cooldown(rsp->skill_id(), rsp->cooldown_ms());
            }
        });
    }
}

void SkillHandler::HandleSkillEffect(const NetworkPacket& packet) {
    if (callbacks_.owner.has_value() && callbacks_.owner->expired()) {
        return;
    }

    if (packet.payload.empty()) {
        invoke_callback([this]() {
            if (callbacks_.on_parse_error) {
                callbacks_.on_parse_error("Empty skill effect payload");
            }
        });
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SkillEffect>(nullptr)) {
        invoke_callback([this]() {
            if (callbacks_.on_parse_error) {
                callbacks_.on_parse_error("Invalid skill effect payload");
            }
        });
        return;
    }

    const auto* effect = flatbuffers::GetRoot<mir2::proto::SkillEffect>(packet.payload.data());
    if (!effect) {
        invoke_callback([this]() {
            if (callbacks_.on_parse_error) {
                callbacks_.on_parse_error("Skill effect parse failed");
            }
        });
        return;
    }

    const uint32_t effect_id = ParseEffectId(effect);
    invoke_callback([this, effect, effect_id]() {
        if (callbacks_.on_skill_effect) {
            callbacks_.on_skill_effect(effect_id,
                                       effect->caster_id(),
                                       effect->target_id(),
                                       effect->x(),
                                       effect->y());
        }
    });
}

void SkillHandler::HandleSkillListSync(const NetworkPacket& packet) {
    if (callbacks_.owner.has_value() && callbacks_.owner->expired()) {
        return;
    }

    (void)packet;
    invoke_callback([this]() {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Skill list sync handler not implemented");
        }
    });
}

void SkillHandler::HandleCastStart(const NetworkPacket& packet) {
    if (callbacks_.owner.has_value() && callbacks_.owner->expired()) {
        return;
    }

    (void)packet;
    invoke_callback([this]() {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Cast start handler not implemented");
        }
    });
}

void SkillHandler::HandleCastInterrupt(const NetworkPacket& packet) {
    if (callbacks_.owner.has_value() && callbacks_.owner->expired()) {
        return;
    }

    (void)packet;
    invoke_callback([this]() {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Cast interrupt handler not implemented");
        }
    });
}

} // namespace mir2::game::handlers
