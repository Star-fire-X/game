/**
 * @file monster_manager.h
 * @brief 怪物管理器
 *
 * 负责管理所有怪物的创建、销毁、查询和更新。
 * 使用单例模式确保全局唯一实例。
 */

#ifndef MIR2_GAME_ENTITY_MONSTER_MANAGER_H_
#define MIR2_GAME_ENTITY_MONSTER_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "core/singleton.h"
#include "game/entity/monster.h"

namespace mir2::game::entity {

/**
 * @brief 怪物管理器
 *
 * 单例类，负责管理所有怪物的生命周期。
 * 支持按地图分组管理怪物。
 *
 * @note 线程安全：所有公共方法都是线程安全的
 */
class MonsterManager : public core::Singleton<MonsterManager> {
  friend class core::Singleton<MonsterManager>;

 public:
  /**
   * @brief 生成怪物
   *
   * @param template_id 怪物模板ID
   * @param map_id 地图ID
   * @param x X坐标
   * @param y Y坐标
   * @param registry EnTT registry 引用
   * @return 创建的怪物共享指针
   */
  std::shared_ptr<Monster> SpawnMonster(uint32_t template_id, uint32_t map_id,
                                        int32_t x, int32_t y,
                                        entt::registry& registry);

  /**
   * @brief 移除怪物
   *
   * @param id 怪物ID
   * @param registry EnTT registry 引用
   */
  void RemoveMonster(uint64_t id, entt::registry& registry);

  /**
   * @brief 根据ID获取怪物
   *
   * @param id 怪物ID
   * @return 怪物共享指针，如果不存在则返回 nullptr
   *
   * @note 时间复杂度：O(1)
   */
  std::shared_ptr<Monster> GetMonster(uint64_t id) const;

  /**
   * @brief 获取指定地图上的所有怪物
   *
   * @param map_id 地图ID
   * @return 怪物列表
   */
  std::vector<std::shared_ptr<Monster>> GetMonstersOnMap(uint32_t map_id) const;

  /**
   * @brief 获取怪物总数
   *
   * @return 当前怪物总数
   */
  size_t TotalCount() const;

  /**
   * @brief 获取指定地图上的怪物数量
   *
   * @param map_id 地图ID
   * @return 该地图上的怪物数量
   */
  size_t CountOnMap(uint32_t map_id) const;

  /**
   * @brief 更新所有怪物
   *
   * @param delta_time 帧时间间隔（秒）
   */
  void Update(float delta_time);

  /**
   * @brief 遍历所有怪物
   *
   * @tparam Func 回调函数类型
   * @param func 对每个怪物执行的回调函数
   */
  template <typename Func>
  void ForEach(Func&& func) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, monster] : monsters_) {
      func(monster);
    }
  }

  /**
   * @brief 清空所有怪物
   *
   * @param registry EnTT registry 引用
   */
  void Clear(entt::registry& registry);

 private:
  MonsterManager() = default;
  ~MonsterManager() = default;

  // ID 到怪物实例的映射
  std::unordered_map<uint64_t, std::shared_ptr<Monster>> monsters_;

  // 地图ID 到怪物ID列表的映射
  std::unordered_map<uint32_t, std::vector<uint64_t>> monsters_by_map_;

  // 保护数据的互斥锁
  mutable std::mutex mutex_;

  // 下一个怪物ID
  uint64_t next_monster_id_ = 1;
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_MONSTER_MANAGER_H_
