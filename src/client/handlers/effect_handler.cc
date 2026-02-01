#include "client/handlers/effect_handler.h"

#include "combat_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::game::handlers {
namespace {

mir2::render::EffectPlayType to_render_effect_type(mir2::proto::EffectType effect_type) {
    switch (effect_type) {
        case mir2::proto::EffectType::CAST:
            return mir2::render::EffectPlayType::CAST;
        case mir2::proto::EffectType::PROJECTILE:
            return mir2::render::EffectPlayType::PROJECTILE;
        case mir2::proto::EffectType::HIT:
            return mir2::render::EffectPlayType::HIT;
        case mir2::proto::EffectType::AOE:
            return mir2::render::EffectPlayType::AOE;
        case mir2::proto::EffectType::NONE:
        default:
            return mir2::render::EffectPlayType::CAST;
    }
}

bool TryLockCallbackOwner(const EffectHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

} // namespace

EffectHandler::EffectHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void EffectHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Effect handlers are bound per-instance to capture callbacks.
}

void EffectHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();
    manager.register_handler(mir2::common::MsgId::kSkillEffect,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleSkillEffect(packet);
                                 }
                             });
    manager.register_handler(mir2::common::MsgId::kPlayEffect,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandlePlayEffect(packet);
                                 }
                             });
    manager.register_handler(mir2::common::MsgId::kPlaySound,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandlePlaySound(packet);
                                 }
                             });
}

void EffectHandler::HandleSkillEffect(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty skill effect payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SkillEffect>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid skill effect payload");
        }
        return;
    }

    const auto* effect = flatbuffers::GetRoot<mir2::proto::SkillEffect>(packet.payload.data());
    if (!effect) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Skill effect parse failed");
        }
        return;
    }

    const std::string effect_id = effect->effect_id() ? effect->effect_id()->str() : std::string();
    const std::string sound_id = effect->sound_id() ? effect->sound_id()->str() : std::string();

    if (callbacks_.on_skill_effect) {
        SkillEffectParams params;
        params.caster_id = effect->caster_id();
        params.target_id = effect->target_id();
        params.skill_id = effect->skill_id();
        params.effect_type = to_render_effect_type(effect->effect_type());
        params.effect_id = effect_id;
        params.sound_id = sound_id;
        params.x = effect->x();
        params.y = effect->y();
        params.duration_ms = effect->duration_ms();

        callbacks_.on_skill_effect(params);
    }
}

void EffectHandler::HandlePlayEffect(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty play effect payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PlayEffect>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid play effect payload");
        }
        return;
    }

    const auto* effect = flatbuffers::GetRoot<mir2::proto::PlayEffect>(packet.payload.data());
    if (!effect) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Play effect parse failed");
        }
        return;
    }

    const std::string effect_id = effect->effect_id() ? effect->effect_id()->str() : std::string();

    if (callbacks_.on_play_effect) {
        callbacks_.on_play_effect(effect_id,
                                  effect->x(),
                                  effect->y(),
                                  effect->direction(),
                                  effect->duration_ms());
    }
}

void EffectHandler::HandlePlaySound(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty play sound payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PlaySound>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid play sound payload");
        }
        return;
    }

    const auto* sound = flatbuffers::GetRoot<mir2::proto::PlaySound>(packet.payload.data());
    if (!sound) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Play sound parse failed");
        }
        return;
    }

    const std::string sound_id = sound->sound_id() ? sound->sound_id()->str() : std::string();

    if (callbacks_.on_play_sound) {
        callbacks_.on_play_sound(sound_id, sound->x(), sound->y());
    }
}

} // namespace mir2::game::handlers
