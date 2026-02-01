#include <gtest/gtest.h>

#include "common/enums.h"
#include "gateway/message_router.h"

namespace mir2::gateway {

class GatewayConfigTest : public ::testing::Test {};

TEST_F(GatewayConfigTest, LoadProductionConfigSucceeds) {
  MessageRouter router;

  // 加载生产配置文件
  const std::string config_path = "E:/mir2-cpp/config/gateway.yaml";
  ASSERT_TRUE(router.LoadRoutesFromConfig(config_path))
      << "Failed to load production gateway config";

  // 验证路由表已加载
  EXPECT_GT(router.GetRouteCount(), 0u)
      << "No routes loaded from production config";
}

TEST_F(GatewayConfigTest, ProductionRoutesMatchExpectedServices) {
  MessageRouter router;
  ASSERT_TRUE(router.LoadRoutesFromConfig("E:/mir2-cpp/config/gateway.yaml"));

  // 验证 DB 服务路由
  auto target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kLoginReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kDb);
  EXPECT_FALSE(router.RequiresAuth(static_cast<uint16_t>(common::MsgId::kLoginReq)));

  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kLogout));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kDb);
  EXPECT_TRUE(router.RequiresAuth(static_cast<uint16_t>(common::MsgId::kLogout)));

  // 验证 World 服务路由
  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kCreateRoleReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kWorld);
  EXPECT_TRUE(router.RequiresAuth(static_cast<uint16_t>(common::MsgId::kCreateRoleReq)));

  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kRoleListReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kWorld);

  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kChatReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kWorld);

  // 验证 Game 服务路由
  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kMoveReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kGame);
  EXPECT_TRUE(router.RequiresAuth(static_cast<uint16_t>(common::MsgId::kMoveReq)));

  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kAttackReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kGame);

  target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kPickupItemReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kGame);
}

TEST_F(GatewayConfigTest, UnregisteredMessagesReturnNullopt) {
  MessageRouter router;
  ASSERT_TRUE(router.LoadRoutesFromConfig("E:/mir2-cpp/config/gateway.yaml"));

  // 验证未配置的消息返回空
  const auto target = router.GetRouteTarget(static_cast<uint16_t>(common::MsgId::kHeartbeat));
  EXPECT_FALSE(target.has_value())
      << "Heartbeat should not be in route table (handled separately)";
}

}  // namespace mir2::gateway
