/**
 * @file monster.h
 * @brief 怪物实体类定义
 *
 * 负责管理单个怪物的数据和状态，与 EnTT 实体绑定。
 */

#ifndef MIR2_GAME_ENTITY_MONSTER_H_
#define MIR2_GAME_ENTITY_MONSTER_H_

#include <cstdint>
#include <memory>
#include <string>

#include <entt/entt.hpp>

#include "common/types.h"

namespace mir2::game::entity {

/**
 * @brief 怪物AI状态
 */
enum class MonsterState : uint8_t {
  kIdle = 0,    // 空闲状态
  kPatrol = 1,  // 巡逻状态
  kChase = 2,   // 追击状态
  kAttack = 3,  // 攻击状态
  kReturn = 4,  // 返回状态（返回出生点）
  kDead = 5     // 死亡状态
};

/**
 * @brief 怪物基础属性
 */
struct MonsterStats {
  int32_t hp = 100;           // 当前生命值
  int32_t max_hp = 100;       // 最大生命值
  int32_t attack = 10;        // 攻击力
  int32_t defense = 5;        // 防御力
  int32_t magic_attack = 0;   // 魔法攻击
  int32_t magic_defense = 0;  // 魔法防御
  float move_speed = 3.0f;    // 移动速度
  float attack_speed = 1.0f;  // 攻击速度
};

/**
 * @brief 怪物数据结构
 *
 * MonsterData: 持久化数据（ID、模板、属性、位置）
 * MonsterAIComponent: 运行时AI状态（状态机、目标等）
 */
struct MonsterData {
  uint64_t id = 0;                 // 怪物实例ID
  uint32_t template_id = 0;        // 怪物模板ID
  std::string name;                // 怪物名称
  MonsterStats stats;              // 怪物属性
  uint32_t map_id = 0;             // 所在地图ID
  int32_t x = 0;                   // 当前X坐标
  int32_t y = 0;                   // 当前Y坐标
  int32_t spawn_x = 0;             // 出生点X坐标
  int32_t spawn_y = 0;             // 出生点Y坐标
  uint8_t direction = 0;           // 朝向
  int32_t attack_range = 1;        // 攻击范围
  int32_t aggro_range = 5;         // 仇恨检测范围
  int32_t patrol_radius = 5;       // 巡逻半径
  uint32_t exp_reward = 10;        // 经验奖励
  uint32_t gold_reward = 5;        // 金币奖励
  float respawn_time = 30.0f;      // 复活时间（秒）
};

/**
 * @brief 怪物实体类
 *
 * 代表游戏中的一个怪物，封装怪物数据和行为。
 * 与 EnTT entity 绑定以支持 ECS 架构。
 *
 * @note 线程安全：非线程安全，需在逻辑线程中操作
 */
class Monster : public std::enable_shared_from_this<Monster> {
 public:
  /**
   * @brief 构造怪物实体
   *
   * @param entity EnTT 实体句柄
   * @param id 怪物唯一ID
   * @param registry ECS 注册表（用于AI状态）
   */
  Monster(entt::entity entity, uint64_t id, entt::registry* registry = nullptr);

  ~Monster() = default;

  // 禁用拷贝
  Monster(const Monster&) = delete;
  Monster& operator=(const Monster&) = delete;

  // 允许移动
  Monster(Monster&&) noexcept = default;
  Monster& operator=(Monster&&) noexcept = default;

  // ---- Getters ----

  uint64_t GetId() const { return data_.id; }
  entt::entity GetEntity() const { return entity_; }
  const std::string& GetName() const { return data_.name; }
  uint32_t GetTemplateId() const { return data_.template_id; }
  uint32_t GetMapId() const { return data_.map_id; }
  int32_t GetX() const { return data_.x; }
  int32_t GetY() const { return data_.y; }
  MonsterState GetState() const;  // 读取 ECS AI 状态
  bool IsDead() const { return data_.stats.hp <= 0 || GetState() == MonsterState::kDead; }
  bool IsAlive() const { return !IsDead(); }

  MonsterData& Data() { return data_; }
  const MonsterData& Data() const { return data_; }

  // ---- Actions ----

  /**
   * @brief 设置怪物位置
   */
  void SetPosition(int32_t x, int32_t y);

  /**
   * @brief 设置AI状态
   */
  void SetState(MonsterState state);

  /**
   * @brief 设置目标
   */
  void SetTarget(uint64_t target_id);

  /**
   * @brief 受到伤害
   *
   * @param damage 伤害值
   * @return 实际扣除的生命值
   */
  int32_t TakeDamage(int32_t damage);

  /**
   * @brief 死亡处理
   */
  void Die();

  /**
   * @brief 复活
   */
  void Respawn();

  /**
   * @brief 返回出生点
   */
  void ReturnToSpawn();

 private:
  entt::entity entity_;     // EnTT 实体句柄
  entt::registry* registry_ = nullptr;  // ECS registry（AI运行时状态）
  MonsterData data_;        // 怪物数据
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_MONSTER_H_
