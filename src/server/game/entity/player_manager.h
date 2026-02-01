/**
 * @file player_manager.h
 * @brief 玩家管理器
 *
 * 负责管理所有在线玩家的创建、销毁和查询。
 * 使用单例模式确保全局唯一实例。
 */

#ifndef MIR2_GAME_ENTITY_PLAYER_MANAGER_H_
#define MIR2_GAME_ENTITY_PLAYER_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "core/singleton.h"
#include "game/entity/player.h"

namespace mir2::game::entity {

/**
 * @brief 玩家管理器
 *
 * 单例类，负责管理所有在线玩家的生命周期。
 *
 * @note 线程安全：所有公共方法都是线程安全的
 */
class PlayerManager : public core::Singleton<PlayerManager> {
  friend class core::Singleton<PlayerManager>;

 public:
  /**
   * @brief 创建新玩家
   *
   * @param id 玩家唯一ID
   * @param registry EnTT registry 引用
   * @return 创建的玩家共享指针，如果ID已存在则返回 nullptr
   */
  std::shared_ptr<Player> CreatePlayer(uint64_t id, entt::registry& registry);

  /**
   * @brief 移除玩家
   *
   * @param id 玩家ID
   * @param registry EnTT registry 引用（用于销毁实体）
   */
  void RemovePlayer(uint64_t id, entt::registry& registry);

  /**
   * @brief 根据ID获取玩家
   *
   * @param id 玩家ID
   * @return 玩家共享指针，如果不存在则返回 nullptr
   *
   * @note 时间复杂度：O(1)
   */
  std::shared_ptr<Player> GetPlayer(uint64_t id) const;

  /**
   * @brief 检查玩家是否在线
   *
   * @param id 玩家ID
   * @return true 如果玩家在线
   */
  bool IsOnline(uint64_t id) const;

  /**
   * @brief 获取在线玩家数量
   *
   * @return 当前在线玩家数
   */
  size_t OnlineCount() const;

  /**
   * @brief 获取指定地图上的所有玩家
   *
   * @param map_id 地图ID
   * @return 玩家列表
   */
  std::vector<std::shared_ptr<Player>> GetPlayersOnMap(uint32_t map_id) const;

  /**
   * @brief 遍历所有在线玩家
   *
   * @tparam Func 回调函数类型
   * @param func 对每个玩家执行的回调函数
   */
  template <typename Func>
  void ForEach(Func&& func) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, player] : players_) {
      func(player);
    }
  }

  /**
   * @brief 清空所有玩家
   *
   * @param registry EnTT registry 引用
   */
  void Clear(entt::registry& registry);

 private:
  PlayerManager() = default;
  ~PlayerManager() = default;

  // 玩家ID到玩家实例的映射表
  std::unordered_map<uint64_t, std::shared_ptr<Player>> players_;

  // 保护 players_ 的互斥锁
  mutable std::mutex mutex_;
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_PLAYER_MANAGER_H_
