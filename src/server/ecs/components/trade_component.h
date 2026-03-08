#ifndef MIR2_ECS_COMPONENTS_TRADE_COMPONENT_H_
#define MIR2_ECS_COMPONENTS_TRADE_COMPONENT_H_

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
    uint64_t trade_id = 0;
    entt::entity partner = entt::null;
    TradeState state = TradeState::kNone;
    int64_t started_at_ms = 0;
    // EnTT's null sentinel is not guaranteed to be zero; initialize explicitly.
    std::array<entt::entity, kMaxTradeSlots> offered_items = [] {
        std::array<entt::entity, kMaxTradeSlots> value{};
        value.fill(entt::null);
        return value;
    }();
    int offered_gold = 0;
    bool confirmed = false;
};

}  // namespace mir2::ecs

#endif
