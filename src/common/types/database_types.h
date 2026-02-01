/**
 * @file database_types.h
 * @brief Legend2 数据库相关类型定义
 *
 * 供 common 与 server 共享的数据库数据结构。
 */

#ifndef LEGEND2_COMMON_TYPES_DATABASE_TYPES_H
#define LEGEND2_COMMON_TYPES_DATABASE_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace mir2::db {

// 账号数据
struct AccountData {
    uint64_t id = 0;
    std::string username;
    std::string password_hash;
    std::string email;
    int64_t created_at = 0;
    int64_t last_login = 0;
    bool banned = false;
};

// 装备槽位数据
struct EquipmentSlotData {
    uint8_t slot;               // 0-12 (武器、衣服、头盔等)
    uint32_t item_template_id;
    uint64_t instance_id;
    int durability = 100;
    int8_t enhancement_level = 0;
};

// 背包物品数据
struct InventorySlotData {
    int slot;                   // 0-39
    uint32_t item_template_id;
    uint64_t instance_id;
    int quantity = 1;
    int durability = 100;
    int8_t enhancement_level = 0;
};

// 角色技能数据
struct CharacterSkillData {
    uint32_t skill_id;
    int level = 1;
    int experience = 0;
};

} // namespace mir2::db

#endif // LEGEND2_COMMON_TYPES_DATABASE_TYPES_H
