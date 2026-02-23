/**
 * @file npc_shop_service.h
 * @brief NPC shop service.
 */

#ifndef MIR2_GAME_NPC_NPC_SHOP_SERVICE_H_
#define MIR2_GAME_NPC_NPC_SHOP_SERVICE_H_

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs {
struct NpcShopComponent;
}  // namespace mir2::ecs

namespace mir2::game::npc {

/**
 * @brief NPC shop buy/sell operations.
 */
class NpcShopService {
 public:
  // Buy items from NPC shop.
  static bool BuyItem(entt::registry& registry, entt::entity player,
                      entt::entity npc, uint32_t item_id, int count);

  // Sell items to NPC shop (entire stack).
  static bool SellItem(entt::registry& registry, entt::entity player,
                       entt::entity npc, entt::entity item);

  // Calculate buy price (total).
  static int CalculateBuyPrice(const ecs::NpcShopComponent& shop,
                               uint32_t item_id, int count);

  // Calculate sell price (total).
  static int CalculateSellPrice(const ecs::NpcShopComponent& shop,
                                uint32_t item_id, int count);
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_SHOP_SERVICE_H_
