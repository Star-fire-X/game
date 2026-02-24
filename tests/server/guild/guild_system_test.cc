#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/guild_component.h"
#include "ecs/event_bus.h"
#include "ecs/systems/guild_system.h"
#include "game/guild/guild_manager.h"

namespace mir2::ecs {
namespace {

class GuildSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registry_ = std::make_unique<entt::registry>();
    event_bus_ = std::make_unique<EventBus>(*registry_);
    guild_mgr_ = &mir2::game::guild::GuildManager::Instance();
    guild_mgr_->Clear(*registry_);
    guild_system_ = std::make_unique<GuildSystem>(*event_bus_, *guild_mgr_);
    next_id_ = 1;
  }

  void TearDown() override {
    if (guild_mgr_ && registry_) {
      guild_mgr_->Clear(*registry_);
    }
    guild_system_.reset();
    event_bus_.reset();
    registry_.reset();
  }

  entt::entity CreatePlayer(const std::string& name, int gold) {
    auto entity = registry_->create();
    auto& identity = registry_->emplace<CharacterIdentityComponent>(entity);
    identity.id = next_id_++;
    identity.account_id = static_cast<AccountId>(100000 + identity.id);
    identity.name = name;

    auto& attributes = registry_->emplace<CharacterAttributesComponent>(entity);
    attributes.gold = gold;
    return entity;
  }

  GuildId CreateGuildWithLeader(entt::entity leader, const std::string& name) {
    const int result = guild_system_->CreateGuild(*registry_, leader, name);
    EXPECT_EQ(result, 0);

    auto* member = registry_->try_get<GuildMemberComponent>(leader);
    return member ? member->guild_id : kInvalidGuildId;
  }

  GuildComponent* GetGuild(GuildId guild_id) {
    return guild_mgr_->GetGuild(guild_id, *registry_);
  }

  std::unique_ptr<entt::registry> registry_;
  std::unique_ptr<EventBus> event_bus_;
  std::unique_ptr<GuildSystem> guild_system_;
  mir2::game::guild::GuildManager* guild_mgr_ = nullptr;
  CharacterId next_id_ = 1;
};

TEST_F(GuildSystemTest, CreateGuild_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);

  const int result = guild_system_->CreateGuild(*registry_, leader, "Warriors");
  EXPECT_EQ(result, 0);

  auto* member = registry_->try_get<GuildMemberComponent>(leader);
  ASSERT_NE(member, nullptr);
  EXPECT_NE(member->guild_id, 0u);
  EXPECT_EQ(member->rank, GUILD_RANK_LEADER);

  auto* guild = GetGuild(member->guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->guild_name, "Warriors");
  EXPECT_EQ(guild->leader, leader);
  EXPECT_EQ(guild->members.size(), 1u);
  EXPECT_TRUE(guild->IsMember(leader));

  const auto& attributes = registry_->get<CharacterAttributesComponent>(leader);
  EXPECT_EQ(attributes.gold,
            initial_gold - static_cast<int>(GUILD_CREATE_FEE));
}

TEST_F(GuildSystemTest, CreateGuild_AlreadyInGuild) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);

  ASSERT_EQ(guild_system_->CreateGuild(*registry_, leader, "Alpha"), 0);

  const int result = guild_system_->CreateGuild(*registry_, leader, "Beta");
  EXPECT_EQ(result, -1);

  auto* member = registry_->try_get<GuildMemberComponent>(leader);
  ASSERT_NE(member, nullptr);
  auto* guild = GetGuild(member->guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->guild_name, "Alpha");
  EXPECT_EQ(guild_mgr_->GuildCount(), 1u);
}

TEST_F(GuildSystemTest, CreateGuild_DuplicateName) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", initial_gold);

  ASSERT_EQ(guild_system_->CreateGuild(*registry_, leader1, "SameName"), 0);

  const int result = guild_system_->CreateGuild(*registry_, leader2, "SameName");
  EXPECT_EQ(result, -4);
  EXPECT_EQ(registry_->try_get<GuildMemberComponent>(leader2), nullptr);
}

TEST_F(GuildSystemTest, JoinGuild_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "Avengers");
  ASSERT_NE(guild_id, 0u);

  auto member = CreatePlayer("Member", 0);
  EXPECT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  auto* member_comp = registry_->try_get<GuildMemberComponent>(member);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(member_comp->guild_id, guild_id);
  EXPECT_EQ(member_comp->rank, GUILD_RANK_MEMBER);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 2u);
  EXPECT_TRUE(guild->IsMember(member));
}

TEST_F(GuildSystemTest, JoinGuild_GuildFull) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "FullHouse");
  ASSERT_NE(guild_id, 0u);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  guild->max_members = 1;

  auto member = CreatePlayer("Member", 0);
  EXPECT_FALSE(guild_system_->JoinGuild(*registry_, member, guild_id));
  EXPECT_EQ(registry_->try_get<GuildMemberComponent>(member), nullptr);
}

TEST_F(GuildSystemTest, LeaveGuild_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "Rangers");
  ASSERT_NE(guild_id, 0u);

  auto member = CreatePlayer("Member", 0);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  EXPECT_TRUE(guild_system_->LeaveGuild(*registry_, member));
  EXPECT_EQ(registry_->try_get<GuildMemberComponent>(member), nullptr);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 1u);
  EXPECT_TRUE(guild->IsMember(leader));
}

TEST_F(GuildSystemTest, LeaveGuild_LeaderBlocked) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "Guardians");
  ASSERT_NE(guild_id, 0u);

  EXPECT_FALSE(guild_system_->LeaveGuild(*registry_, leader));

  auto* member = registry_->try_get<GuildMemberComponent>(leader);
  ASSERT_NE(member, nullptr);
  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->leader, leader);
  EXPECT_TRUE(guild->IsMember(leader));
}

TEST_F(GuildSystemTest, KickMember_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Raiders");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  EXPECT_TRUE(guild_system_->KickMember(*registry_, leader, member));

  EXPECT_EQ(registry_->try_get<GuildMemberComponent>(member), nullptr);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 1u);
  EXPECT_TRUE(guild->IsMember(leader));
  EXPECT_FALSE(guild->IsMember(member));
}

TEST_F(GuildSystemTest, KickMember_NotLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member1 = CreatePlayer("Member1", 0);
  auto member2 = CreatePlayer("Member2", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Raiders");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member1, guild_id));
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member2, guild_id));

  EXPECT_FALSE(guild_system_->KickMember(*registry_, member1, member2));

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 3u);
  EXPECT_TRUE(guild->IsMember(member1));
  EXPECT_TRUE(guild->IsMember(member2));
}

TEST_F(GuildSystemTest, KickMember_CannotKickLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Guardians");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  EXPECT_FALSE(guild_system_->KickMember(*registry_, leader, leader));

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->leader, leader);
  EXPECT_TRUE(guild->IsMember(leader));
}

TEST_F(GuildSystemTest, UpdateRankStructure_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto officer = CreatePlayer("Officer", 0);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Vanguard");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, officer, guild_id));
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const auto leader_id = registry_->get<CharacterIdentityComponent>(leader).id;
  const auto officer_id =
      registry_->get<CharacterIdentityComponent>(officer).id;
  const auto member_id = registry_->get<CharacterIdentityComponent>(member).id;

  std::vector<GuildRank> new_ranks = {
      GuildRank{.rank = GUILD_RANK_LEADER,
                .rank_name = "Master",
                .member_ids = {leader_id}},
      GuildRank{.rank = GUILD_RANK_VICE_LEADER,
                .rank_name = "Officer",
                .member_ids = {officer_id}},
      GuildRank{.rank = GUILD_RANK_MEMBER,
                .rank_name = "Member",
                .member_ids = {member_id}}};

  const int result =
      guild_system_->UpdateRankStructure(*registry_, leader, new_ranks);
  EXPECT_EQ(result, 0);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  ASSERT_EQ(guild->ranks.size(), 3u);
  EXPECT_EQ(guild->ranks[0].rank, GUILD_RANK_LEADER);
  EXPECT_EQ(guild->ranks[0].rank_name, "Master");
  EXPECT_EQ(guild->ranks[1].rank, GUILD_RANK_VICE_LEADER);
  EXPECT_EQ(guild->ranks[1].rank_name, "Officer");
  EXPECT_EQ(guild->ranks[2].rank, GUILD_RANK_MEMBER);
  EXPECT_EQ(guild->ranks[2].rank_name, "Member");

  auto* leader_comp = registry_->try_get<GuildMemberComponent>(leader);
  auto* officer_comp = registry_->try_get<GuildMemberComponent>(officer);
  auto* member_comp = registry_->try_get<GuildMemberComponent>(member);
  ASSERT_NE(leader_comp, nullptr);
  ASSERT_NE(officer_comp, nullptr);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(leader_comp->rank, GUILD_RANK_LEADER);
  EXPECT_EQ(leader_comp->rank_name, "Master");
  EXPECT_EQ(officer_comp->rank, GUILD_RANK_VICE_LEADER);
  EXPECT_EQ(officer_comp->rank_name, "Officer");
  EXPECT_EQ(member_comp->rank, GUILD_RANK_MEMBER);
  EXPECT_EQ(member_comp->rank_name, "Member");
}

TEST_F(GuildSystemTest, UpdateRankStructure_UsesStableMemberIds) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto officer = CreatePlayer("Officer", 0);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "StableIds");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, officer, guild_id));
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const auto leader_id =
      registry_->get<CharacterIdentityComponent>(leader).id;
  const auto officer_id =
      registry_->get<CharacterIdentityComponent>(officer).id;
  const auto member_id =
      registry_->get<CharacterIdentityComponent>(member).id;

  std::vector<GuildRank> new_ranks = {
      GuildRank{.rank = GUILD_RANK_LEADER,
                .rank_name = "Master",
                .member_ids = {leader_id}},
      GuildRank{.rank = GUILD_RANK_VICE_LEADER,
                .rank_name = "Officer",
                .member_ids = {officer_id}},
      GuildRank{.rank = GUILD_RANK_MEMBER,
                .rank_name = "Member",
                .member_ids = {member_id}}};

  const int result =
      guild_system_->UpdateRankStructure(*registry_, leader, new_ranks);
  EXPECT_EQ(result, 0);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  ASSERT_EQ(guild->ranks.size(), 3u);
  EXPECT_THAT(guild->ranks[0].member_ids, testing::ElementsAre(leader_id));
  EXPECT_THAT(guild->ranks[1].member_ids, testing::ElementsAre(officer_id));
  EXPECT_THAT(guild->ranks[2].member_ids, testing::ElementsAre(member_id));

  auto* leader_comp = registry_->try_get<GuildMemberComponent>(leader);
  auto* officer_comp = registry_->try_get<GuildMemberComponent>(officer);
  auto* member_comp = registry_->try_get<GuildMemberComponent>(member);
  ASSERT_NE(leader_comp, nullptr);
  ASSERT_NE(officer_comp, nullptr);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(leader_comp->rank, GUILD_RANK_LEADER);
  EXPECT_EQ(officer_comp->rank, GUILD_RANK_VICE_LEADER);
  EXPECT_EQ(member_comp->rank, GUILD_RANK_MEMBER);
}

TEST_F(GuildSystemTest, UpdateRankStructure_RemainsValidAfterRenameWhenUsingIds) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "RenameSafe");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const auto leader_id =
      registry_->get<CharacterIdentityComponent>(leader).id;
  const auto member_id =
      registry_->get<CharacterIdentityComponent>(member).id;

  std::vector<GuildRank> initial_ranks = {
      GuildRank{.rank = GUILD_RANK_LEADER,
                .rank_name = "Master",
                .member_ids = {leader_id}},
      GuildRank{.rank = GUILD_RANK_MEMBER,
                .rank_name = "Member",
                .member_ids = {member_id}}};

  ASSERT_EQ(guild_system_->UpdateRankStructure(*registry_, leader, initial_ranks), 0);

  auto& member_identity =
      registry_->get<CharacterIdentityComponent>(member);
  member_identity.name = "MemberRenamed";

  std::vector<GuildRank> renamed_ranks = {
      GuildRank{.rank = GUILD_RANK_LEADER,
                .rank_name = "Master",
                .member_ids = {leader_id}},
      GuildRank{.rank = GUILD_RANK_VICE_LEADER,
                .rank_name = "Officer",
                .member_ids = {member_id}}};

  EXPECT_EQ(
      guild_system_->UpdateRankStructure(*registry_, leader, renamed_ranks), 0);

  auto* member_comp = registry_->try_get<GuildMemberComponent>(member);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(member_comp->rank, GUILD_RANK_VICE_LEADER);
  EXPECT_EQ(member_comp->rank_name, "Officer");
}

TEST_F(GuildSystemTest, UpdateRankStructure_NotLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Vanguard");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const auto leader_id =
      registry_->get<CharacterIdentityComponent>(leader).id;
  const auto member_id =
      registry_->get<CharacterIdentityComponent>(member).id;

  std::vector<GuildRank> new_ranks = {
      GuildRank{.rank = GUILD_RANK_LEADER,
                .rank_name = "Leader",
                .member_ids = {leader_id}},
      GuildRank{.rank = GUILD_RANK_MEMBER,
                .rank_name = "Member",
                .member_ids = {member_id}}};

  const int result =
      guild_system_->UpdateRankStructure(*registry_, member, new_ranks);
  EXPECT_EQ(result, -7);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_TRUE(guild->ranks.empty());
}

TEST_F(GuildSystemTest, SetMemberRank_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member1 = CreatePlayer("Member1", 0);
  auto member2 = CreatePlayer("Member2", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Vanguard");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member1, guild_id));
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member2, guild_id));

  const auto leader_id =
      registry_->get<CharacterIdentityComponent>(leader).id;
  const auto member1_id =
      registry_->get<CharacterIdentityComponent>(member1).id;
  const auto member2_id =
      registry_->get<CharacterIdentityComponent>(member2).id;

  std::vector<GuildRank> new_ranks = {
      GuildRank{.rank = GUILD_RANK_LEADER,
                .rank_name = "Master",
                .member_ids = {leader_id}},
      GuildRank{.rank = GUILD_RANK_VICE_LEADER,
                .rank_name = "Officer",
                .member_ids = {member2_id}},
      GuildRank{.rank = GUILD_RANK_MEMBER,
                .rank_name = "Member",
                .member_ids = {member1_id}}};

  ASSERT_EQ(
      guild_system_->UpdateRankStructure(*registry_, leader, new_ranks), 0);

  EXPECT_TRUE(guild_system_->SetMemberRank(*registry_, leader, member1_id,
                                           GUILD_RANK_VICE_LEADER));

  auto* member_comp = registry_->try_get<GuildMemberComponent>(member1);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(member_comp->rank, GUILD_RANK_VICE_LEADER);
  EXPECT_EQ(member_comp->rank_name, "Officer");

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  const auto* vice_rank = static_cast<const GuildRank*>(nullptr);
  const auto* member_rank = static_cast<const GuildRank*>(nullptr);
  for (const auto& rank : guild->ranks) {
    if (rank.rank == GUILD_RANK_VICE_LEADER) {
      vice_rank = &rank;
    } else if (rank.rank == GUILD_RANK_MEMBER) {
      member_rank = &rank;
    }
  }
  ASSERT_NE(vice_rank, nullptr);
  ASSERT_NE(member_rank, nullptr);
  EXPECT_THAT(vice_rank->member_ids, testing::Contains(member1_id));
  EXPECT_THAT(vice_rank->member_ids, testing::Contains(member2_id));
  EXPECT_THAT(member_rank->member_ids,
              testing::Not(testing::Contains(member1_id)));
}

TEST_F(GuildSystemTest, DeclareWar_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", static_cast<int>(GUILD_CREATE_FEE) + 500);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "GuildA");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "GuildB");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);

  const int result = guild_system_->DeclareWar(*registry_, leader1, guild2);
  EXPECT_EQ(result, 0);

  EXPECT_TRUE(guild_mgr_->IsAtWar(guild1, guild2, *registry_));
  const auto& attributes = registry_->get<CharacterAttributesComponent>(leader1);
  EXPECT_EQ(attributes.gold,
            initial_gold - static_cast<int>(GUILD_CREATE_FEE) -
                static_cast<int>(GUILD_WAR_FEE));
}

TEST_F(GuildSystemTest, DeclareWar_NotLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  auto enemy_leader = CreatePlayer("EnemyLeader", initial_gold);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Alliance");
  const uint32_t enemy_guild_id = CreateGuildWithLeader(enemy_leader, "Enemy");
  ASSERT_NE(guild_id, 0u);
  ASSERT_NE(enemy_guild_id, 0u);

  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const int result = guild_system_->DeclareWar(*registry_, member, enemy_guild_id);
  EXPECT_EQ(result, -1);
  EXPECT_FALSE(guild_mgr_->IsAtWar(guild_id, enemy_guild_id, *registry_));
}

TEST_F(GuildSystemTest, DeclareWar_InsufficientGold) {
  const int initial_gold =
      static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) - 1;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto enemy_leader =
      CreatePlayer("EnemyLeader", static_cast<int>(GUILD_CREATE_FEE) + 500);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "PoorGuild");
  const uint32_t enemy_guild_id =
      CreateGuildWithLeader(enemy_leader, "RichGuild");
  ASSERT_NE(guild_id, 0u);
  ASSERT_NE(enemy_guild_id, 0u);

  const int result =
      guild_system_->DeclareWar(*registry_, leader, enemy_guild_id);
  EXPECT_EQ(result, -2);
  EXPECT_FALSE(guild_mgr_->IsAtWar(guild_id, enemy_guild_id, *registry_));

  const auto& attributes = registry_->get<CharacterAttributesComponent>(leader);
  EXPECT_EQ(attributes.gold,
            initial_gold - static_cast<int>(GUILD_CREATE_FEE));
}

TEST_F(GuildSystemTest, DeclareWar_AllyBlocked) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", initial_gold);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "Lions");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "Tigers");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);

  ASSERT_EQ(guild_system_->MakeAlliance(*registry_, leader1, guild2), 0);
  EXPECT_TRUE(guild_mgr_->IsAllied(guild1, guild2, *registry_));

  const int result = guild_system_->DeclareWar(*registry_, leader1, guild2);
  EXPECT_EQ(result, -4);
  EXPECT_FALSE(guild_mgr_->IsAtWar(guild1, guild2, *registry_));
}

TEST_F(GuildSystemTest, CancelWar_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", static_cast<int>(GUILD_CREATE_FEE) + 500);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "Warriors");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "Raiders");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);

  ASSERT_EQ(guild_system_->DeclareWar(*registry_, leader1, guild2), 0);
  EXPECT_TRUE(guild_mgr_->IsAtWar(guild1, guild2, *registry_));

  EXPECT_TRUE(guild_system_->CancelWar(*registry_, leader1, guild2));
  EXPECT_FALSE(guild_mgr_->IsAtWar(guild1, guild2, *registry_));
}

TEST_F(GuildSystemTest, CancelWar_NotLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  auto enemy_leader = CreatePlayer("EnemyLeader", initial_gold);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Alpha");
  const uint32_t enemy_guild_id = CreateGuildWithLeader(enemy_leader, "Beta");
  ASSERT_NE(guild_id, 0u);
  ASSERT_NE(enemy_guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  ASSERT_EQ(guild_system_->DeclareWar(*registry_, leader, enemy_guild_id), 0);
  EXPECT_TRUE(guild_mgr_->IsAtWar(guild_id, enemy_guild_id, *registry_));

  EXPECT_FALSE(guild_system_->CancelWar(*registry_, member, enemy_guild_id));
  EXPECT_TRUE(guild_mgr_->IsAtWar(guild_id, enemy_guild_id, *registry_));
}

TEST_F(GuildSystemTest, MakeAlliance_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", initial_gold);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "Knights");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "Mages");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);

  const int result = guild_system_->MakeAlliance(*registry_, leader1, guild2);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(guild_mgr_->IsAllied(guild1, guild2, *registry_));

  auto* guild = GetGuild(guild1);
  ASSERT_NE(guild, nullptr);
  EXPECT_TRUE(guild->IsAlliedWith(guild2));
}

TEST_F(GuildSystemTest, BreakAlliance_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", initial_gold);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "Stars");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "Moons");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);

  ASSERT_EQ(guild_system_->MakeAlliance(*registry_, leader1, guild2), 0);
  EXPECT_TRUE(guild_mgr_->IsAllied(guild1, guild2, *registry_));

  EXPECT_TRUE(guild_system_->BreakAlliance(*registry_, leader1, guild2));
  EXPECT_FALSE(guild_mgr_->IsAllied(guild1, guild2, *registry_));
}

TEST_F(GuildSystemTest, BreakAlliance_NotLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  auto ally_leader = CreatePlayer("AllyLeader", initial_gold);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Sun");
  const uint32_t ally_guild_id = CreateGuildWithLeader(ally_leader, "Moon");
  ASSERT_NE(guild_id, 0u);
  ASSERT_NE(ally_guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  ASSERT_EQ(guild_system_->MakeAlliance(*registry_, leader, ally_guild_id), 0);
  EXPECT_TRUE(guild_mgr_->IsAllied(guild_id, ally_guild_id, *registry_));

  EXPECT_FALSE(guild_system_->BreakAlliance(*registry_, member, ally_guild_id));
  EXPECT_TRUE(guild_mgr_->IsAllied(guild_id, ally_guild_id, *registry_));
}

TEST_F(GuildSystemTest, MakeAlliance_AtWar) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE + GUILD_WAR_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", initial_gold);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "Dragons");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "Wolves");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);

  ASSERT_TRUE(guild_mgr_->DeclareWar(guild1, guild2, *registry_));
  EXPECT_TRUE(guild_mgr_->IsAtWar(guild1, guild2, *registry_));

  const int result = guild_system_->MakeAlliance(*registry_, leader1, guild2);
  EXPECT_EQ(result, -3);
  EXPECT_FALSE(guild_mgr_->IsAllied(guild1, guild2, *registry_));
}

TEST_F(GuildSystemTest, TransferLeadership_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Phoenix");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const int result = guild_system_->TransferLeadership(*registry_, leader, member);
  EXPECT_EQ(result, 0);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->leader, member);

  auto* leader_comp = registry_->try_get<GuildMemberComponent>(leader);
  auto* member_comp = registry_->try_get<GuildMemberComponent>(member);
  ASSERT_NE(leader_comp, nullptr);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(leader_comp->rank, GUILD_RANK_MEMBER);
  EXPECT_EQ(member_comp->rank, GUILD_RANK_LEADER);
}

TEST_F(GuildSystemTest, DissolveGuild_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Loners");
  ASSERT_NE(guild_id, 0u);

  const int result = guild_system_->DissolveGuild(*registry_, leader);
  EXPECT_EQ(result, 0);

  EXPECT_EQ(guild_mgr_->GetGuildEntity(guild_id),
            static_cast<entt::entity>(entt::null));
  EXPECT_EQ(registry_->try_get<GuildMemberComponent>(leader), nullptr);
  EXPECT_EQ(guild_mgr_->GuildCount(), 0u);
}

TEST_F(GuildSystemTest, DissolveGuild_HasMembers) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);

  const uint32_t guild_id = CreateGuildWithLeader(leader, "Squad");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const int result = guild_system_->DissolveGuild(*registry_, leader);
  EXPECT_EQ(result, -2);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 2u);
  EXPECT_TRUE(guild->IsMember(leader));
  EXPECT_TRUE(guild->IsMember(member));
}

TEST_F(GuildSystemTest, UpdateNotice_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "NoticeGuild");
  ASSERT_NE(guild_id, 0u);

  const std::vector<std::string> notice_lines = {
      "Welcome to the guild",
      "Be respectful",
  };
  EXPECT_TRUE(guild_system_->UpdateNotice(*registry_, leader, notice_lines));

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->notice_list, notice_lines);
}

TEST_F(GuildSystemTest, UpdateNotice_NotLeader) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "NoticeDenied");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const std::vector<std::string> initial_notice = {"Initial notice"};
  ASSERT_TRUE(guild_system_->UpdateNotice(*registry_, leader, initial_notice));

  const std::vector<std::string> new_notice = {"Member update attempt"};
  EXPECT_FALSE(guild_system_->UpdateNotice(*registry_, member, new_notice));

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->notice_list, initial_notice);
}

TEST_F(GuildSystemTest, GetNotice_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "NoticeRead");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  const std::vector<std::string> notice_lines = {"Line one", "Line two"};
  ASSERT_TRUE(guild_system_->UpdateNotice(*registry_, leader, notice_lines));

  const auto result = guild_system_->GetNotice(*registry_, member);
  EXPECT_EQ(result, notice_lines);
}

TEST_F(GuildSystemTest, StartTeamFight_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "TeamFightStart");
  ASSERT_NE(guild_id, 0u);

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  guild->in_team_fight = false;
  guild->match_point = 7;
  EXPECT_TRUE(guild->AddFightMember(leader));

  EXPECT_TRUE(guild_system_->StartTeamFight(*registry_, leader));
  EXPECT_TRUE(guild->in_team_fight);
  EXPECT_EQ(guild->match_point, 0);
  EXPECT_TRUE(guild->fight_members.empty());
}

TEST_F(GuildSystemTest, JoinTeamFight_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "TeamFightJoin");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  ASSERT_TRUE(guild_system_->StartTeamFight(*registry_, leader));
  EXPECT_TRUE(guild_system_->JoinTeamFight(*registry_, member));

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  ASSERT_EQ(guild->fight_members.size(), 1u);
  EXPECT_EQ(guild->fight_members.front(), member);
}

TEST_F(GuildSystemTest, JoinTeamFight_DuplicateJoinIgnored) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader = CreatePlayer("Leader", initial_gold);
  auto member = CreatePlayer("Member", 0);
  const uint32_t guild_id = CreateGuildWithLeader(leader, "TeamFightNoDup");
  ASSERT_NE(guild_id, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, member, guild_id));

  ASSERT_TRUE(guild_system_->StartTeamFight(*registry_, leader));
  EXPECT_TRUE(guild_system_->JoinTeamFight(*registry_, member));
  EXPECT_TRUE(guild_system_->JoinTeamFight(*registry_, member));

  auto* guild = GetGuild(guild_id);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->fight_members.size(), 1u);
  EXPECT_TRUE(guild->IsFightMember(member));
}

TEST_F(GuildSystemTest, RecordTeamFightKill_Success) {
  const int initial_gold = static_cast<int>(GUILD_CREATE_FEE) + 500;
  auto leader1 = CreatePlayer("Leader1", initial_gold);
  auto leader2 = CreatePlayer("Leader2", initial_gold);
  auto killer = CreatePlayer("Killer", 0);
  auto victim = CreatePlayer("Victim", 0);

  const uint32_t guild1 = CreateGuildWithLeader(leader1, "TeamAlpha");
  const uint32_t guild2 = CreateGuildWithLeader(leader2, "TeamBeta");
  ASSERT_NE(guild1, 0u);
  ASSERT_NE(guild2, 0u);
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, killer, guild1));
  ASSERT_TRUE(guild_system_->JoinGuild(*registry_, victim, guild2));

  ASSERT_TRUE(guild_system_->StartTeamFight(*registry_, leader1));
  ASSERT_TRUE(guild_system_->StartTeamFight(*registry_, leader2));
  ASSERT_TRUE(guild_system_->JoinTeamFight(*registry_, killer));
  ASSERT_TRUE(guild_system_->JoinTeamFight(*registry_, victim));

  auto* killer_guild = GetGuild(guild1);
  auto* victim_guild = GetGuild(guild2);
  ASSERT_NE(killer_guild, nullptr);
  ASSERT_NE(victim_guild, nullptr);
  EXPECT_EQ(killer_guild->match_point, 0);
  EXPECT_EQ(victim_guild->match_point, 0);

  guild_system_->RecordTeamFightKill(*registry_, killer, victim);
  EXPECT_EQ(killer_guild->match_point, 1);
  EXPECT_EQ(victim_guild->match_point, 0);
}

}  // namespace
}  // namespace mir2::ecs
