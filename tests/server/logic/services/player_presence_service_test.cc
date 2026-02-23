#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ecs/components/character_components.h"
#include "logic/services/player_presence_service.h"

namespace mir2::logic::test {
namespace {

class PlayerPresenceServiceTest : public ::testing::Test {
 protected:
  entt::entity CreatePlayer(uint64_t player_id,
                            const std::string& name,
                            bool online,
                            uint32_t map_id,
                            int hp) {
    const entt::entity entity = registry_.create();

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = player_id;
    identity.name = name;

    auto& state = registry_.emplace<ecs::CharacterStateComponent>(entity);
    state.is_online = online;
    state.map_id = map_id;

    auto& attributes = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attributes.hp = hp;

    return entity;
  }

  entt::registry registry_;
  PlayerPresenceService service_{registry_};
};

TEST_F(PlayerPresenceServiceTest, BasicPresenceQueriesAndMapFilters) {
  const entt::entity alice = CreatePlayer(1001, "Alice", true, 1, 100);
  CreatePlayer(1002, "Bob", false, 1, 100);
  CreatePlayer(1003, "Carol", true, 2, 50);

  EXPECT_TRUE(service_.IsOnline(1001));
  EXPECT_FALSE(service_.IsOnline(1002));
  EXPECT_FALSE(service_.IsOnline(9999));

  const auto found_alice = service_.FindEntity(1001);
  ASSERT_TRUE(found_alice.has_value());
  EXPECT_EQ(*found_alice, alice);

  EXPECT_FALSE(service_.FindEntity(1002).has_value());

  const auto alice_name = service_.FindName(1001);
  ASSERT_TRUE(alice_name.has_value());
  EXPECT_EQ(*alice_name, "Alice");

  const auto alice_map = service_.FindMapId(1001);
  ASSERT_TRUE(alice_map.has_value());
  EXPECT_EQ(*alice_map, 1u);

  auto map1_ids = service_.GetOnlinePlayerIdsOnMap(1);
  std::sort(map1_ids.begin(), map1_ids.end());
  ASSERT_EQ(map1_ids.size(), 1u);
  EXPECT_EQ(map1_ids[0], 1001u);

  auto all_online = service_.GetAllOnlinePlayerIds();
  std::sort(all_online.begin(), all_online.end());
  ASSERT_EQ(all_online.size(), 2u);
  EXPECT_EQ(all_online[0], 1001u);
  EXPECT_EQ(all_online[1], 1003u);
}

TEST_F(PlayerPresenceServiceTest, NameLookupCacheHandlesRenameAndOfflineTransitions) {
  const entt::entity entity = CreatePlayer(2001, "CachedName", true, 1, 100);

  const auto first_lookup = service_.FindPlayerIdByName("CachedName");
  ASSERT_TRUE(first_lookup.has_value());
  EXPECT_EQ(*first_lookup, 2001u);

  auto& identity = registry_.get<ecs::CharacterIdentityComponent>(entity);
  identity.name = "Renamed";

  EXPECT_FALSE(service_.FindPlayerIdByName("CachedName").has_value());

  const auto renamed_lookup = service_.FindPlayerIdByName("Renamed");
  ASSERT_TRUE(renamed_lookup.has_value());
  EXPECT_EQ(*renamed_lookup, 2001u);

  auto& state = registry_.get<ecs::CharacterStateComponent>(entity);
  state.is_online = false;

  EXPECT_FALSE(service_.FindPlayerIdByName("Renamed").has_value());
}

TEST_F(PlayerPresenceServiceTest, NameLookupCachePressureDoesNotBreakLookups) {
  constexpr int kPlayerCount = 4200;
  for (int i = 0; i < kPlayerCount; ++i) {
    CreatePlayer(300000 + i, "User_" + std::to_string(i), true, 1, 10);
  }

  for (int i = 0; i < kPlayerCount; ++i) {
    const auto lookup = service_.FindPlayerIdByName("User_" + std::to_string(i));
    ASSERT_TRUE(lookup.has_value());
    EXPECT_EQ(*lookup, static_cast<uint64_t>(300000 + i));
  }

  const auto tail_lookup = service_.FindPlayerIdByName("User_4199");
  ASSERT_TRUE(tail_lookup.has_value());
  EXPECT_EQ(*tail_lookup, 304199u);
}

TEST_F(PlayerPresenceServiceTest, ChatPreferencesAndBlockingBehavior) {
  CreatePlayer(4001, "Owner", true, 1, 100);
  CreatePlayer(4002, "Target", true, 1, 100);

  EXPECT_TRUE(service_.CanHearWhisper(4001));
  EXPECT_TRUE(service_.CanHearCry(4001));
  EXPECT_TRUE(service_.CanHearGuildMessage(4001));

  EXPECT_TRUE(service_.SetHearWhisper(4001, false));
  EXPECT_TRUE(service_.SetHearCry(4001, false));
  EXPECT_TRUE(service_.SetHearGuildMessage(4001, false));

  EXPECT_FALSE(service_.CanHearWhisper(4001));
  EXPECT_FALSE(service_.CanHearCry(4001));
  EXPECT_FALSE(service_.CanHearGuildMessage(4001));

  EXPECT_FALSE(service_.AddBlock(4001, 4001));
  EXPECT_TRUE(service_.AddBlock(4001, 4002));
  EXPECT_TRUE(service_.IsBlocked(4001, 4002));
  EXPECT_FALSE(service_.AddBlock(4001, 4002));

  EXPECT_TRUE(service_.RemoveBlock(4001, 4002));
  EXPECT_FALSE(service_.IsBlocked(4001, 4002));

  EXPECT_FALSE(service_.SetHearWhisper(9999, false));
  EXPECT_FALSE(service_.AddBlock(9999, 4002));
  EXPECT_FALSE(service_.CanHearWhisper(9999));
}

TEST_F(PlayerPresenceServiceTest, IsDeadAndFactoryCreation) {
  CreatePlayer(5001, "Alive", true, 1, 10);
  CreatePlayer(5002, "Dead", true, 1, 0);
  CreatePlayer(5003, "Offline", false, 1, 100);

  EXPECT_FALSE(service_.IsDead(5001));
  EXPECT_TRUE(service_.IsDead(5002));
  EXPECT_TRUE(service_.IsDead(5003));
  EXPECT_TRUE(service_.IsDead(9999));

  std::unique_ptr<PlayerPresenceService> created =
      PlayerPresenceService::CreateDefault(registry_);
  ASSERT_NE(created, nullptr);
  EXPECT_FALSE(created->IsDead(5001));
}

}  // namespace
}  // namespace mir2::logic::test
