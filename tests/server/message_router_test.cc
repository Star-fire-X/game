#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "common/enums.h"
#include "gateway/message_router.h"

namespace mir2::gateway {

class MessageRouterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    router_ = std::make_unique<MessageRouter>();
  }

  void TearDown() override {
    router_.reset();
  }

  std::string CreateTempConfig(const std::string& content) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("message_router_test_" + std::to_string(timestamp) + ".yaml");
    std::ofstream output(path);
    output << content;
    output.close();
    return path.string();
  }

  std::unique_ptr<MessageRouter> router_;
};

TEST_F(MessageRouterTest, RegisterAndGetRoute) {
  const uint16_t msg_id = 1001;
  const auto service = common::ServiceType::kWorld;

  router_->RegisterRoute(msg_id, service, false);

  const auto target = router_->GetRouteTarget(msg_id);
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, service);
}

TEST_F(MessageRouterTest, GetRouteTargetReturnsNulloptForUnregistered) {
  const uint16_t msg_id = 9999;
  const auto target = router_->GetRouteTarget(msg_id);
  EXPECT_FALSE(target.has_value());
}

TEST_F(MessageRouterTest, RequiresAuthDefaultsFalse) {
  const uint16_t msg_id = 1002;
  EXPECT_FALSE(router_->RequiresAuth(msg_id));
}

TEST_F(MessageRouterTest, RequiresAuthReturnsTrue) {
  const uint16_t msg_id = 1003;
  router_->RegisterRoute(msg_id, common::ServiceType::kGame, true);
  EXPECT_TRUE(router_->RequiresAuth(msg_id));
}

TEST_F(MessageRouterTest, RequiresAuthReturnsFalse) {
  const uint16_t msg_id = 1004;
  router_->RegisterRoute(msg_id, common::ServiceType::kDb, false);
  EXPECT_FALSE(router_->RequiresAuth(msg_id));
}

TEST_F(MessageRouterTest, ClearRemovesAllRoutes) {
  router_->RegisterRoute(1001, common::ServiceType::kWorld, false);
  router_->RegisterRoute(1002, common::ServiceType::kGame, true);
  EXPECT_EQ(router_->GetRouteCount(), 2u);

  router_->Clear();
  EXPECT_EQ(router_->GetRouteCount(), 0u);
  EXPECT_FALSE(router_->GetRouteTarget(1001).has_value());
}

TEST_F(MessageRouterTest, LoadRoutesFromConfigSuccess) {
  const std::string config = R"(
message_routes:
  - msg_id: 1001
    service: world
    require_auth: true
  - msg_id: 1002
    service: game
    require_auth: false
  - msg_id: 1003
    service: db
)";
  const auto path = CreateTempConfig(config);

  ASSERT_TRUE(router_->LoadRoutesFromConfig(path));
  EXPECT_EQ(router_->GetRouteCount(), 3u);

  const auto target1 = router_->GetRouteTarget(1001);
  ASSERT_TRUE(target1.has_value());
  EXPECT_EQ(*target1, common::ServiceType::kWorld);
  EXPECT_TRUE(router_->RequiresAuth(1001));

  const auto target2 = router_->GetRouteTarget(1002);
  ASSERT_TRUE(target2.has_value());
  EXPECT_EQ(*target2, common::ServiceType::kGame);
  EXPECT_FALSE(router_->RequiresAuth(1002));

  const auto target3 = router_->GetRouteTarget(1003);
  ASSERT_TRUE(target3.has_value());
  EXPECT_EQ(*target3, common::ServiceType::kDb);

  std::filesystem::remove(path);
}

TEST_F(MessageRouterTest, LoadRoutesFromConfigInvalidService) {
  const std::string config = R"(
message_routes:
  - msg_id: 1001
    service: invalid_service
)";
  const auto path = CreateTempConfig(config);

  ASSERT_TRUE(router_->LoadRoutesFromConfig(path));
  EXPECT_EQ(router_->GetRouteCount(), 0u);  // 无效路由被跳过

  std::filesystem::remove(path);
}

TEST_F(MessageRouterTest, LoadRoutesFromConfigMissingSection) {
  const std::string config = R"(
server:
  port: 7000
)";
  const auto path = CreateTempConfig(config);

  ASSERT_TRUE(router_->LoadRoutesFromConfig(path));  // 不报错，允许无路由配置
  EXPECT_EQ(router_->GetRouteCount(), 0u);

  std::filesystem::remove(path);
}

TEST_F(MessageRouterTest, LoadRoutesFromConfigFileNotFound) {
  EXPECT_FALSE(router_->LoadRoutesFromConfig("/non/existent/path.yaml"));
}

TEST_F(MessageRouterTest, ConcurrentAccess) {
  router_->RegisterRoute(1001, common::ServiceType::kWorld, false);
  router_->RegisterRoute(1002, common::ServiceType::kGame, true);

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this]() {
      for (int j = 0; j < 100; ++j) {
        const auto target1 = router_->GetRouteTarget(1001);
        EXPECT_TRUE(target1.has_value());
        const auto target2 = router_->GetRouteTarget(1002);
        EXPECT_TRUE(target2.has_value());
        EXPECT_FALSE(router_->RequiresAuth(1001));
        EXPECT_TRUE(router_->RequiresAuth(1002));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace mir2::gateway
