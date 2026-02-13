#include "ecs/systems/luck_system.h"

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"

#include <algorithm>

namespace mir2::ecs {

LuckSystem::LuckSystem()
    : System(SystemPriority::kRecovery) {}  // Same priority tier as recovery

void LuckSystem::Update(entt::registry& registry, float delta_time) {
    accumulator_ += delta_time;
    if (accumulator_ < kTickInterval) {
        return;
    }
    accumulator_ -= kTickInterval;

    auto view = registry.view<CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& attrs = view.get<CharacterAttributesComponent>(entity);

        if (attrs.body_luck > 0) {
            attrs.body_luck = std::max(0, attrs.body_luck - 1);
            dirty_tracker::mark_attributes_dirty(registry, entity);
        }
    }
}

}  // namespace mir2::ecs
