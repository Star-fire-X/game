/**
 * @file area_trigger.h
 * @brief 区域触发器与持续效果定义
 */

#ifndef MIR2_GAME_MAP_AREA_TRIGGER_H_
#define MIR2_GAME_MAP_AREA_TRIGGER_H_

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>

namespace mir2::game::map {

/**
 * @brief 区域效果类型
 */
enum class AreaEffectType : uint8_t {
  kDamage = 0,  ///< 持续伤害
  kHeal = 1,    ///< 持续治疗
  kBuff = 2,    ///< 增益效果
  kDebuff = 3,  ///< 减益效果
  kTeleport = 4,    ///< 传送
  kFire = 5,        ///< 火墙
  kMine = 6,        ///< 地雷
  kHolyCurtain = 7, ///< 圣言护体
  kTrap = 8         ///< 困魔咒（定身陷阱）
};

/**
 * @brief 区域触发器
 */
struct AreaTrigger {
  uint32_t trigger_id = 0;
  int32_t center_x = 0;
  int32_t center_y = 0;
  int32_t radius = 0;
  AreaEffectType effect_type = AreaEffectType::kDamage;
  std::function<void(entt::entity)> on_enter;
  std::function<void(entt::entity)> on_exit;

  /**
   * @brief 判断坐标是否在触发区域内
   */
  bool Contains(int32_t x, int32_t y) const {
    const int64_t dx = static_cast<int64_t>(x) - center_x;
    const int64_t dy = static_cast<int64_t>(y) - center_y;
    const int64_t r = radius > 0 ? radius : 0;
    return dx * dx + dy * dy <= r * r;
  }
};

/**
 * @brief 持续区域效果
 */
struct ContinuousAreaEffect {
  uint32_t effect_id = 0;
  AreaEffectType type = AreaEffectType::kDamage;
  entt::entity caster = entt::null;
  uint8_t target_life_attrib = 0;  ///< 目标生命属性（红/绿/蓝名）
  int32_t center_x = 0;
  int32_t center_y = 0;
  int32_t radius = 0;
  float tick_interval = 1.0f;
  int32_t damage_per_tick = 0;
  float duration = 0.0f;

  /**
   * @brief 判断坐标是否在效果区域内
   */
  bool Contains(int32_t x, int32_t y) const {
    const int64_t dx = static_cast<int64_t>(x) - center_x;
    const int64_t dy = static_cast<int64_t>(y) - center_y;
    const int64_t r = radius > 0 ? radius : 0;
    return dx * dx + dy * dy <= r * r;
  }
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_AREA_TRIGGER_H_
