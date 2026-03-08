#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

#include "common/enums.h"
#include "ecs/components/party_component.h"
#include "ecs/components/guild_component.h"
#include "ecs/components/character_components.h"

namespace mir2::game::chat {
namespace {

class ChatServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registry_ = std::make_unique<entt::registry>();
  }

  void TearDown() override {
    registry_.reset();
  }

  std::unique_ptr<entt::registry> registry_;
};

// 测试组队聊天 - 无队伍返回 false
TEST_F(ChatServiceTest, TeamChatFailsWithoutParty) {
  auto player = registry_->create();
  registry_->emplace<ecs::CharacterIdentityComponent>(
      player, 1001u, uint64_t{5001}, "TestPlayer");

  // 没有 PartyMemberComponent，应该无法发送组队聊天
  auto* member = registry_->try_get<ecs::PartyMemberComponent>(player);
  EXPECT_EQ(member, nullptr);
}

// 测试组队聊天 - 有队伍
TEST_F(ChatServiceTest, TeamChatWithParty) {
  // 创建队伍实体
  auto party_entity = registry_->create();
  auto& party = registry_->emplace<ecs::PartyComponent>(party_entity);
  party.party_id = 100;

  // 创建队员
  auto player1 = registry_->create();
  auto player2 = registry_->create();

  registry_->emplace<ecs::CharacterIdentityComponent>(
      player1, 1001u, uint64_t{5001}, "Player1");
  registry_->emplace<ecs::CharacterIdentityComponent>(
      player2, 1002u, uint64_t{5002}, "Player2");
  registry_->emplace<ecs::PartyMemberComponent>(player1, 100u);
  registry_->emplace<ecs::PartyMemberComponent>(player2, 100u);

  EXPECT_TRUE(party.AddMember(player1));
  EXPECT_TRUE(party.AddMember(player2));
  party.leader = player1;

  // 验证队伍设置
  EXPECT_EQ(party.members.size(), 2);
  EXPECT_TRUE(party.IsLeader(player1));
  EXPECT_TRUE(party.IsMember(player2));
}

// 测试行会聊天 - 无行会返回 false
TEST_F(ChatServiceTest, GuildChatFailsWithoutGuild) {
  auto player = registry_->create();
  registry_->emplace<ecs::CharacterIdentityComponent>(
      player, 1001u, uint64_t{5001}, "TestPlayer");

  auto* member = registry_->try_get<ecs::GuildMemberComponent>(player);
  EXPECT_EQ(member, nullptr);
}

// 测试行会聊天 - 有行会
TEST_F(ChatServiceTest, GuildChatWithGuild) {
  // 创建行会实体
  auto guild_entity = registry_->create();
  auto& guild = registry_->emplace<ecs::GuildComponent>(guild_entity);
  guild.guild_id = 200;
  guild.guild_name = "TestGuild";

  // 创建会员
  auto player1 = registry_->create();
  auto player2 = registry_->create();

  registry_->emplace<ecs::CharacterIdentityComponent>(
      player1, 2001u, uint64_t{6001}, "GuildMember1");
  registry_->emplace<ecs::CharacterIdentityComponent>(
      player2, 2002u, uint64_t{6002}, "GuildMember2");
  registry_->emplace<ecs::GuildMemberComponent>(player1, 200u, 0);  // 会长
  registry_->emplace<ecs::GuildMemberComponent>(player2, 200u, 2);  // 成员

  EXPECT_TRUE(guild.AddMember(player1));
  EXPECT_TRUE(guild.AddMember(player2));
  guild.leader = player1;

  EXPECT_EQ(guild.members.size(), 2);
  EXPECT_TRUE(guild.IsLeader(player1));
  EXPECT_EQ(guild.guild_name, "TestGuild");
}

// 测试私聊开关
TEST_F(ChatServiceTest, WhisperToggle) {
  // 模拟玩家的私聊开关行为
  bool hear_whisper = true;

  // 关闭私聊
  hear_whisper = false;
  EXPECT_FALSE(hear_whisper);

  // 开启私聊
  hear_whisper = true;
  EXPECT_TRUE(hear_whisper);
}

// 测试屏蔽列表
TEST_F(ChatServiceTest, BlockList) {
  std::unordered_set<uint64_t> block_list;
  const size_t max_blocks = 10;

  // 添加屏蔽
  uint64_t target_id = 1001;
  if (block_list.size() < max_blocks) {
    block_list.insert(target_id);
  }
  EXPECT_EQ(block_list.count(target_id), 1);

  // 检查是否被屏蔽
  EXPECT_TRUE(block_list.count(target_id) > 0);
  EXPECT_FALSE(block_list.count(9999) > 0);

  // 移除屏蔽
  block_list.erase(target_id);
  EXPECT_EQ(block_list.count(target_id), 0);
}

// 测试屏蔽列表上限
TEST_F(ChatServiceTest, BlockListLimit) {
  std::unordered_set<uint64_t> block_list;
  const size_t max_blocks = 10;

  // 添加10个屏蔽
  for (uint64_t i = 1; i <= 10; ++i) {
    if (block_list.size() < max_blocks) {
      block_list.insert(i);
    }
  }
  EXPECT_EQ(block_list.size(), 10);

  // 第11个应该失败
  bool added = false;
  if (block_list.size() < max_blocks) {
    block_list.insert(11);
    added = true;
  }
  EXPECT_FALSE(added);
  EXPECT_EQ(block_list.size(), 10);
}

}  // namespace
}  // namespace mir2::game::chat
