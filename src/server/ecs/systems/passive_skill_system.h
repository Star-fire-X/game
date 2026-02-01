#ifndef LEGEND2_SERVER_ECS_PASSIVE_SKILL_SYSTEM_H
#define LEGEND2_SERVER_ECS_PASSIVE_SKILL_SYSTEM_H

#include "ecs/components/skill_component.h"
#include "ecs/components/character_components.h"
#include <entt/entt.hpp>

namespace mir2::ecs {

struct AttributeModifiers {
    int attack_bonus = 0;
    int defense_bonus = 0;
    int magic_attack_bonus = 0;
    int magic_defense_bonus = 0;
    int hit_rate_bonus = 0;    // 命中率加成
    int dodge_bonus = 0;       // 闪避加成
    int speed_bonus = 0;
    float critical_bonus = 0.0f;  // 暴击率加成
};

class PassiveSkillSystem {
public:
    explicit PassiveSkillSystem(entt::registry& registry);

    // Recalculate passive bonuses
    void recalculate_passives(entt::entity entity);

    // Get passive bonuses for an entity
    AttributeModifiers get_passive_bonuses(entt::entity entity) const;

    // Trigger passive skills on attack (returns cached bonuses)
    AttributeModifiers trigger_on_attack(entt::entity attacker) const;

private:
    entt::registry& registry_;

    // Calculate bonus from specific passive skill
    AttributeModifiers calculate_skill_bonus(uint32_t skill_id, uint8_t level) const;
};

} // namespace mir2::ecs
#endif
