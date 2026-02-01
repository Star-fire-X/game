/**
 * @file character_components.h
 * @brief ECS 角色组件定义
 *
 * 合并角色身份、属性、状态与背包数据，便于统一管理。
 */

#ifndef LEGEND2_SERVER_ECS_CHARACTER_COMPONENTS_H
#define LEGEND2_SERVER_ECS_CHARACTER_COMPONENTS_H

#include "common/types.h"

#include <cstdint>
#include <climits>
#include <string>

namespace mir2::ecs {

/**
 * @brief 角色身份组件
 */
struct CharacterIdentityComponent {
    uint32_t id = 0;                      ///< 角色唯一ID
    std::string account_id;               ///< 账号ID
    std::string name;                     ///< 角色名称
    mir2::common::CharacterClass char_class = mir2::common::CharacterClass::WARRIOR;  ///< 职业
    mir2::common::Gender gender = mir2::common::Gender::MALE;                         ///< 性别
};

/**
 * @brief 角色属性组件
 */
struct CharacterAttributesComponent {
    int level = 1;        ///< 等级
    int experience = 0;   ///< 当前经验值
    int hp = 0;           ///< 当前HP
    int max_hp = 0;       ///< 最大HP
    int mp = 0;           ///< 当前MP
    int max_mp = 0;       ///< 最大MP
    int attack = 0;       ///< 物理攻击
    int defense = 0;      ///< 物理防御
    int magic_attack = 0; ///< 魔法攻击
    int magic_defense = 0;///< 魔法防御
    int speed = 0;        ///< 移动/攻速
    int gold = 0;         ///< 金币数量
    int luck = 0;         ///< 幸运值
    int hit_plus = 0;     ///< 力量加成
    uint8_t life_attrib = 0; ///< 生命属性(0=普通,1=不死)
    int anti_poison = 0;  ///< 抗毒
    int anti_magic = 0;   ///< 反魔法
    int sc = 0;           ///< 精神力（道士）

    /// 获取升到下一级所需经验
    int GetExpForNextLevel() const {
        const int base_exp = 100;
        const int max_level = 255;

        // 防止溢出
        if (level >= max_level) {
            return INT_MAX;
        }

        // 使用 int64_t 计算防止中间结果溢出
        int64_t exp = static_cast<int64_t>(base_exp) * level * level;
        return exp > INT_MAX ? INT_MAX : static_cast<int>(exp);
    }
};

/**
 * @brief 角色状态组件
 */
struct CharacterStateComponent {
    uint32_t map_id = 1;                 ///< 当前地图ID
    mir2::common::Position position = {100, 100}; ///< 当前位置
    mir2::common::Direction direction = mir2::common::Direction::DOWN; ///< 朝向
    int64_t created_at = 0;              ///< 创建时间戳
    int64_t last_login = 0;              ///< 最后登录时间戳
    int64_t last_active = 0;             ///< 最近活跃时间戳
};

/**
 * @brief 背包与装备组件（JSON）
 */
struct InventoryComponent {
    std::string inventory_json = "[]"; ///< 背包数据（JSON格式）
    std::string equipment_json = "{}"; ///< 装备数据（JSON格式）
    std::string skills_json = "[]";    ///< 技能数据（JSON格式）
};

/**
 * @brief 角色脏标记组件
 */
struct DirtyComponent {
    bool identity_dirty = false;    ///< 身份数据变更
    bool attributes_dirty = false;  ///< 属性数据变更
    bool state_dirty = false;       ///< 状态数据变更
    bool inventory_dirty = false;   ///< 旧版背包脏标记（等价于 items/equipment/skills）
    bool items_dirty = false;       ///< 物品数据变更
    bool equipment_dirty = false;   ///< 装备数据变更
    bool skills_dirty = false;      ///< 技能数据变更
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_CHARACTER_COMPONENTS_H
