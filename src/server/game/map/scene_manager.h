/**
 * @file scene_manager.h
 * @brief 场景管理器
 *
 * 管理所有地图实例的生命周期和实体到地图的映射关系。
 */

#ifndef MIR2_GAME_MAP_SCENE_MANAGER_H_
#define MIR2_GAME_MAP_SCENE_MANAGER_H_

#include <entt/entt.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "game/map/map_instance.h"
#include "game/map/map_loader.h"

namespace mir2::game::map {

/**
 * @brief 场景管理器
 *
 * 负责：
 * - 地图实例的创建和销毁
 * - 维护 map_id -> MapInstance 映射
 * - 维护 entity -> map_id 反向索引
 * - 提供实体到地图的查询接口
 *
 * @note 线程安全：所有公共方法都是线程安全的
 */
class SceneManager {
 public:
  /**
   * @brief 地图配置
   */
  struct MapConfig {
    int32_t map_id = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t grid_size = AOIManager::kDefaultGridSize;
    std::string map_path;
    bool load_walkability = true;
    std::vector<std::pair<int32_t, int32_t>> fixes;

    MapConfig() = default;
    MapConfig(int32_t map_id_in, int32_t width_in, int32_t height_in)
        : map_id(map_id_in), width(width_in), height(height_in) {}
  };

  SceneManager() = default;
  ~SceneManager() = default;

  // 禁用拷贝
  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  /**
   * @brief 创建地图实例
   *
   * @param config 地图配置
   * @return 地图实例指针，如果地图已存在则返回 nullptr
   */
  MapInstance* CreateMap(const MapConfig& config);

  /**
   * @brief 获取或创建地图实例
   *
   * @param config 地图配置
   * @return 地图实例指针
   */
  MapInstance* GetOrCreateMap(const MapConfig& config);

  /**
   * @brief 获取地图实例
   *
   * @param map_id 地图ID
   * @return 地图实例指针，如果不存在则返回 nullptr
   */
  MapInstance* GetMap(int32_t map_id);

  /**
   * @brief 获取地图实例（const版本）
   *
   * @param map_id 地图ID
   * @return 地图实例指针，如果不存在则返回 nullptr
   */
  const MapInstance* GetMap(int32_t map_id) const;

  /**
   * @brief 通过实体查找所在地图
   *
   * @param entity 实体
   * @return 地图实例指针，如果实体不在任何地图则返回 nullptr
   */
  MapInstance* GetMapByEntity(entt::entity entity);

  /**
   * @brief 通过实体查找所在地图（const版本）
   *
   * @param entity 实体
   * @return 地图实例指针，如果实体不在任何地图则返回 nullptr
   */
  const MapInstance* GetMapByEntity(entt::entity entity) const;

  /**
   * @brief 查询实体所属地图ID（安全接口）
   *
   * @param entity 实体
   * @return 地图ID（不存在返回 std::nullopt）
   */
  std::optional<int32_t> TryGetEntityMapId(entt::entity entity) const;

  /**
   * @brief 添加实体到地图
   *
   * @param map_id 地图ID
   * @param entity 实体
   * @param x X坐标
   * @param y Y坐标
   * @return true 如果添加成功
   */
  bool AddEntityToMap(int32_t map_id, entt::entity entity, int32_t x, int32_t y);

  /**
   * @brief 从地图移除实体
   *
   * @param entity 实体
   * @return true 如果移除成功
   */
  bool RemoveEntityFromMap(entt::entity entity);

  /**
   * @brief 更新实体位置
   *
   * @param entity 实体
   * @param new_x 新X坐标
   * @param new_y 新Y坐标
   * @return true 如果更新成功
   */
  bool UpdateEntityPosition(entt::entity entity, int32_t new_x, int32_t new_y);

  /**
   * @brief 传送实体到目标地图
   *
   * @param entity 实体
   * @param target_map_id 目标地图ID
   * @param target_x 目标X坐标
   * @param target_y 目标Y坐标
   * @return true 如果传送成功
   */
  bool TeleportEntity(entt::entity entity, int32_t target_map_id,
                      int32_t target_x, int32_t target_y);

  /**
   * @brief 销毁地图实例
   *
   * @param map_id 地图ID
   * @return true 如果销毁成功
   */
  bool DestroyMap(int32_t map_id);

  /**
   * @brief 重载地图实例
   *
   * 保存实体 -> 销毁旧实例 -> 重载文件 -> 恢复实体
   *
   * @param map_id 地图ID
   * @return true 如果重载并恢复成功
   */
  bool ReloadMap(int32_t map_id);

  /**
   * @brief 获取地图数量
   *
   * @return 地图数量
   */
  size_t MapCount() const;

  /**
   * @brief 清空所有地图
   */
  void Clear();

 private:
  /**
   * @brief 更新实体到地图的反向索引
   */
  void IndexEntity(entt::entity entity, int32_t map_id);

  /**
   * @brief 移除实体的反向索引
   */
  void UnindexEntity(entt::entity entity);

  /**
   * @brief 应用地图修正
   */
  void ApplyMapFixes(MapInstance* map, const MapConfig& config);

  /**
   * @brief 加载地图数据（不持锁，供内部使用）
   *
   * @param config 地图配置
   * @param out_width 输出地图宽度
   * @param out_height 输出地图高度
   * @param out_walkability 输出可行走性数据（未加载瓦片数据时返回）
   * @return 瓦片数据（成功返回，否则返回 std::nullopt）
   */
  std::optional<MapTileData> LoadMapDataUnlocked(
      const MapConfig& config,
      int32_t& out_width,
      int32_t& out_height,
      std::vector<uint8_t>& out_walkability) const;

  std::unordered_map<int32_t, std::shared_ptr<MapInstance>> maps_;
  std::unordered_map<int32_t, MapConfig> map_configs_;
  std::unordered_map<entt::entity, int32_t> entity_to_map_;
  std::unordered_set<int32_t> loading_map_ids_;
  MapLoader map_loader_;
  std::condition_variable_any map_load_cv_;
  mutable std::mutex entity_ops_mutex_;
  mutable std::shared_mutex mutex_;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_SCENE_MANAGER_H_
