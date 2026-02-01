#ifndef MIR2_HANDLERS_MERCHANT_HANDLER_H
#define MIR2_HANDLERS_MERCHANT_HANDLER_H

#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>
#include <string>

namespace mir2::ecs { class EventBus; }

namespace mir2::handlers {

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

class MerchantHandler {
public:
    explicit MerchantHandler(entt::registry& registry, ecs::EventBus& event_bus);
    
    void LoadShops(const std::string& config_path);
    bool BuyItem(entt::entity player, uint32_t store_id, uint32_t item_id, int count);
    bool SellItem(entt::entity player, entt::entity item, int count);
    const ShopConfig* GetShop(uint32_t store_id) const;

private:
    entt::registry& registry_;
    ecs::EventBus& event_bus_;
    std::unordered_map<uint32_t, ShopConfig> shops_;
};

}  // namespace mir2::handlers

#endif
