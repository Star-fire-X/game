/**
 * @file character_codec.h
 * @brief 角色 ECS 工厂函数声明
 */

#ifndef MIR2_SERVER_ECS_PERSISTENCE_CHARACTER_CODEC_H_
#define MIR2_SERVER_ECS_PERSISTENCE_CHARACTER_CODEC_H_

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

#endif  // MIR2_SERVER_ECS_PERSISTENCE_CHARACTER_CODEC_H_
