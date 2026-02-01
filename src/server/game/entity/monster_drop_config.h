/**
 * @file monster_drop_config.h
 * @brief 怪物掉落配置
 */

#ifndef MIR2_GAME_ENTITY_MONSTER_DROP_CONFIG_H
#define MIR2_GAME_ENTITY_MONSTER_DROP_CONFIG_H

#include <cstdint>
#include <vector>
#include <entt/entt.hpp>

namespace mir2::game::entity {

/**
 * @brief 掉落物品信息
 */
struct DropItem {
    uint32_t item_id = 0;
    float drop_rate = 0.0f;
    int32_t min_count = 1;
    int32_t max_count = 1;
    int32_t rarity = 1;
    float boss_bonus = 0.0f;
};

/**
 * @brief 掉落表
 */
struct MonsterDropTable {
    uint32_t monster_template_id = 0;
    std::vector<DropItem> items;
};

/**
 * @brief 所有权模式
 */
enum class LootOwnershipMode : uint8_t {
    kLastHitter = 0,
    kDamageRanking = 1,
    kPartyShare = 2
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_MONSTER_DROP_CONFIG_H
