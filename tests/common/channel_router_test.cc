#include <gtest/gtest.h>

#include "common/enums.h"
#include "common/network/channel_router.h"

namespace {

using mir2::common::ChannelRouter;
using mir2::common::ChannelType;
using mir2::common::MsgId;

}  // namespace

TEST(ChannelRouterTest, DefaultRoutes) {
  ChannelRouter router;

  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kMoveReq)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kMoveRsp)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kEntityEnter)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kEntityLeave)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kEntityMove)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kEntityUpdate)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kStateSync)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kPartyUpdate)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kAttackRsp)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kDeath)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kRespawn)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kBuffAdd)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kBuffRemove)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kSkillEffect)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kPlayEffect)), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kPlaySound)), ChannelType::kKcp);

  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kLoginReq)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kEnterGameRsp)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kChangeMap)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kTeleport)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kAttackReq)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kSkillReq)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kDamage)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kInventoryUpdate)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kChatReq)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kNpcInteractReq)), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannel(static_cast<uint16_t>(MsgId::kSystemMsg)), ChannelType::kTcp);
}

TEST(ChannelRouterTest, GetChannelWithFallback) {
  ChannelRouter router;

  const uint16_t move_req = static_cast<uint16_t>(MsgId::kMoveReq);
  EXPECT_EQ(router.GetChannelWithFallback(move_req, true), ChannelType::kKcp);
  EXPECT_EQ(router.GetChannelWithFallback(move_req, false), ChannelType::kTcp);

  const uint16_t chat_req = static_cast<uint16_t>(MsgId::kChatReq);
  router.SetRoute(chat_req, ChannelType::kKcp);
  EXPECT_EQ(router.GetChannelWithFallback(chat_req, false), ChannelType::kTcp);
  EXPECT_EQ(router.GetChannelWithFallback(chat_req, true), ChannelType::kKcp);
}
