/**
 * @file inventory_migration.h
 * @brief JSON 与实体化库存快照兼容迁移工具
 *
 * 提供 JSON <-> ECS 兼容快照组件的转换函数，支持批量迁移。
 */

#ifndef MIR2_SERVER_ECS_INVENTORY_MIGRATION_H_
#define MIR2_SERVER_ECS_INVENTORY_MIGRATION_H_

#include <string>
#include <tuple>

#include <entt/entt.hpp>

namespace mir2::ecs::inventory {

namespace compat {

/// JSON → 实体化运行时组件 + 快照同步
void LoadInventoryFromJson(entt::registry& registry,
                           entt::entity character,
                           const std::string& inventory_json,
                           const std::string& equipment_json,
                           const std::string& skills_json);

/// 快照同步 → JSON（向后兼容）
std::tuple<std::string, std::string, std::string>
SaveInventoryToJson(entt::registry& registry, entt::entity character);

/// 批量迁移所有角色
void MigrateAllCharacters(entt::registry& registry);

}  // namespace compat

}  // namespace mir2::ecs::inventory

#endif  // MIR2_SERVER_ECS_INVENTORY_MIGRATION_H_
