/**
 * @file registry_manager.h
 * @brief 全局 ECS Registry 管理器
 */

#ifndef LEGEND2_SERVER_ECS_REGISTRY_MANAGER_H
#define LEGEND2_SERVER_ECS_REGISTRY_MANAGER_H

#include "ecs/character_entity_manager.h"
#include "ecs/world.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace mir2::ecs {

/**
 * @brief 全局 ECS World 管理器
 *
 * @warning 非线程安全！所有方法必须在同一线程调用。
 * @note 单例模式，管理所有 World 与跨地图角色管理。
 * @see src/server/ecs/THREADING.md 了解线程模型详情
 */
class RegistryManager {
 public:
  static RegistryManager& Instance();

  /// 获取或查询指定地图的 World（不存在时返回 nullptr）
  World* GetWorld(uint32_t map_id);

  /// 创建 World（若已存在则返回已有实例）
  /// @param reserve_capacity 预估实体容量（0 表示使用配置默认值）
  World* CreateWorld(uint32_t map_id, std::size_t reserve_capacity = 0);

  /// 更新所有 World（每帧调用）
  void UpdateAll(float delta_time);

  /// 遍历所有 World（用于跨 World 操作）
  template<typename Func>
  void ForEachWorld(Func&& func) {
    for (auto& [map_id, world] : worlds_) {
      func(map_id, *world);
    }
  }

  /// 遍历所有 World（只读）
  template<typename Func>
  void ForEachWorld(Func&& func) const {
    for (const auto& [map_id, world] : worlds_) {
      func(map_id, *world);
    }
  }

  /// 获取角色实体管理器
  CharacterEntityManager& GetCharacterManager();
  const CharacterEntityManager& GetCharacterManager() const;

 private:
  RegistryManager();
  RegistryManager(const RegistryManager&) = delete;
  RegistryManager& operator=(const RegistryManager&) = delete;
  RegistryManager(RegistryManager&&) = delete;
  RegistryManager& operator=(RegistryManager&&) = delete;

  /// map_id -> World
  std::unordered_map<uint32_t, std::unique_ptr<World>> worlds_;

  /// 跨 World 角色管理器（全局唯一）
  CharacterEntityManager character_manager_;
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_REGISTRY_MANAGER_H
