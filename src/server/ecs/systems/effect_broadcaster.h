#ifndef LEGEND2_SERVER_ECS_EFFECT_BROADCASTER_H
#define LEGEND2_SERVER_ECS_EFFECT_BROADCASTER_H

#include "ecs/components/skill_template_component.h"
#include "common/types.h"

#include <cstdint>
#include <entt/entt.hpp>
#include <functional>
#include <string>

namespace mir2::ecs {

// 特效广播回调类型
using EffectBroadcastCallback = std::function<void(
    uint64_t caster_id, uint64_t target_id, uint32_t skill_id,
    uint8_t effect_type, const std::string& effect_id,
    const std::string& sound_id, int x, int y, uint32_t duration_ms)>;

class EffectBroadcaster {
public:
    explicit EffectBroadcaster(entt::registry& registry);

    void set_broadcast_callback(EffectBroadcastCallback callback);

    // 广播技能特效
    void broadcast_skill_effect(entt::entity caster, entt::entity target,
                                const SkillTemplate& skill, uint8_t effect_type);

    // 广播施法特效
    void broadcast_cast_effect(entt::entity caster, const SkillTemplate& skill);

    // 广播命中特效
    void broadcast_hit_effect(entt::entity target, const SkillTemplate& skill);
    void broadcast_hit_effect(entt::entity caster, entt::entity target, const SkillTemplate& skill);

private:
    entt::registry& registry_;
    EffectBroadcastCallback broadcast_callback_;

    mir2::common::Position get_entity_position(entt::entity entity) const;
};

} // namespace mir2::ecs

#endif // LEGEND2_SERVER_ECS_EFFECT_BROADCASTER_H
