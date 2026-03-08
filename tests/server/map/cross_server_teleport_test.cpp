#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "game/map/cross_server_teleport.h"

namespace {

using mir2::game::map::CrossServerTeleport;

TEST(CrossServerTeleportTest, RequestReturnsTrue) {
  const auto entity = static_cast<entt::entity>(1);
  EXPECT_TRUE(CrossServerTeleport::RequestCrossServerTeleport(
      entity, "server-2", "map-10", 120, 88));
}

TEST(CrossServerTeleportTest, HandleResponseSuccessDoesNotThrow) {
  const auto entity = static_cast<entt::entity>(2);
  EXPECT_NO_THROW(CrossServerTeleport::HandleCrossServerResponse(entity, true, ""));
}

TEST(CrossServerTeleportTest, HandleResponseFailureDoesNotThrow) {
  const auto entity = static_cast<entt::entity>(3);
  EXPECT_NO_THROW(CrossServerTeleport::HandleCrossServerResponse(
      entity, false, "target server unavailable"));
}

}  // namespace
