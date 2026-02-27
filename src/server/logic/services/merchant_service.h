#ifndef MIR2_LOGIC_SERVICES_MERCHANT_SERVICE_H_
#define MIR2_LOGIC_SERVICES_MERCHANT_SERVICE_H_

#include <cstdint>
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "ecs/event_bus.h"

namespace mir2::logic {

struct ShopItem {
    uint32_t item_id = 0;
    int price = 0;
    int stock = -1;  // -1 = unlimited
};

struct ShopConfig {
    uint32_t store_id = 0;
    std::string name;
    std::vector<ShopItem> items;
    float buy_rate = 1.0f;   // 买入价格倍率
    float sell_rate = 0.5f;  // 卖出价格倍率
};

class MerchantService {
public:
    explicit MerchantService(entt::registry& registry, ecs::EventBus& event_bus);
    
    void LoadShops(const std::string& config_path);
    bool BuyItem(entt::entity player, uint32_t store_id, uint32_t item_id, int count);
    bool SellItem(entt::entity player, entt::entity item, int count);
    const ShopConfig* GetShop(uint32_t store_id) const;

private:
    entt::registry& registry_;
    ecs::EventBus& event_bus_;
    ecs::EventBus::Subscription open_merchant_subscription_;
    std::unordered_map<uint32_t, ShopConfig> shops_;
    std::unordered_map<uint32_t, uint32_t> open_shop_by_player_id_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_MERCHANT_SERVICE_H_
