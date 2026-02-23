#include <gtest/gtest.h>

#include <vector>

#include "common/enums.h"
#include "gateway/connection_holder.h"

namespace mir2::gateway {

TEST(ConnectionHolderTest, StateMachineTransitions) {
  ConnectionHolder holder;

  EXPECT_EQ(holder.GetState(), ConnectionHolder::State::FORWARDING);
  EXPECT_TRUE(holder.EnterHolding());
  EXPECT_EQ(holder.GetState(), ConnectionHolder::State::HOLDING);
  EXPECT_FALSE(holder.EnterFlushing());
  EXPECT_TRUE(holder.EnterRestoring());
  EXPECT_EQ(holder.GetState(), ConnectionHolder::State::RESTORING);
  EXPECT_TRUE(holder.EnterFlushing());
  EXPECT_EQ(holder.GetState(), ConnectionHolder::State::FLUSHING);
  EXPECT_TRUE(holder.ReturnToForwarding());
  EXPECT_EQ(holder.GetState(), ConnectionHolder::State::FORWARDING);
}

TEST(ConnectionHolderTest, HoldingBuffersMessages) {
  ConnectionHolder holder(256, 1024);
  ASSERT_TRUE(holder.EnterHolding());

  std::vector<uint8_t> payload{1, 2, 3, 4};
  auto action = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      common::ChannelType::kTcp,
      payload);

  EXPECT_EQ(action, ConnectionHolder::Action::kBuffered);
  EXPECT_GT(holder.BufferedBytes(), 0u);

  BufferedMessage message{};
  EXPECT_TRUE(holder.PopBufferedMessage(&message));
  EXPECT_EQ(message.msg_id, static_cast<uint16_t>(common::MsgId::kAttackReq));
  EXPECT_EQ(message.channel, common::ChannelType::kTcp);
  EXPECT_EQ(message.payload, payload);
  EXPECT_EQ(holder.BufferedBytes(), 0u);
}

TEST(ConnectionHolderTest, HoldingDropsRealtimeMessages) {
  ConnectionHolder holder(256, 1024);
  ASSERT_TRUE(holder.EnterHolding());

  std::vector<uint8_t> payload(8, 0x1);
  auto action_move = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kMoveReq),
      common::ChannelType::kTcp,
      payload);
  EXPECT_EQ(action_move, ConnectionHolder::Action::kDroppedRealtime);
  EXPECT_EQ(holder.BufferedBytes(), 0u);

  auto action_kcp = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      common::ChannelType::kKcp,
      payload);
  EXPECT_EQ(action_kcp, ConnectionHolder::Action::kDroppedRealtime);
  EXPECT_EQ(holder.BufferedBytes(), 0u);
}

TEST(ConnectionHolderTest, BufferFullTriggersDisconnect) {
  ConnectionHolder holder(128, 1024);
  ASSERT_TRUE(holder.EnterHolding());

  std::vector<uint8_t> payload(32, 0x2);
  auto action = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      common::ChannelType::kTcp,
      payload);
  EXPECT_EQ(action, ConnectionHolder::Action::kBuffered);

  const size_t free_bytes = holder.BufferCapacityBytes() - holder.BufferedBytes();
  std::vector<uint8_t> overflow_payload(free_bytes, 0x3);
  auto overflow_action = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      common::ChannelType::kTcp,
      overflow_payload);
  EXPECT_EQ(overflow_action, ConnectionHolder::Action::kDisconnect);
  EXPECT_TRUE(holder.ShouldDisconnect());
}

TEST(ConnectionHolderTest, DisconnectThresholdProtectsHolding) {
  ConnectionHolder holder(1024, 128);
  ASSERT_TRUE(holder.EnterHolding());

  std::vector<uint8_t> payload(100, 0x1);
  auto first_action = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      common::ChannelType::kTcp,
      payload);
  EXPECT_EQ(first_action, ConnectionHolder::Action::kBuffered);

  auto second_action = holder.HandleClientMessage(
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      common::ChannelType::kTcp,
      payload);
  EXPECT_EQ(second_action, ConnectionHolder::Action::kDisconnect);
  EXPECT_TRUE(holder.ShouldDisconnect());
}

TEST(ConnectionHolderTest, DefaultsMatchLimits) {
  ConnectionHolder holder;
  EXPECT_EQ(holder.BufferCapacityBytes(),
            ConnectionHolder::kDefaultBufferCapacityBytes);
  EXPECT_EQ(holder.DisconnectThresholdBytes(),
            ConnectionHolder::kDefaultDisconnectThresholdBytes);
}

}  // namespace mir2::gateway
