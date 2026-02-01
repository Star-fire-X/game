/**
 * @file inventory_migration.h
 * @brief JSON 与结构化组件迁移工具
 *
 * 提供 JSON <-> ECS 结构化组件的转换函数，支持批量迁移。
 */

#ifndef LEGEND2_SERVER_ECS_INVENTORY_MIGRATION_H
#define LEGEND2_SERVER_ECS_INVENTORY_MIGRATION_H

#include <string>
#include <tuple>

#include <entt/entt.hpp>

namespace mir2::ecs::inventory {

/// JSON → 结构化组件
void LoadInventoryFromJson(entt::registry& registry,
                           entt::entity character,
                           const std::string& inventory_json,
                           const std::string& equipment_json,
                           const std::string& skills_json);

/// 结构化组件 → JSON（向后兼容）
std::tuple<std::string, std::string, std::string>
SaveInventoryToJson(entt::registry& registry, entt::entity character);

/// 批量迁移所有角色
void MigrateAllCharacters(entt::registry& registry);

}  // namespace mir2::ecs::inventory

#endif  // LEGEND2_SERVER_ECS_INVENTORY_MIGRATION_H
