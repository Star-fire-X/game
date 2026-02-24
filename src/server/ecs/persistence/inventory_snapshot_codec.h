/**
 * @file inventory_snapshot_codec.h
 * @brief Inventory snapshot JSON codec
 */

#ifndef MIR2_SERVER_ECS_PERSISTENCE_INVENTORY_SNAPSHOT_CODEC_H_
#define MIR2_SERVER_ECS_PERSISTENCE_INVENTORY_SNAPSHOT_CODEC_H_

#include <string>
#include <tuple>

#include <entt/entt.hpp>

namespace mir2::ecs::persistence {

/// JSON → 实体化运行时组件 + 快照同步
void LoadInventorySnapshotFromJson(entt::registry& registry,
                                   entt::entity character,
                                   const std::string& inventory_json,
                                   const std::string& equipment_json,
                                   const std::string& skills_json);

/// 快照同步 → JSON（向后兼容）
std::tuple<std::string, std::string, std::string>
SaveInventorySnapshotToJson(entt::registry& registry, entt::entity character);

}  // namespace mir2::ecs::persistence

#endif  // MIR2_SERVER_ECS_PERSISTENCE_INVENTORY_SNAPSHOT_CODEC_H_
