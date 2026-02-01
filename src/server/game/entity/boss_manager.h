/**
 * @file boss_manager.h
 * @brief BOSS管理器定义
 *
 * 负责管理全局BOSS实例的创建、销毁与更新。
 */

#ifndef MIR2_GAME_ENTITY_BOSS_MANAGER_H_
#define MIR2_GAME_ENTITY_BOSS_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/singleton.h"
#include "game/entity/boss_behavior.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::entity {

/**
 * @brief BOSS管理器
 *
 * 单例类，负责管理所有BOSS的生命周期。
 * @note 线程安全：所有公共方法都是线程安全的
 */
class BossManager : public core::Singleton<BossManager> {
  friend class core::Singleton<BossManager>;

 public:
  /**
   * @brief 创建BOSS
   * @param monster_id 怪物模板ID
   * @return 创建的BOSS行为对象
   */
  std::shared_ptr<BossBehavior> CreateBoss(uint32_t monster_id);

  /**
   * @brief 更新所有BOSS
   * @param delta_time 帧时间(秒)
   */
  void UpdateAll(float delta_time);

  /**
   * @brief 销毁BOSS
   * @param boss_id BOSS实例ID
   */
  void DestroyBoss(uint32_t boss_id);

  /**
   * @brief 获取BOSS
   */
  std::shared_ptr<BossBehavior> GetBoss(uint32_t boss_id) const;

  /**
   * @brief 注册BOSS配置
   */
  void RegisterBossConfig(const BossConfig& config);

  /**
   * @brief 设置事件总线
   */
  void SetEventBus(ecs::EventBus* event_bus);

 private:
  BossManager() = default;
  ~BossManager() = default;

  std::unordered_map<uint32_t, std::shared_ptr<BossBehavior>> bosses_;
  std::unordered_map<uint32_t, BossConfig> boss_configs_;
  mutable std::mutex mutex_;
  uint32_t next_boss_id_ = 1;
  ecs::EventBus* event_bus_ = nullptr;
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_BOSS_MANAGER_H_
