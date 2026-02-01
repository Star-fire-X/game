/**
 * @file player.h
 * @brief 玩家实体类定义
 *
 * 负责管理单个玩家的数据和状态，与 EnTT 实体绑定。
 */

#ifndef MIR2_GAME_ENTITY_PLAYER_H_
#define MIR2_GAME_ENTITY_PLAYER_H_

#include <cstdint>
#include <memory>
#include <string>

#include <entt/entt.hpp>

#include "common/enums.h"

namespace mir2::game::entity {

/**
 * @brief 玩家基础数据结构
 *
 * 存储玩家的持久化属性数据。
 */
struct PlayerData {
  uint64_t id = 0;               // 玩家唯一ID
  uint64_t account_id = 0;       // 所属账号ID
  std::string name;              // 角色名称
  common::CharacterClass profession = common::CharacterClass::WARRIOR;  // 职业
  common::Gender gender = common::Gender::MALE;                  // 性别
  uint16_t level = 1;            // 等级
  uint64_t exp = 0;              // 经验值
  int32_t hp = 100;              // 当前生命值
  int32_t max_hp = 100;          // 最大生命值
  int32_t mp = 50;               // 当前魔法值
  int32_t max_mp = 50;           // 最大魔法值
  uint32_t map_id = 1;           // 所在地图ID
  int32_t x = 100;               // X坐标
  int32_t y = 100;               // Y坐标
  uint8_t direction = 0;         // 朝向（0-7）
  uint64_t gold = 0;             // 金币数量
};

/**
 * @brief 玩家战斗属性
 *
 * 计算后的战斗相关属性，由基础属性和装备加成得出。
 */
struct PlayerAttributes {
  int32_t attack = 10;           // 物理攻击力
  int32_t defense = 5;           // 物理防御力
  int32_t magic_attack = 10;     // 魔法攻击力
  int32_t magic_defense = 5;     // 魔法防御力
  int32_t accuracy = 10;         // 命中率
  int32_t agility = 10;          // 敏捷
  float crit_rate = 0.05f;       // 暴击率
  float crit_damage = 1.5f;      // 暴击伤害倍率
};

/**
 * @brief 玩家实体类
 *
 * 代表游戏中的一个玩家角色，封装玩家数据和行为。
 * 与 EnTT entity 绑定以支持 ECS 架构。
 *
 * @note 线程安全：非线程安全，需在逻辑线程中操作
 */
class Player : public std::enable_shared_from_this<Player> {
 public:
  /**
   * @brief 构造玩家实体
   *
   * @param entity EnTT 实体句柄
   * @param id 玩家唯一ID
   */
  Player(entt::entity entity, uint64_t id);

  ~Player() = default;

  // 禁用拷贝
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  // 允许移动
  Player(Player&&) noexcept = default;
  Player& operator=(Player&&) noexcept = default;

  // ---- Getters ----

  /**
   * @brief 获取玩家唯一ID
   */
  uint64_t GetId() const { return data_.id; }

  /**
   * @brief 获取 EnTT 实体句柄
   */
  entt::entity GetEntity() const { return entity_; }

  /**
   * @brief 获取玩家名称
   */
  const std::string& GetName() const { return data_.name; }

  /**
   * @brief 获取玩家等级
   */
  uint16_t GetLevel() const { return data_.level; }

  /**
   * @brief 获取所在地图ID
   */
  uint32_t GetMapId() const { return data_.map_id; }

  /**
   * @brief 获取X坐标
   */
  int32_t GetX() const { return data_.x; }

  /**
   * @brief 获取Y坐标
   */
  int32_t GetY() const { return data_.y; }

  /**
   * @brief 检查玩家是否死亡
   */
  bool IsDead() const { return data_.hp <= 0; }

  /**
   * @brief 获取玩家基础数据（可修改）
   */
  PlayerData& Data() { return data_; }

  /**
   * @brief 获取玩家基础数据（只读）
   */
  const PlayerData& Data() const { return data_; }

  /**
   * @brief 获取玩家战斗属性（可修改）
   */
  PlayerAttributes& Attributes() { return attributes_; }

  /**
   * @brief 获取玩家战斗属性（只读）
   */
  const PlayerAttributes& Attributes() const { return attributes_; }

  // ---- Actions ----

  /**
   * @brief 设置玩家位置
   *
   * @param x 新X坐标
   * @param y 新Y坐标
   */
  void SetPosition(int32_t x, int32_t y);

  /**
   * @brief 设置玩家地图
   *
   * @param map_id 新地图ID
   * @param x 新X坐标
   * @param y 新Y坐标
   */
  void SetMap(uint32_t map_id, int32_t x, int32_t y);

  /**
   * @brief 设置朝向
   *
   * @param direction 朝向（0-7）
   */
  void SetDirection(uint8_t direction);

  /**
   * @brief 受到伤害
   *
   * @param damage 伤害值
   * @return 实际扣除的生命值
   */
  int32_t TakeDamage(int32_t damage);

  /**
   * @brief 恢复生命值
   *
   * @param amount 恢复量
   * @return 实际恢复的生命值
   */
  int32_t Heal(int32_t amount);

  /**
   * @brief 增加经验值
   *
   * @param exp 经验值增量
   */
  void AddExp(uint64_t exp);

  /**
   * @brief 重新计算战斗属性
   *
   * 基于等级、职业和装备重新计算所有战斗属性。
   */
  void RecalculateAttributes();

 private:
  entt::entity entity_;          // EnTT 实体句柄
  PlayerData data_;              // 玩家基础数据
  PlayerAttributes attributes_;  // 战斗属性
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_PLAYER_H_
