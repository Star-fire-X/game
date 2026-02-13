#include "ecs/systems/recovery_system.h"

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"

#include <algorithm>

namespace mir2::ecs {

RecoverySystem::RecoverySystem()
    : System(SystemPriority::kRecovery) {}

void RecoverySystem::Update(entt::registry& registry, float delta_time) {
    accumulator_ += delta_time;
    if (accumulator_ < kRecoveryInterval) {
        return;
    }
    accumulator_ -= kRecoveryInterval;

    auto view = registry.view<CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& attrs = view.get<CharacterAttributesComponent>(entity);

        // 死亡角色不恢复
        if (attrs.hp <= 0) {
            continue;
        }

        bool changed = false;

        // HP 恢复
        if (attrs.hp < attrs.max_hp) {
            int recovery = std::max(1, attrs.max_hp / 50);
            // 加上渐进恢复累计
            recovery += attrs.inc_health;
            attrs.inc_health = 0;
            attrs.hp = std::min(attrs.max_hp, attrs.hp + recovery);
            changed = true;
        }

        // MP 恢复
        if (attrs.mp < attrs.max_mp) {
            int recovery = std::max(1, attrs.max_mp / 50);
            recovery += attrs.inc_spell;
            attrs.inc_spell = 0;
            attrs.mp = std::min(attrs.max_mp, attrs.mp + recovery);
            changed = true;
        }

        if (changed) {
            dirty_tracker::mark_attributes_dirty(registry, entity);
        }
    }
}

}  // namespace mir2::ecs
