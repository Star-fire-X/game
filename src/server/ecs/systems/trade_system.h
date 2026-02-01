/**
 * @file trade_system.h
 * @brief ECS 玩家间交易系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_TRADE_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_TRADE_SYSTEM_H

#include <entt/entt.hpp>

#include "ecs/components/trade_component.h"
#include "ecs/world.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief 玩家间交易逻辑系统
 */
class TradeSystem : public System {
 public:
    TradeSystem();

    void Update(entt::registry& registry, float delta_time) override;

    // 发起交易请求
    static bool RequestTrade(entt::registry& registry,
                             entt::entity requester,
                             entt::entity target,
                             EventBus* event_bus = nullptr);

    // 接受交易
    static bool AcceptTrade(entt::registry& registry,
                            entt::entity target,
                            EventBus* event_bus = nullptr);

    // 拒绝交易
    static bool DeclineTrade(entt::registry& registry,
                             entt::entity target,
                             EventBus* event_bus = nullptr);

    // 添加交易物品
    static bool AddTradeItem(entt::registry& registry,
                             entt::entity trader,
                             entt::entity item,
                             EventBus* event_bus = nullptr);

    // 移除交易物品
    static bool RemoveTradeItem(entt::registry& registry,
                                entt::entity trader,
                                entt::entity item);

    // 设置交易金币
    static bool SetTradeGold(entt::registry& registry,
                             entt::entity trader,
                             int gold);

    // 确认交易
    static bool ConfirmTrade(entt::registry& registry,
                             entt::entity trader,
                             EventBus* event_bus = nullptr);

    // 执行交易（双方确认后）
    static bool ExecuteTrade(entt::registry& registry,
                             entt::entity trader_a,
                             entt::entity trader_b,
                             EventBus* event_bus = nullptr);

    // 取消交易
    static bool CancelTrade(entt::registry& registry,
                            entt::entity trader,
                            EventBus* event_bus = nullptr);
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_TRADE_SYSTEM_H
