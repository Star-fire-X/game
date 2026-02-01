/**
 * @file boss_behavior.h
 * @brief BOSS行为控制定义
 *
 * 负责管理单个BOSS的阶段状态、特殊机制与技能。
 */

#ifndef MIR2_GAME_ENTITY_BOSS_BEHAVIOR_H_
#define MIR2_GAME_ENTITY_BOSS_BEHAVIOR_H_

#include <cstdint>
#include <string>
#include <vector>

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::entity {

/**
 * @brief BOSS战斗阶段
 */
enum class BossPhase : uint8_t {
  kNormal = 0,  // 正常阶段
  kSummon = 1,  // 召唤阶段
  kRage = 2     // 狂暴阶段
};

/**
 * @brief BOSS配置数据
 */
struct BossConfig {
  uint32_t monster_id = 0;               // 怪物模板ID
  std::string name;                      // BOSS名称
  int32_t max_hp = 1000;                 // 最大生命值
  float rage_hp_threshold = 0.3f;        // 进入狂暴的血量阈值
  float summon_hp_threshold = 0.5f;      // 进入召唤的血量阈值
  uint32_t summon_count = 5;             // 召唤数量
  std::vector<uint32_t> summon_monster_ids;  // 召唤怪物模板ID列表
  float special_skill_interval = 5.0f;   // 特殊技能释放间隔(秒)
  int32_t special_skill_damage = 100;    // 特殊技能伤害
  int32_t attack = 50;                   // 基础攻击
  float attack_speed = 1.0f;             // 基础攻速
  float rage_attack_multiplier = 1.5f;   // 狂暴攻击倍率(+50%)
  float rage_attack_speed_multiplier = 1.3f;  // 狂暴攻速倍率(+30%)
};

/**
 * @brief BOSS行为控制器
 */
class BossBehavior {
 public:
  BossBehavior(uint32_t boss_id, const BossConfig& config,
               ecs::EventBus* event_bus = nullptr);
  ~BossBehavior() = default;

  BossBehavior(const BossBehavior&) = delete;
  BossBehavior& operator=(const BossBehavior&) = delete;
  BossBehavior(BossBehavior&&) noexcept = default;
  BossBehavior& operator=(BossBehavior&&) noexcept = default;

  uint32_t GetBossId() const { return boss_id_; }
  uint32_t GetMonsterId() const { return config_.monster_id; }
  const std::string& GetName() const { return config_.name; }
  BossPhase GetPhase() const { return phase_; }
  int32_t GetCurrentHp() const { return current_hp_; }
  int32_t GetMaxHp() const { return max_hp_; }
  int32_t GetAttack() const { return attack_; }
  float GetAttackSpeed() const { return attack_speed_; }
  bool IsDead() const { return dead_; }

  void SetEventBus(ecs::EventBus* event_bus) { event_bus_ = event_bus; }

  /**
   * @brief 每帧更新
   * @param delta_time 帧时间(秒)
   */
  void Update(float delta_time);

  /**
   * @brief 血量变化回调
   */
  void OnHpChange(int32_t current_hp, int32_t max_hp);

  /**
   * @brief 进入狂暴状态
   */
  void EnterRageMode();

  /**
   * @brief 召唤小怪
   * @param count 召唤数量
   */
  void SummonMinions(uint32_t count);

  /**
   * @brief 释放特殊技能
   */
  void CastSpecialSkill();

 private:
  uint32_t boss_id_ = 0;
  BossConfig config_{};
  BossPhase phase_ = BossPhase::kNormal;
  int32_t current_hp_ = 0;
  int32_t max_hp_ = 0;
  int32_t base_attack_ = 0;
  float base_attack_speed_ = 1.0f;
  int32_t attack_ = 0;
  float attack_speed_ = 1.0f;
  bool rage_triggered_ = false;
  bool summon_triggered_ = false;
  bool dead_ = false;
  float special_skill_timer_ = 0.0f;
  ecs::EventBus* event_bus_ = nullptr;
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_BOSS_BEHAVIOR_H_
