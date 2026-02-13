#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "common/enums.h"
#include "gateway/ring_buffer.h"

namespace mir2::gateway {

TEST(RingBufferTest, PushPopRoundTripPreservesMessage) {
  RingBuffer buffer(256);
  const std::vector<uint8_t> payload{1, 2, 3, 4, 5};

  ASSERT_TRUE(buffer.Push(static_cast<uint16_t>(common::MsgId::kAttackReq),
                          common::ChannelType::kTcp,
                          payload));
  EXPECT_GT(buffer.Size(), 0u);

  BufferedMessage message{};
  ASSERT_TRUE(buffer.Pop(&message));
  EXPECT_EQ(message.msg_id, static_cast<uint16_t>(common::MsgId::kAttackReq));
  EXPECT_EQ(message.channel, common::ChannelType::kTcp);
  EXPECT_EQ(message.payload, payload);
  EXPECT_TRUE(buffer.Empty());
}

TEST(RingBufferTest, PushFailureDoesNotCorruptExistingRecord) {
  RingBuffer buffer(64);
  const std::vector<uint8_t> first_payload(12, 0x7);
  const std::vector<uint8_t> oversized_payload(80, 0x9);

  ASSERT_TRUE(buffer.Push(static_cast<uint16_t>(common::MsgId::kChatReq),
                          common::ChannelType::kTcp,
                          first_payload));
  EXPECT_FALSE(buffer.Push(static_cast<uint16_t>(common::MsgId::kAttackReq),
                           common::ChannelType::kTcp,
                           oversized_payload));

  BufferedMessage message{};
  ASSERT_TRUE(buffer.Pop(&message));
  EXPECT_EQ(message.msg_id, static_cast<uint16_t>(common::MsgId::kChatReq));
  EXPECT_EQ(message.payload, first_payload);
  EXPECT_TRUE(buffer.Empty());
}

}  // namespace mir2::gateway
