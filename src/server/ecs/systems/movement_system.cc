#include "ecs/systems/movement_system.h"

#include "ecs/dirty_tracker.h"

namespace mir2::ecs {

MovementSystem::MovementSystem()
    : System(SystemPriority::kMovement) {}

void MovementSystem::Update(entt::registry& /*registry*/, float /*delta_time*/) {
    // TODO: 处理移动插值或移动速度计算
}

void MovementSystem::SetPosition(entt::registry& registry, entt::entity entity, int x, int y) {
    auto& state = registry.get_or_emplace<CharacterStateComponent>(entity);
    state.position.x = x;
    state.position.y = y;
    dirty_tracker::mark_state_dirty(registry, entity);
}

void MovementSystem::SetMapId(entt::registry& registry, entt::entity entity, uint32_t map_id) {
    auto& state = registry.get_or_emplace<CharacterStateComponent>(entity);
    state.map_id = map_id;
    dirty_tracker::mark_state_dirty(registry, entity);
}

void MovementSystem::SetDirection(entt::registry& registry, entt::entity entity,
                                  mir2::common::Direction direction) {
    auto& state = registry.get_or_emplace<CharacterStateComponent>(entity);
    state.direction = direction;
}

}  // namespace mir2::ecs
