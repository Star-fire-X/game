/**
 * @file character_factory.h
 * @brief 角色 ECS 工厂函数声明
 */

#ifndef LEGEND2_SERVER_LEGACY_CHARACTER_FACTORY_H
#define LEGEND2_SERVER_LEGACY_CHARACTER_FACTORY_H

#include "common/character_data.h"
#include <entt/entt.hpp>

namespace legend2 {

/// 创建新角色实体 (ECS版本)
/// @param registry ECS Registry
/// @param id 角色ID
/// @param request 创建请求
/// @return 创建的实体ID
entt::entity CreateCharacterEntity(
    entt::registry& registry,
    uint32_t id,
    const mir2::common::CharacterCreateRequest& request);

/// 从CharacterData加载角色实体
/// @param registry ECS Registry
/// @param data 角色数据
/// @return 加载的实体ID
entt::entity LoadCharacterEntity(
    entt::registry& registry,
    const mir2::common::CharacterData& data);

/// 将角色实体保存为CharacterData
/// @param registry ECS Registry
/// @param entity 实体ID
/// @return 角色数据
mir2::common::CharacterData SaveCharacterData(
    entt::registry& registry,
    entt::entity entity);

}  // namespace legend2

#endif  // LEGEND2_SERVER_LEGACY_CHARACTER_FACTORY_H
