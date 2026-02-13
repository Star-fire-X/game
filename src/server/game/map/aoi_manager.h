/**
 * @file aoi_manager.h
 * @brief AOI（Area of Interest）视野管理器
 *
 * 使用九宫格算法管理实体的视野范围，实现高效的视野查询和变更通知。
 * 适用于大规模实体的视野同步场景。
 */

#ifndef MIR2_GAME_MAP_AOI_MANAGER_H_
#define MIR2_GAME_MAP_AOI_MANAGER_H_

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mir2::game::map {

/**
 * @brief AOI 事件类型
 */
enum class AOIEventType : uint8_t {
  kEnter = 0,  // 实体进入视野
  kLeave = 1,  // 实体离开视野
  kMove = 2    // 实体在视野内移动
};

/**
 * @brief AOI 管理器
 *
 * 使用九宫格算法实现高效的视野管理：
 * - 将地图划分为固定大小的格子（Grid）
 * - 每个实体所在的格子及其周围8个格子构成其视野范围
 * - 实体移动时只需计算格子变化，高效处理进入/离开视野事件
 *
 * @note 线程安全：所有公共方法都是线程安全的
 */
class AOIManager {
 public:
  /**
   * @brief 默认格子大小（20个瓦片）
   *
   * 格子大小应根据游戏视野范围调整，通常设置为视野半径的 2/3 左右
   */
  static constexpr int32_t kDefaultGridSize = 20;

  /**
   * @brief AOI 事件回调类型
   *
   * @param event_type 事件类型
   * @param watcher_id 观察者实体ID（视野变化的接收者）
   * @param target_id 目标实体ID（进入/离开视野的实体）
   * @param x 目标X坐标
   * @param y 目标Y坐标
   */
  using AOICallback = std::function<void(AOIEventType event_type,
                                          uint64_t watcher_id,
                                          uint64_t target_id,
                                          int32_t x, int32_t y)>;

  /**
   * @brief 构造 AOI 管理器
   *
   * @param map_width 地图宽度（瓦片数）
   * @param map_height 地图高度（瓦片数）
   * @param grid_size 格子大小（瓦片数），默认为 kDefaultGridSize
   */
  AOIManager(int32_t map_width, int32_t map_height,
             int32_t grid_size = kDefaultGridSize);

  ~AOIManager() = default;

  // 禁用拷贝
  AOIManager(const AOIManager&) = delete;
  AOIManager& operator=(const AOIManager&) = delete;

  /**
   * @brief 设置 AOI 事件回调
   *
   * @param callback 回调函数
   */
  void SetCallback(AOICallback callback) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    callback_ = std::move(callback);
  }

  /**
   * @brief 实体进入地图
   *
   * @param entity_id 实体ID
   * @param x X坐标
   * @param y Y坐标
   */
  void Enter(uint64_t entity_id, int32_t x, int32_t y);

  /**
   * @brief 实体离开地图
   *
   * @param entity_id 实体ID
   */
  void Leave(uint64_t entity_id);

  /**
   * @brief 实体移动
   *
   * @param entity_id 实体ID
   * @param new_x 新X坐标
   * @param new_y 新Y坐标
   */
  void Move(uint64_t entity_id, int32_t new_x, int32_t new_y);

  /**
   * @brief 获取指定位置视野范围内的所有实体
   *
   * @param x X坐标
   * @param y Y坐标
   * @return 视野内的实体ID列表
   */
  std::vector<uint64_t> GetEntitiesInView(int32_t x, int32_t y) const;

  /**
   * @brief 获取指定实体视野范围内的所有其他实体
   *
   * @param entity_id 实体ID
   * @return 视野内的其他实体ID列表（不包含自己）
   */
  std::vector<uint64_t> GetEntitiesInViewOf(uint64_t entity_id) const;

  /**
   * @brief 检查两个实体是否在彼此视野内
   *
   * @param entity_a 实体A的ID
   * @param entity_b 实体B的ID
   * @return true 如果两个实体在彼此视野内
   */
  bool InViewOf(uint64_t entity_a, uint64_t entity_b) const;

  /**
   * @brief 获取地图上的实体总数
   *
   * @return 实体数量
   */
  size_t EntityCount() const;

  /**
   * @brief 获取实体位置
   *
   * @param entity_id 实体ID
   * @param out_x 输出X坐标
   * @param out_y 输出Y坐标
   * @return true 如果实体存在
   */
  bool GetEntityPosition(uint64_t entity_id, int32_t& out_x, int32_t& out_y) const;

  /**
   * @brief 清空所有实体
   */
  void Clear();

 private:
  /**
   * @brief 格子ID结构
   */
  struct GridId {
    int32_t gx;  // 格子X索引
    int32_t gy;  // 格子Y索引

    bool operator==(const GridId& other) const {
      return gx == other.gx && gy == other.gy;
    }
  };

  /**
   * @brief GridId 哈希函数
   */
  struct GridIdHash {
    size_t operator()(const GridId& id) const {
      // 组合两个32位整数为64位，然后哈希
      return std::hash<int64_t>()(
          (static_cast<int64_t>(id.gx) << 32) | static_cast<uint32_t>(id.gy));
    }
  };

  static constexpr size_t kMaxSurroundingGridCount = 9;
  using GridArray = std::array<GridId, kMaxSurroundingGridCount>;

  /**
   * @brief 实体位置信息
   */
  struct EntityPosition {
    int32_t x;
    int32_t y;
    GridId grid;
  };

  /**
   * @brief 坐标转换为格子ID
   */
  GridId ToGridId(int32_t x, int32_t y) const {
    return {x / grid_size_, y / grid_size_};
  }

  /**
   * @brief 获取格子周围的九宫格（包括自身）
   *
   * @return 写入 out 的格子数量
   */
  size_t FillSurroundingGrids(const GridId& center, GridArray& out) const;

  /**
   * @brief 获取格子内的所有实体
   */
  const std::unordered_set<uint64_t>* GetEntitiesInGrid(const GridId& grid) const;

  /**
   * @brief 检查目标格子是否在列表中
   */
  static bool ContainsGrid(const GridArray& grids, size_t count,
                           const GridId& target);

  /**
   * @brief 将实体添加到格子
   */
  void AddToGrid(uint64_t entity_id, const GridId& grid);

  /**
   * @brief 从格子中移除实体
   */
  void RemoveFromGrid(uint64_t entity_id, const GridId& grid);

  // 地图尺寸
  int32_t map_width_;
  int32_t map_height_;

  // 格子大小
  int32_t grid_size_;

  // 格子数量
  int32_t grid_count_x_;
  int32_t grid_count_y_;

  // 格子到实体集合的映射
  std::unordered_map<GridId, std::unordered_set<uint64_t>, GridIdHash> grids_;

  // 实体到位置信息的映射
  std::unordered_map<uint64_t, EntityPosition> entities_;

  // AOI 事件回调
  AOICallback callback_;

  // 互斥锁
  mutable std::shared_mutex mutex_;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_AOI_MANAGER_H_
