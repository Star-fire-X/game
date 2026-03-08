#include <gtest/gtest.h>

#include <cstdint>

#include <entt/entt.hpp>

#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/trade_component.h"
#include "ecs/systems/trade_system.h"

namespace mir2::ecs::test {
namespace {

entt::entity CreatePlayer(entt::registry& registry, int64_t last_trade_close_time_ms) {
  const auto entity = registry.create();
  auto& state = registry.emplace<CharacterStateComponent>(entity);
  state.last_trade_close_time_ms = last_trade_close_time_ms;
  return entity;
}

TEST(TradeSystemTest, RequestTradeRespectsCloseCooldown) {
  entt::registry registry;
  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  const auto requester = CreatePlayer(registry, now_ms);
  const auto target = CreatePlayer(registry, now_ms - 10000);

  EXPECT_FALSE(TradeSystem::RequestTrade(registry, requester, target, nullptr));

  registry.get<CharacterStateComponent>(requester).last_trade_close_time_ms =
      now_ms - 10000;
  EXPECT_TRUE(TradeSystem::RequestTrade(registry, requester, target, nullptr));

  const auto& requester_trade = registry.get<TradeComponent>(requester);
  const auto& target_trade = registry.get<TradeComponent>(target);
  EXPECT_EQ(requester_trade.state, TradeState::kPending);
  EXPECT_EQ(target_trade.state, TradeState::kPending);
  EXPECT_NE(requester_trade.trade_id, 0u);
  EXPECT_EQ(requester_trade.trade_id, target_trade.trade_id);
}

TEST(TradeSystemTest, UpdateCancelsTimedOutTrades) {
  entt::registry registry;
  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  const auto left = CreatePlayer(registry, 0);
  const auto right = CreatePlayer(registry, 0);

  auto& left_trade = registry.emplace<TradeComponent>(left);
  left_trade.partner = right;
  left_trade.state = TradeState::kTrading;
  left_trade.started_at_ms = now_ms - 121000;

  auto& right_trade = registry.emplace<TradeComponent>(right);
  right_trade.partner = left;
  right_trade.state = TradeState::kTrading;
  right_trade.started_at_ms = now_ms - 121000;

  TradeSystem system;
  system.Update(registry, 0.0F);

  const auto& left_trade_after = registry.get<TradeComponent>(left);
  const auto& right_trade_after = registry.get<TradeComponent>(right);
  EXPECT_EQ(left_trade_after.state, TradeState::kNone);
  EXPECT_EQ(right_trade_after.state, TradeState::kNone);
  EXPECT_TRUE(left_trade_after.partner == entt::null);
  EXPECT_TRUE(right_trade_after.partner == entt::null);
  EXPECT_EQ(left_trade_after.trade_id, 0u);
  EXPECT_EQ(right_trade_after.trade_id, 0u);
  EXPECT_EQ(left_trade_after.started_at_ms, 0);
  EXPECT_EQ(right_trade_after.started_at_ms, 0);

  const auto& left_state = registry.get<CharacterStateComponent>(left);
  const auto& right_state = registry.get<CharacterStateComponent>(right);
  EXPECT_GT(left_state.last_trade_close_time_ms, 0);
  EXPECT_GT(right_state.last_trade_close_time_ms, 0);
}

}  // namespace
}  // namespace mir2::ecs::test
