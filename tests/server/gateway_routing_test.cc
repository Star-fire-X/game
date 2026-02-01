#include <gtest/gtest.h>

#include <memory>

#include "common/enums.h"
#include "gateway/message_router.h"

namespace mir2::gateway {

class GatewayRoutingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    router_ = std::make_unique<MessageRouter>();
  }

  std::unique_ptr<MessageRouter> router_;
};

TEST_F(GatewayRoutingTest, DefaultRoutesAreRegistered) {
  // 模拟 GatewayServer::RegisterDefaultRoutes 行为
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                        common::ServiceType::kDb, false);
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kLogout),
                        common::ServiceType::kDb, true);
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                        common::ServiceType::kWorld, true);
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                        common::ServiceType::kGame, true);

  EXPECT_EQ(router_->GetRouteCount(), 4u);

  auto target = router_->GetRouteTarget(static_cast<uint16_t>(common::MsgId::kLoginReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kDb);
}

TEST_F(GatewayRoutingTest, AuthCheckPreventsUnauthorizedAccess) {
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                        common::ServiceType::kGame, true);

  // 需要认证的消息
  EXPECT_TRUE(router_->RequiresAuth(static_cast<uint16_t>(common::MsgId::kMoveReq)));

  // 不需要认证的消息
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                        common::ServiceType::kDb, false);
  EXPECT_FALSE(router_->RequiresAuth(static_cast<uint16_t>(common::MsgId::kLoginReq)));
}

TEST_F(GatewayRoutingTest, ConfigOverridesDefaultRoutes) {
  // 首先注册默认路由
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                        common::ServiceType::kDb, false);

  auto target = router_->GetRouteTarget(static_cast<uint16_t>(common::MsgId::kLoginReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kDb);

  // 加载配置会覆盖（Clear 后重新加载）
  router_->Clear();
  router_->RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                        common::ServiceType::kWorld, true);

  target = router_->GetRouteTarget(static_cast<uint16_t>(common::MsgId::kLoginReq));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, common::ServiceType::kWorld);
  EXPECT_TRUE(router_->RequiresAuth(static_cast<uint16_t>(common::MsgId::kLoginReq)));
}

TEST_F(GatewayRoutingTest, AllMsgIdRangesCanBeRouted) {
  // 验证各模块消息范围都可正确路由
  router_->RegisterRoute(1001, common::ServiceType::kDb, false);      // 登录模块
  router_->RegisterRoute(2010, common::ServiceType::kGame, true);     // 游戏模块
  router_->RegisterRoute(3001, common::ServiceType::kGame, true);     // 战斗模块
  router_->RegisterRoute(4010, common::ServiceType::kGame, true);     // 物品模块
  router_->RegisterRoute(5001, common::ServiceType::kWorld, true);    // 社交模块
  router_->RegisterRoute(9001, common::ServiceType::kGateway, false); // 系统模块（特殊处理）

  EXPECT_EQ(router_->GetRouteCount(), 6u);

  // 验证每个范围都能找到目标
  EXPECT_TRUE(router_->GetRouteTarget(1001).has_value());
  EXPECT_TRUE(router_->GetRouteTarget(2010).has_value());
  EXPECT_TRUE(router_->GetRouteTarget(3001).has_value());
  EXPECT_TRUE(router_->GetRouteTarget(4010).has_value());
  EXPECT_TRUE(router_->GetRouteTarget(5001).has_value());
  EXPECT_TRUE(router_->GetRouteTarget(9001).has_value());
}

}  // namespace mir2::gateway
