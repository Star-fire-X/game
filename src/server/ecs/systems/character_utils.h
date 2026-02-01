/**
 * @file character_utils.h
 * @brief ECS 角色金币与属性工具函数
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_CHARACTER_UTILS_H
#define LEGEND2_SERVER_ECS_SYSTEMS_CHARACTER_UTILS_H

#include <entt/entt.hpp>

#include "common/types.h"
#include "ecs/components/character_components.h"

namespace mir2::ecs {

namespace CharacterUtils {

/// 增加金币
void AddGold(entt::registry& registry, entt::entity entity, int amount);

/// 花费金币,返回是否成功
bool SpendGold(entt::registry& registry, entt::entity entity, int amount);

/// 获取金币数量
int GetGold(entt::registry& registry, entt::entity entity);

/// 添加属性加成 (装备、buff等)
void AddStats(entt::registry& registry, entt::entity entity,
              const mir2::common::CharacterStats& bonus);

/// 移除属性加成
void RemoveStats(entt::registry& registry, entt::entity entity,
                 const mir2::common::CharacterStats& bonus);

}  // namespace CharacterUtils

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_CHARACTER_UTILS_H
