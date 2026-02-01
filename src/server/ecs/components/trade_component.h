#ifndef MIR2_ECS_COMPONENTS_TRADE_COMPONENT_H
#define MIR2_ECS_COMPONENTS_TRADE_COMPONENT_H

#include <array>
#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs {

constexpr int kMaxTradeSlots = 10;

enum class TradeState : uint8_t {
    kNone = 0,
    kPending = 1,
    kTrading = 2,
    kConfirmed = 3
};

struct TradeComponent {
    entt::entity partner = entt::null;
    TradeState state = TradeState::kNone;
    std::array<entt::entity, kMaxTradeSlots> offered_items{};
    int offered_gold = 0;
    bool confirmed = false;
};

}  // namespace mir2::ecs

#endif
