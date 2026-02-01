#ifndef LEGEND2_SERVER_ECS_EQUIPMENT_BONUS_SYSTEM_H
#define LEGEND2_SERVER_ECS_EQUIPMENT_BONUS_SYSTEM_H

#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief 装备属性加成结果
 */
struct EquipmentBonus {
    int attack_bonus = 0;
    int defense_bonus = 0;
    int magic_attack_bonus = 0;
    int magic_defense_bonus = 0;
    int hp_bonus = 0;
    int mp_bonus = 0;
    int hit_rate_bonus = 0;
    int dodge_bonus = 0;
    int speed_bonus = 0;
    int luck_bonus = 0;

    // 特殊效果
    int lifesteal_percent = 0;    // 吸血百分比
    int reflect_percent = 0;      // 反伤百分比
    int fire_damage = 0;          // 火焰附加伤害
    int ice_damage = 0;           // 冰霜附加伤害
    int lightning_damage = 0;     // 雷电附加伤害
    int poison_damage = 0;        // 毒素附加伤害
};

/**
 * @brief 装备加成系统
 */
class EquipmentBonusSystem {
public:
    explicit EquipmentBonusSystem(entt::registry& registry);

    // 计算实体的总装备加成
    EquipmentBonus calculate_total_bonus(entt::entity entity) const;

    // 重新计算并缓存装备加成
    void recalculate_bonus(entt::entity entity);

    // 获取缓存的装备加成
    const EquipmentBonus* get_cached_bonus(entt::entity entity) const;

private:
    entt::registry& registry_;
};

} // namespace mir2::ecs
#endif
