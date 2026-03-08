#include <gtest/gtest.h>

#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

TEST(SessionRoleStoreTest, BindClientRoleReturnsEvictedClientOnDuplicateLogin) {
  RoleStore store;

  EXPECT_FALSE(store.BindClientRole(1001, 5001).has_value());
  const auto evicted = store.BindClientRole(1002, 5001);

  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ(*evicted, 1001u);
  EXPECT_FALSE(store.GetRoleId(1001).has_value());
  ASSERT_TRUE(store.GetRoleId(1002).has_value());
  EXPECT_EQ(*store.GetRoleId(1002), 5001u);
  ASSERT_TRUE(store.GetClientIdByRoleId(5001).has_value());
  EXPECT_EQ(*store.GetClientIdByRoleId(5001), 1002u);
}

TEST(SessionRoleStoreTest, BindClientRoleReturnsNulloptForInvalidInput) {
  RoleStore store;
  EXPECT_FALSE(store.BindClientRole(0, 5001).has_value());
  EXPECT_FALSE(store.BindClientRole(1001, 0).has_value());
}

}  // namespace
}  // namespace mir2::logic::test
