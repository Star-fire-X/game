/**
 * @file trade_events.h
 * @brief 交易事件定义
 */

#ifndef MIR2_ECS_EVENTS_TRADE_EVENTS_H
#define MIR2_ECS_EVENTS_TRADE_EVENTS_H

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs::events {

/**
 * @brief 发起交易请求事件
 */
struct TradeRequestEvent {
    entt::entity requester;
    entt::entity target;
};

/**
 * @brief 交易被接受事件
 */
struct TradeAcceptedEvent {
    entt::entity requester;
    entt::entity target;
};

/**
 * @brief 交易被拒绝事件
 */
struct TradeDeclinedEvent {
    entt::entity requester;
    entt::entity target;
};

/**
 * @brief 交易物品添加事件
 */
struct TradeItemAddedEvent {
    entt::entity trader;
    entt::entity partner;
    entt::entity item;
    uint32_t item_id;
    int count;
    int trade_slot_index;
};

/**
 * @brief 交易确认事件
 */
struct TradeConfirmedEvent {
    entt::entity trader;
    entt::entity partner;
};

/**
 * @brief 交易完成事件
 */
struct TradeCompletedEvent {
    entt::entity trader_a;
    entt::entity trader_b;
    int gold_from_a;
    int gold_from_b;
};

/**
 * @brief 交易取消事件
 */
struct TradeCancelledEvent {
    entt::entity trader;
    entt::entity partner;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_TRADE_EVENTS_H
