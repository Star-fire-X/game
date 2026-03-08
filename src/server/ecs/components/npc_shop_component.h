/**
 * @file npc_shop_component.h
 * @brief NPC shop component definitions.
 */

#ifndef MIR2_SERVER_ECS_NPC_SHOP_COMPONENT_H_
#define MIR2_SERVER_ECS_NPC_SHOP_COMPONENT_H_

#include <cstdint>
#include <vector>

namespace mir2::ecs {

/**
 * @brief Shop item entry.
 */
struct ShopItem {
    uint32_t item_id = 0;
    int32_t price = 0;      ///< Override price (0 = template price)
    int stock = -1;         ///< Stock (-1 = unlimited)
};

/**
 * @brief NPC shop component.
 */
struct NpcShopComponent {
    uint32_t shop_id = 0;
    std::vector<ShopItem> items;
    float tax_rate = 0.0f;  ///< Tax rate (castle 5%)
    bool buy_back = true;   ///< Whether the shop buys items back
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_NPC_SHOP_COMPONENT_H_
