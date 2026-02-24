/**
 * @file map_instance.h
 * @brief 地图实例管理
 *
 * 管理单个地图实例的实体集合和视野同步（AOI）。
 */

#ifndef MIR2_GAME_MAP_MAP_INSTANCE_H_
#define MIR2_GAME_MAP_MAP_INSTANCE_H_

#include <entt/entt.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include "core/concurrency/mpsc_ring.h"
#include "game/map/aoi_manager.h"
#include "game/map/area_event_processor.h"
#include "game/map/door_manager.h"
#include "game/map/map_attributes.h"
#include "game/map/map_loader.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::map {

/**
 * @brief 地图实例
 *
 * 管理单个地图的实体集合和视野同步。
 * 每个地图实例持有独立的 AOIManager，但不持有 Registry。
 *
 * @note 线程安全：所有公共方法都是线程安全的
 */
class MapInstance {
 public:
  /**
   * @brief AOI 事件回调类型
   *
   * @param event_type 事件类型
   * @param watcher 观察者实体
   * @param target 目标实体
   * @param x 目标X坐标
   * @param y 目标Y坐标
   */
  using AOIEventCallback = std::function<void(AOIEventType event_type,
                                               entt::entity watcher,
                                               entt::entity target,
                                               int32_t x, int32_t y)>;

  /**
   * @brief 构造地图实例
   *
   * @param map_id 地图ID
   * @param map_width 地图宽度（瓦片数）
   * @param map_height 地图高度（瓦片数）
   * @param grid_size AOI 格子大小（默认20）
   * @param walkability 可行走性缓存（为空则默认全部可行走）
   * @param tile_data 瓦片数据（为空则不加载）
   */
  MapInstance(int32_t map_id, int32_t map_width, int32_t map_height,
              int32_t grid_size = AOIManager::kDefaultGridSize,
              std::vector<uint8_t> walkability = {},
              std::optional<MapTileData> tile_data = std::nullopt);

  ~MapInstance() = default;

  // 禁用拷贝
  MapInstance(const MapInstance&) = delete;
  MapInstance& operator=(const MapInstance&) = delete;

  /**
   * @brief 获取地图ID
   */
  int32_t GetMapId() const noexcept { return map_id_; }

  /**
   * @brief 获取地图宽度（瓦片数）
   */
  int32_t GetMapWidth() const noexcept { return map_width_; }

  /**
   * @brief 获取地图高度（瓦片数）
   */
  int32_t GetMapHeight() const noexcept { return map_height_; }

  /**
   * @brief 检查坐标是否在地图边界内
   *
   * @param x X坐标
   * @param y Y坐标
   * @return true 如果坐标合法
   */
  bool IsValidPosition(int32_t x, int32_t y) const;

  /**
   * @brief 检查指定坐标是否可行走
   *
   * @param x X坐标
   * @param y Y坐标
   * @return true 可行走
   */
  bool IsWalkable(int32_t x, int32_t y) const;

  /**
   * @brief 检查指定坐标是否可飞行
   *
   * @param x X坐标
   * @param y Y坐标
   * @return true 可飞行
   */
  bool IsFlyable(int32_t x, int32_t y) const;

  /**
   * @brief 设置指定坐标可行走性
   *
   * @param x X坐标
   * @param y Y坐标
   * @param walkable 是否可行走
   */
  void SetWalkable(int32_t x, int32_t y, bool walkable);

  /**
   * @brief 检查指定坐标是否有门
   *
   * @param x X坐标
   * @param y Y坐标
   * @return true 有门
   */
  bool HasDoor(int32_t x, int32_t y) const;

  /**
   * @brief 获取指定坐标的门索引
   *
   * @param x X坐标
   * @param y Y坐标
   * @return 门索引（无门返回0）
   */
  uint8_t GetDoorIndex(int32_t x, int32_t y) const;

  /**
   * @brief 检查指定坐标的门是否打开
   *
   * @param x X坐标
   * @param y Y坐标
   * @return true 门打开
   */
  bool IsDoorOpen(int32_t x, int32_t y) const;

  /**
   * @brief 获取指定坐标的瓦片信息
   *
   * @param x X坐标
   * @param y Y坐标
   * @return 瓦片信息指针，若不存在则返回 nullptr
   */
  const TileInfo* GetTileInfo(int32_t x, int32_t y) const;

  /**
   * @brief 检查指定坐标是否在安全区
   *
   * @param x X坐标
   * @param y Y坐标
   * @return true 如果在安全区
   */
  bool InSafeZone(int32_t x, int32_t y) const;

  /**
   * @brief 检查地图是否允许PK
   *
   * @return true 如果允许PK
   */
  bool CanPK() const;

  /**
   * @brief 检查地图是否允许召回
   */
  bool CanRecall() const;

  /**
   * @brief 检查地图是否允许随机移动
   */
  bool CanRandomMove() const;

  /**
   * @brief 检查地图是否为战斗区
   */
  bool IsFightZone() const;

  /**
   * @brief 检查地图是否为矿区
   */
  bool IsMineMap() const;

  /**
   * @brief 获取地图黑暗等级
   */
  int32_t GetDarkLevel() const;

  /**
   * @brief 检查进入地图的任务要求
   */
  bool CheckQuestRequirement(int32_t quest_id, int32_t quest_value) const;

  /**
   * @brief 检查等级是否符合地图要求
   *
   * @param level 当前等级
   * @return true 如果满足等级要求
   */
  bool CheckLevelRequirement(int32_t level) const;

  /**
   * @brief 获取地图属性
   */
  MapAttributes GetAttributes() const;

  /**
   * @brief 获取 AOI 管理器
   */
  AOIManager* GetAOIManager() noexcept { return aoi_manager_.get(); }
  const AOIManager* GetAOIManager() const noexcept { return aoi_manager_.get(); }

  /**
   * @brief 设置地图属性
   *
   * @param attrs 地图属性
   */
  void SetAttributes(const MapAttributes& attrs);

  /**
   * @brief 设置 AOI 事件回调
   *
   * @param callback 回调函数
   */
  void SetAOICallback(AOIEventCallback callback);

  /**
   * @brief 派发待处理 AOI 事件（消费端）
   *
   * @return 本次派发的事件数
   */
  size_t DispatchPendingAOIEvents();

  /**
   * @brief 派发待处理 AOI 事件（限制批量）
   *
   * @param max_events 最多派发事件数
   * @return 本次派发的事件数
   */
  size_t DispatchPendingAOIEvents(size_t max_events);

  /**
   * @brief 获取待处理 AOI 事件近似数量
   */
  size_t PendingAOIEventCount() const;

  /**
   * @brief 设置区域事件总线
   */
  void SetEventBus(ecs::EventBus* event_bus);

  /**
   * @brief 添加区域触发器
   */
  void AddAreaTrigger(const AreaTrigger& trigger);

  /**
   * @brief 移除区域触发器
   */
  void RemoveAreaTrigger(uint32_t trigger_id);

  /**
   * @brief 添加持续区域效果
   */
  void AddContinuousAreaEffect(const ContinuousAreaEffect& effect);

  /**
   * @brief 移除持续区域效果
   */
  void RemoveContinuousAreaEffect(uint32_t effect_id);

  /**
   * @brief 更新区域效果
   */
  void UpdateAreaEvents(float delta_time, entt::registry& registry);

  /**
   * @brief 添加实体到地图
   *
   * @param entity 实体
   * @param x X坐标
   * @param y Y坐标
   * @return true 如果添加成功
   */
  bool AddEntity(entt::entity entity, int32_t x, int32_t y);

  /**
   * @brief 从地图移除实体
   *
   * @param entity 实体
   * @return true 如果移除成功
   */
  bool RemoveEntity(entt::entity entity);

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
   * @brief 获取指定位置视野范围内的所有实体
   *
   * @param x X坐标
   * @param y Y坐标
   * @return 视野内的实体列表
   */
  std::vector<entt::entity> GetEntitiesInView(int32_t x, int32_t y) const;

  /**
   * @brief 获取指定实体视野范围内的所有其他实体
   *
   * @param entity 实体
   * @return 视野内的其他实体列表（不包含自己）
   */
  std::vector<entt::entity> GetEntitiesInViewOf(entt::entity entity) const;

  /**
   * @brief 检查实体是否在地图中
   *
   * @param entity 实体
   * @return true 如果实体在地图中
   */
  bool HasEntity(entt::entity entity) const;

  /**
   * @brief 获取地图上的实体总数
   *
   * @return 实体数量
   */
  size_t EntityCount() const;

  /**
   * @brief 获取实体位置
   *
   * @param entity 实体
   * @param out_x 输出X坐标
   * @param out_y 输出Y坐标
   * @return true 如果实体存在
   */
  bool GetEntityPosition(entt::entity entity, int32_t& out_x, int32_t& out_y) const;

  /**
   * @brief 清空地图上的所有实体
   */
  void Clear();

 private:
  /**
   * @brief 内部 AOI 回调适配器
   */
  void OnAOIEvent(AOIEventType event_type, uint64_t watcher_id,
                  uint64_t target_id, int32_t x, int32_t y);

  /**
   * @brief 重建安全区索引
   */
  void RebuildSafeZoneIndex();

  struct PendingAOIEvent {
    AOIEventType event_type = AOIEventType::kMove;
    entt::entity watcher = entt::null;
    entt::entity target = entt::null;
    int32_t x = 0;
    int32_t y = 0;
  };

  /**
   * @brief 实体转换为 uint64_t
   */
  static uint64_t EntityToId(entt::entity entity) noexcept {
    return static_cast<uint64_t>(entity);
  }

  /**
   * @brief uint64_t 转换为实体
   */
  static entt::entity IdToEntity(uint64_t id) noexcept {
    return static_cast<entt::entity>(id);
  }

  /**
   * @brief 获取可行走数据（调用方需持有 mutex_）
   */
  const std::vector<uint8_t>& WalkabilityDataUnsafe() const;

  /**
   * @brief 获取可行走数据可写引用（调用方需持有 mutex_）
   */
  std::vector<uint8_t>& MutableWalkabilityDataUnsafe();

  /**
   * @brief 获取实体操作分片锁
   */
  std::mutex& GetEntityOpsMutex(entt::entity entity) const;

  int32_t map_id_;
  int32_t map_width_;
  int32_t map_height_;

  MapAttributes attributes_;
  std::shared_ptr<const MapAttributes> attributes_snapshot_;

  std::unique_ptr<AOIManager> aoi_manager_;
  std::unique_ptr<AOIManager> safe_zone_index_;
  std::unique_ptr<DoorManager> door_manager_;
  AreaEventProcessor area_event_processor_;
  std::unordered_set<entt::entity> entities_;
  std::vector<uint8_t> walkability_;
  std::optional<MapTileData> tile_data_;

  AOIEventCallback aoi_callback_;
  mutable std::mutex aoi_callback_mutex_;
  std::atomic<bool> aoi_callback_enabled_{false};
  static constexpr size_t kAOIEventQueueCapacity = 8192;
  core::concurrency::MpscRingQueue<PendingAOIEvent, kAOIEventQueueCapacity>
      pending_aoi_events_;
  mutable std::mutex aoi_dispatch_mutex_;
  mutable std::shared_mutex entity_ops_barrier_mutex_;
  static constexpr size_t kEntityOpsLockStripeCount = 64;
  mutable std::array<std::mutex, kEntityOpsLockStripeCount> entity_ops_mutexes_;
  mutable std::shared_mutex mutex_;  // 使用 shared_mutex 支持读写锁分离
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_MAP_INSTANCE_H_
