/**
 * @file movement_system.h
 * @brief ECS 移动逻辑系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_MOVEMENT_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_MOVEMENT_SYSTEM_H

#include "ecs/components/character_components.h"
#include "ecs/world.h"

#include <cstdint>

namespace mir2::ecs {

/**
 * @brief 角色移动逻辑系统
 */
class MovementSystem : public System {
 public:
    MovementSystem();

    void Update(entt::registry& registry, float delta_time) override;

    /// 设置角色位置
    static void SetPosition(entt::registry& registry, entt::entity entity, int x, int y);

    /// 设置角色所在地图
    static void SetMapId(entt::registry& registry, entt::entity entity, uint32_t map_id);

    /// 设置角色朝向
    static void SetDirection(entt::registry& registry, entt::entity entity,
                             mir2::common::Direction direction);
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_MOVEMENT_SYSTEM_H
