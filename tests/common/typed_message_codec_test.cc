#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/protocol/message_codec.h"
#include "common/protocol/packet_codec.h"
#include "common/protocol/typed_message_bindings.h"
#include "common/protocol/typed_message_codec.h"
#include "login_generated.h"

namespace {

template <typename Binding, typename Native, typename CompareFn>
void ExpectTypedRoundTrip(const Native& input, CompareFn&& compare_fn) {
  const auto payload = mir2::common::protocol::EncodeTypedPayload<Binding>(input);
  ASSERT_FALSE(payload.empty());
  EXPECT_TRUE(mir2::common::protocol::ValidateTypedPayload<Binding>(
      payload.data(), payload.size()));

  const auto decoded_optional =
      mir2::common::protocol::DecodeTypedPayload<Binding>(payload.data(), payload.size());
  ASSERT_TRUE(decoded_optional.has_value());
  compare_fn(decoded_optional.value(), input);

  const auto packet = mir2::common::protocol::EncodeTypedPacket<Binding>(input);
  ASSERT_FALSE(packet.empty());

  mir2::common::NetworkPacket decoded_packet;
  uint16_t sequence = 0;
  ASSERT_EQ(mir2::common::DecodePacketV2(packet.data(), packet.size(), &decoded_packet, &sequence),
            mir2::common::DecodeStatus::kOk);
  EXPECT_EQ(decoded_packet.msg_id, static_cast<uint16_t>(Binding::kMsgId));
  EXPECT_EQ(sequence, 0);

  Native decoded_from_packet;
  ASSERT_TRUE(mir2::common::protocol::DecodeTypedPayload<Binding>(
      decoded_packet.payload.data(), decoded_packet.payload.size(), &decoded_from_packet));
  compare_fn(decoded_from_packet, input);
}

}  // namespace

TEST(TypedMessageCodecTest, LoginReqBindingRoundTrip) {
  mir2::common::LoginRequest input;
  input.username = "user1";
  input.password = "password1";
  input.version = "1.2.3";
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::LoginReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.username, expected.username);
        EXPECT_EQ(actual.password, expected.password);
        EXPECT_EQ(actual.version, expected.version);
      });
}

TEST(TypedMessageCodecTest, LoginRspBindingRoundTrip) {
  mir2::common::LoginResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.account_id = 42;
  input.session_token = "token_abc";
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::LoginRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.account_id, expected.account_id);
        EXPECT_EQ(actual.session_token, expected.session_token);
      });
}

TEST(TypedMessageCodecTest, CreateRoleReqBindingRoundTrip) {
  mir2::common::CreateCharacterRequest input;
  input.name = "KnightA";
  input.profession = mir2::proto::Profession::WARRIOR;
  input.gender = mir2::proto::Gender::FEMALE;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::CreateRoleReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.name, expected.name);
        EXPECT_EQ(actual.profession, expected.profession);
        EXPECT_EQ(actual.gender, expected.gender);
      });
}

TEST(TypedMessageCodecTest, CreateRoleRspBindingRoundTrip) {
  mir2::common::CreateCharacterResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.player_id = 9999;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::CreateRoleRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.player_id, expected.player_id);
      });
}

TEST(TypedMessageCodecTest, MoveReqBindingRoundTrip) {
  mir2::common::MoveRequest input;
  input.target_x = 120;
  input.target_y = 333;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MoveReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_x, expected.target_x);
        EXPECT_EQ(actual.target_y, expected.target_y);
      });
}

TEST(TypedMessageCodecTest, MoveRspBindingRoundTrip) {
  mir2::common::MoveResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.x = 7;
  input.y = 8;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MoveRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.x, expected.x);
        EXPECT_EQ(actual.y, expected.y);
      });
}

TEST(TypedMessageCodecTest, AttackReqBindingRoundTrip) {
  mir2::common::AttackRequest input;
  input.target_id = 123456;
  input.target_type = mir2::proto::EntityType::MONSTER;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AttackReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_id, expected.target_id);
        EXPECT_EQ(actual.target_type, expected.target_type);
      });
}

TEST(TypedMessageCodecTest, AttackRspBindingRoundTrip) {
  mir2::common::AttackResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.attacker_id = 1;
  input.target_id = 2;
  input.damage = 10;
  input.target_hp = 90;
  input.target_dead = false;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AttackRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.attacker_id, expected.attacker_id);
        EXPECT_EQ(actual.target_id, expected.target_id);
        EXPECT_EQ(actual.damage, expected.damage);
        EXPECT_EQ(actual.target_hp, expected.target_hp);
        EXPECT_EQ(actual.target_dead, expected.target_dead);
      });
}

TEST(TypedMessageCodecTest, SkillReqBindingRoundTrip) {
  mir2::common::SkillRequest input;
  input.skill_id = 1001;
  input.target_id = 2002;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::SkillReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.skill_id, expected.skill_id);
        EXPECT_EQ(actual.target_id, expected.target_id);
      });
}

TEST(TypedMessageCodecTest, SkillRspBindingRoundTrip) {
  mir2::common::SkillResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.caster_id = 3003;
  input.target_id = 4004;
  input.damage = 77;
  input.healing = 9;
  input.target_dead = false;
  input.skill_id = 1001;
  input.result = mir2::proto::SkillResult::HIT;
  input.cooldown_ms = 1500;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::SkillRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.caster_id, expected.caster_id);
        EXPECT_EQ(actual.target_id, expected.target_id);
        EXPECT_EQ(actual.damage, expected.damage);
        EXPECT_EQ(actual.healing, expected.healing);
        EXPECT_EQ(actual.target_dead, expected.target_dead);
        EXPECT_EQ(actual.skill_id, expected.skill_id);
        EXPECT_EQ(actual.result, expected.result);
        EXPECT_EQ(actual.cooldown_ms, expected.cooldown_ms);
      });
}

TEST(TypedMessageCodecTest, UseItemReqBindingRoundTrip) {
  mir2::common::UseItemRequest input;
  input.slot = 12;
  input.item_id = 6001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::UseItemReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
      });
}

TEST(TypedMessageCodecTest, UseItemRspBindingRoundTrip) {
  mir2::common::UseItemResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.slot = 12;
  input.item_id = 6001;
  input.remaining = 9;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::UseItemRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
        EXPECT_EQ(actual.remaining, expected.remaining);
      });
}

TEST(TypedMessageCodecTest, PickupItemReqBindingRoundTrip) {
  mir2::common::PickupItemRequest input;
  input.item_id = 6002;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PickupItemReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.item_id, expected.item_id);
      });
}

TEST(TypedMessageCodecTest, PickupItemRspBindingRoundTrip) {
  mir2::common::PickupItemResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.item_id = 6002;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PickupItemRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.item_id, expected.item_id);
      });
}

TEST(TypedMessageCodecTest, DropItemReqBindingRoundTrip) {
  mir2::common::DropItemRequest input;
  input.slot = 5;
  input.item_id = 6002;
  input.count = 2;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::DropItemReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
        EXPECT_EQ(actual.count, expected.count);
      });
}

TEST(TypedMessageCodecTest, DropItemRspBindingRoundTrip) {
  mir2::common::DropItemResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.item_id = 6002;
  input.count = 2;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::DropItemRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.item_id, expected.item_id);
        EXPECT_EQ(actual.count, expected.count);
      });
}

TEST(TypedMessageCodecTest, EquipReqBindingRoundTrip) {
  mir2::common::EquipRequest input;
  input.slot = 2;
  input.item_id = 7001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::EquipReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
      });
}

TEST(TypedMessageCodecTest, EquipRspBindingRoundTrip) {
  mir2::common::EquipResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.slot = 2;
  input.item_id = 7001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::EquipRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
      });
}

TEST(TypedMessageCodecTest, UnequipReqBindingRoundTrip) {
  mir2::common::UnequipRequest input;
  input.slot = 2;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::UnequipReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.slot, expected.slot);
      });
}

TEST(TypedMessageCodecTest, UnequipRspBindingRoundTrip) {
  mir2::common::UnequipResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  input.slot = 2;
  input.item_id = 7001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::UnequipRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
      });
}

TEST(TypedMessageCodecTest, ChatReqBindingRoundTrip) {
  mir2::common::ChatRequest input;
  input.channel = mir2::proto::ChatChannel::WORLD;
  input.content = "hello world";
  input.target_id = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::ChatReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.channel, expected.channel);
        EXPECT_EQ(actual.content, expected.content);
        EXPECT_EQ(actual.target_id, expected.target_id);
      });
}

TEST(TypedMessageCodecTest, ChatRspBindingRoundTrip) {
  mir2::common::ChatResponse input;
  input.code = mir2::proto::ErrorCode::ERR_OK;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::ChatRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.code, expected.code);
      });
}

TEST(TypedMessageCodecTest, GuildCreateReqBindingRoundTrip) {
  mir2::common::GuildCreateRequest input;
  input.guild_name = "Crimson";
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildCreateReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.guild_name, expected.guild_name);
      });
}

TEST(TypedMessageCodecTest, GuildCreateRspBindingRoundTrip) {
  mir2::common::GuildCreateResponse input;
  input.success = true;
  input.error_code = 0;
  input.guild_id = 9001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildCreateRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.guild_id, expected.guild_id);
      });
}

TEST(TypedMessageCodecTest, GuildJoinReqBindingRoundTrip) {
  mir2::common::GuildJoinRequest input;
  input.guild_id = 9001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildJoinReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.guild_id, expected.guild_id);
      });
}

TEST(TypedMessageCodecTest, GuildJoinRspBindingRoundTrip) {
  mir2::common::GuildJoinResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildJoinRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildLeaveReqBindingRoundTrip) {
  mir2::common::GuildLeaveRequest input;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildLeaveReqBinding>(
      input, [](const auto&, const auto&) {});
}

TEST(TypedMessageCodecTest, GuildLeaveRspBindingRoundTrip) {
  mir2::common::GuildLeaveResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildLeaveRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildKickReqBindingRoundTrip) {
  mir2::common::GuildKickRequest input;
  input.target_character_id = 1234;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildKickReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_character_id, expected.target_character_id);
      });
}

TEST(TypedMessageCodecTest, GuildKickRspBindingRoundTrip) {
  mir2::common::GuildKickResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildKickRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildDeclareWarReqBindingRoundTrip) {
  mir2::common::GuildDeclareWarRequest input;
  input.target_guild_id = 2001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildDeclareWarReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_guild_id, expected.target_guild_id);
      });
}

TEST(TypedMessageCodecTest, GuildDeclareWarRspBindingRoundTrip) {
  mir2::common::GuildDeclareWarResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildDeclareWarRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildCancelWarReqBindingRoundTrip) {
  mir2::common::GuildCancelWarRequest input;
  input.target_guild_id = 2001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildCancelWarReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_guild_id, expected.target_guild_id);
      });
}

TEST(TypedMessageCodecTest, GuildCancelWarRspBindingRoundTrip) {
  mir2::common::GuildCancelWarResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildCancelWarRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildMakeAllyReqBindingRoundTrip) {
  mir2::common::GuildMakeAllyRequest input;
  input.target_guild_id = 3001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildMakeAllyReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_guild_id, expected.target_guild_id);
      });
}

TEST(TypedMessageCodecTest, GuildMakeAllyRspBindingRoundTrip) {
  mir2::common::GuildMakeAllyResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildMakeAllyRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildBreakAllyReqBindingRoundTrip) {
  mir2::common::GuildBreakAllyRequest input;
  input.target_guild_id = 3001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildBreakAllyReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_guild_id, expected.target_guild_id);
      });
}

TEST(TypedMessageCodecTest, GuildBreakAllyRspBindingRoundTrip) {
  mir2::common::GuildBreakAllyResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildBreakAllyRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, TradeReqBindingRoundTrip) {
  mir2::common::TradeRequest input;
  input.target_character_id = 2002;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_character_id, expected.target_character_id);
      });
}

TEST(TypedMessageCodecTest, TradeRspBindingRoundTrip) {
  mir2::common::TradeResponse input;
  input.success = true;
  input.error_code = 0;
  input.trade_id = 9001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.trade_id, expected.trade_id);
      });
}

TEST(TypedMessageCodecTest, TradeAddItemReqBindingRoundTrip) {
  mir2::common::TradeAddItemRequest input;
  input.trade_id = 9001;
  input.inventory_slot = 7;
  input.item_id = 30001;
  input.count = 2;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeAddItemReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.trade_id, expected.trade_id);
        EXPECT_EQ(actual.inventory_slot, expected.inventory_slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
        EXPECT_EQ(actual.count, expected.count);
      });
}

TEST(TypedMessageCodecTest, TradeAddItemRspBindingRoundTrip) {
  mir2::common::TradeAddItemResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeAddItemRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, TradeSetGoldReqBindingRoundTrip) {
  mir2::common::TradeSetGoldRequest input;
  input.trade_id = 9001;
  input.gold = 1234;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeSetGoldReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.trade_id, expected.trade_id);
        EXPECT_EQ(actual.gold, expected.gold);
      });
}

TEST(TypedMessageCodecTest, TradeSetGoldRspBindingRoundTrip) {
  mir2::common::TradeSetGoldResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeSetGoldRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, TradeConfirmReqBindingRoundTrip) {
  mir2::common::TradeConfirmRequest input;
  input.trade_id = 9001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeConfirmReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.trade_id, expected.trade_id);
      });
}

TEST(TypedMessageCodecTest, TradeConfirmRspBindingRoundTrip) {
  mir2::common::TradeConfirmResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeConfirmRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, TradeCancelReqBindingRoundTrip) {
  mir2::common::TradeCancelRequest input;
  input.trade_id = 9001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeCancelReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.trade_id, expected.trade_id);
      });
}

TEST(TypedMessageCodecTest, TradeCancelRspBindingRoundTrip) {
  mir2::common::TradeCancelResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeCancelRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, TradeUpdateBindingRoundTrip) {
  mir2::common::TradeUpdateMessage input;
  input.trade_id = 9001;
  input.left_character_id = 1001;
  input.right_character_id = 1002;
  input.left_gold = 300;
  input.right_gold = 400;
  input.left_confirmed = true;
  input.right_confirmed = false;
  input.left_items.push_back({1, 5001, 1});
  input.left_items.push_back({2, 5002, 2});
  input.right_items.push_back({3, 6001, 1});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeUpdateBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.trade_id, expected.trade_id);
        EXPECT_EQ(actual.left_character_id, expected.left_character_id);
        EXPECT_EQ(actual.right_character_id, expected.right_character_id);
        EXPECT_EQ(actual.left_gold, expected.left_gold);
        EXPECT_EQ(actual.right_gold, expected.right_gold);
        EXPECT_EQ(actual.left_confirmed, expected.left_confirmed);
        EXPECT_EQ(actual.right_confirmed, expected.right_confirmed);
        ASSERT_EQ(actual.left_items.size(), expected.left_items.size());
        ASSERT_EQ(actual.right_items.size(), expected.right_items.size());
        for (size_t i = 0; i < expected.left_items.size(); ++i) {
          EXPECT_EQ(actual.left_items[i].inventory_slot, expected.left_items[i].inventory_slot);
          EXPECT_EQ(actual.left_items[i].item_id, expected.left_items[i].item_id);
          EXPECT_EQ(actual.left_items[i].count, expected.left_items[i].count);
        }
        for (size_t i = 0; i < expected.right_items.size(); ++i) {
          EXPECT_EQ(actual.right_items[i].inventory_slot, expected.right_items[i].inventory_slot);
          EXPECT_EQ(actual.right_items[i].item_id, expected.right_items[i].item_id);
          EXPECT_EQ(actual.right_items[i].count, expected.right_items[i].count);
        }
      });
}

TEST(TypedMessageCodecTest, TradeCompleteBindingRoundTrip) {
  mir2::common::TradeCompleteMessage input;
  input.trade_id = 9001;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::TradeCompleteBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.trade_id, expected.trade_id);
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, PartyInviteReqBindingRoundTrip) {
  mir2::common::PartyInviteRequest input;
  input.target_character_id = 2002;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyInviteReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_character_id, expected.target_character_id);
      });
}

TEST(TypedMessageCodecTest, PartyInviteRspBindingRoundTrip) {
  mir2::common::PartyInviteResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyInviteRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, PartyJoinReqBindingRoundTrip) {
  mir2::common::PartyJoinRequest input;
  input.party_id = 7001;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyJoinReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.party_id, expected.party_id);
      });
}

TEST(TypedMessageCodecTest, PartyJoinRspBindingRoundTrip) {
  mir2::common::PartyJoinResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyJoinRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, PartyLeaveReqBindingRoundTrip) {
  mir2::common::PartyLeaveRequest input;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyLeaveReqBinding>(
      input, [](const auto&, const auto&) {});
}

TEST(TypedMessageCodecTest, PartyLeaveRspBindingRoundTrip) {
  mir2::common::PartyLeaveResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyLeaveRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, PartyKickReqBindingRoundTrip) {
  mir2::common::PartyKickRequest input;
  input.target_character_id = 2002;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyKickReqBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_character_id, expected.target_character_id);
      });
}

TEST(TypedMessageCodecTest, PartyKickRspBindingRoundTrip) {
  mir2::common::PartyKickResponse input;
  input.success = true;
  input.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyKickRspBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, PartyUpdateBindingRoundTrip) {
  mir2::common::PartyUpdateMessage input;
  input.party_id = 7001;
  input.leader_character_id = 1001;
  input.members.push_back({1001, "Leader", 100, 120, 1, 10, 10, true});
  input.members.push_back({1002, "Member", 90, 120, 1, 11, 10, true});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::PartyUpdateBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.party_id, expected.party_id);
        EXPECT_EQ(actual.leader_character_id, expected.leader_character_id);
        ASSERT_EQ(actual.members.size(), expected.members.size());
        for (size_t i = 0; i < expected.members.size(); ++i) {
          EXPECT_EQ(actual.members[i].character_id, expected.members[i].character_id);
          EXPECT_EQ(actual.members[i].name, expected.members[i].name);
          EXPECT_EQ(actual.members[i].hp, expected.members[i].hp);
          EXPECT_EQ(actual.members[i].max_hp, expected.members[i].max_hp);
          EXPECT_EQ(actual.members[i].map_id, expected.members[i].map_id);
          EXPECT_EQ(actual.members[i].x, expected.members[i].x);
          EXPECT_EQ(actual.members[i].y, expected.members[i].y);
          EXPECT_EQ(actual.members[i].online, expected.members[i].online);
        }
      });
}

TEST(TypedMessageCodecTest, GuildUpdateNoticeReqRspBindingRoundTrip) {
  mir2::common::GuildUpdateNoticeRequest req;
  req.notice_lines = {"line1", "line2"};
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildUpdateNoticeReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.notice_lines, expected.notice_lines);
      });

  mir2::common::GuildUpdateNoticeResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildUpdateNoticeRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildUpdateRankReqRspBindingRoundTrip) {
  mir2::common::GuildUpdateRankRequest req;
  req.members.push_back({1001, 1});
  req.members.push_back({1002, 2});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildUpdateRankReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        ASSERT_EQ(actual.members.size(), expected.members.size());
        for (size_t i = 0; i < expected.members.size(); ++i) {
          EXPECT_EQ(actual.members[i].character_id, expected.members[i].character_id);
          EXPECT_EQ(actual.members[i].rank, expected.members[i].rank);
        }
      });

  mir2::common::GuildUpdateRankResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildUpdateRankRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
      });
}

TEST(TypedMessageCodecTest, GuildInfoSyncBindingRoundTrip) {
  mir2::common::GuildInfoSyncMessage input;
  input.has_guild = true;
  input.guild_id = 9001;
  input.guild_name = "Crimson";
  input.level = 3;
  input.member_count = 12;
  input.leader_id = 1001;
  input.leader_name = "Leader";
  input.max_members = 40;
  input.notice_list = {"n1", "n2"};
  input.ranks.push_back({1, "Leader", {"Leader"}});
  input.war_guilds.push_back({9101, 1111, 2222});
  input.ally_guild_ids = {9201, 9202};
  input.allow_ally = true;
  input.in_team_fight = false;
  input.match_point = 88;
  input.fight_members = {"A", "B"};
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::GuildInfoSyncBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.has_guild, expected.has_guild);
        EXPECT_EQ(actual.guild_id, expected.guild_id);
        EXPECT_EQ(actual.guild_name, expected.guild_name);
        EXPECT_EQ(actual.level, expected.level);
        EXPECT_EQ(actual.member_count, expected.member_count);
        EXPECT_EQ(actual.leader_id, expected.leader_id);
        EXPECT_EQ(actual.leader_name, expected.leader_name);
        EXPECT_EQ(actual.max_members, expected.max_members);
        EXPECT_EQ(actual.notice_list, expected.notice_list);
        EXPECT_EQ(actual.ally_guild_ids, expected.ally_guild_ids);
        EXPECT_EQ(actual.allow_ally, expected.allow_ally);
        EXPECT_EQ(actual.in_team_fight, expected.in_team_fight);
        EXPECT_EQ(actual.match_point, expected.match_point);
        EXPECT_EQ(actual.fight_members, expected.fight_members);
        ASSERT_EQ(actual.ranks.size(), expected.ranks.size());
        ASSERT_EQ(actual.war_guilds.size(), expected.war_guilds.size());
      });
}

TEST(TypedMessageCodecTest, RankingReqRspBindingRoundTrip) {
  mir2::common::RankingRequest req;
  req.ranking_type = mir2::proto::RankingType::GOLD;
  req.page = 2;
  req.page_size = 50;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::RankingReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.ranking_type, expected.ranking_type);
        EXPECT_EQ(actual.page, expected.page);
        EXPECT_EQ(actual.page_size, expected.page_size);
      });

  mir2::common::RankingResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.ranking_type = mir2::proto::RankingType::LEVEL;
  rsp.total_count = 1;
  rsp.entries.push_back({1, 1001, "Alice", 12345, "extra"});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::RankingRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.ranking_type, expected.ranking_type);
        EXPECT_EQ(actual.total_count, expected.total_count);
        ASSERT_EQ(actual.entries.size(), expected.entries.size());
        EXPECT_EQ(actual.entries[0].rank, expected.entries[0].rank);
        EXPECT_EQ(actual.entries[0].entity_id, expected.entries[0].entity_id);
        EXPECT_EQ(actual.entries[0].name, expected.entries[0].name);
        EXPECT_EQ(actual.entries[0].value, expected.entries[0].value);
        EXPECT_EQ(actual.entries[0].extra, expected.entries[0].extra);
      });
}

TEST(TypedMessageCodecTest, RankingMyRankReqRspBindingRoundTrip) {
  mir2::common::RankingMyRankRequest req;
  req.ranking_type = mir2::proto::RankingType::PK;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::RankingMyRankReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.ranking_type, expected.ranking_type);
      });

  mir2::common::RankingMyRankResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.ranking_type = mir2::proto::RankingType::PK;
  rsp.rank = 9;
  rsp.value = 321;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::RankingMyRankRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.ranking_type, expected.ranking_type);
        EXPECT_EQ(actual.rank, expected.rank);
        EXPECT_EQ(actual.value, expected.value);
      });
}

TEST(TypedMessageCodecTest, MailSendReqRspBindingRoundTrip) {
  mir2::common::MailSendRequest req;
  req.target_character_id = 1002;
  req.subject = "subject";
  req.content = "content";
  req.gold = 88;
  req.items.push_back({5001, 2});
  req.expire_time = 12345;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailSendReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.target_character_id, expected.target_character_id);
        EXPECT_EQ(actual.subject, expected.subject);
        EXPECT_EQ(actual.content, expected.content);
        EXPECT_EQ(actual.gold, expected.gold);
        EXPECT_EQ(actual.expire_time, expected.expire_time);
        ASSERT_EQ(actual.items.size(), expected.items.size());
        EXPECT_EQ(actual.items[0].item_id, expected.items[0].item_id);
        EXPECT_EQ(actual.items[0].count, expected.items[0].count);
      });

  mir2::common::MailSendResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.mail_id = 9901;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailSendRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.mail_id, expected.mail_id);
      });
}

TEST(TypedMessageCodecTest, MailListReqRspBindingRoundTrip) {
  mir2::common::MailListRequest req;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailListReqBinding>(
      req, [](const auto&, const auto&) {});

  mir2::common::MailListResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.mails.push_back({1, 1001, "hello", true, false, false, 100, 200, 50, 1});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailListRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        ASSERT_EQ(actual.mails.size(), expected.mails.size());
        EXPECT_EQ(actual.mails[0].mail_id, expected.mails[0].mail_id);
        EXPECT_EQ(actual.mails[0].subject, expected.mails[0].subject);
      });
}

TEST(TypedMessageCodecTest, MailReadReqRspBindingRoundTrip) {
  mir2::common::MailReadRequest req;
  req.mail_id = 777;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailReadReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.mail_id, expected.mail_id);
      });

  mir2::common::MailReadResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.has_mail = true;
  rsp.mail.mail_id = 777;
  rsp.mail.from_character_id = 1001;
  rsp.mail.subject = "sub";
  rsp.mail.content = "detail";
  rsp.mail.is_read = true;
  rsp.mail.claimed = false;
  rsp.mail.send_time = 100;
  rsp.mail.expire_time = 200;
  rsp.mail.gold = 3;
  rsp.mail.items.push_back({5002, 1});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailReadRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.has_mail, expected.has_mail);
        EXPECT_EQ(actual.mail.mail_id, expected.mail.mail_id);
        EXPECT_EQ(actual.mail.subject, expected.mail.subject);
        EXPECT_EQ(actual.mail.content, expected.mail.content);
        ASSERT_EQ(actual.mail.items.size(), expected.mail.items.size());
      });
}

TEST(TypedMessageCodecTest, MailDeleteReqRspBindingRoundTrip) {
  mir2::common::MailDeleteRequest req;
  req.mail_id = 888;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailDeleteReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.mail_id, expected.mail_id);
      });

  mir2::common::MailDeleteResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.mail_id = 888;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailDeleteRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.mail_id, expected.mail_id);
      });
}

TEST(TypedMessageCodecTest, MailClaimReqRspBindingRoundTrip) {
  mir2::common::MailClaimRequest req;
  req.mail_id = 999;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailClaimReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.mail_id, expected.mail_id);
      });

  mir2::common::MailClaimResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.mail_id = 999;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailClaimRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.mail_id, expected.mail_id);
      });
}

TEST(TypedMessageCodecTest, MailNotifyBindingRoundTrip) {
  mir2::common::MailNotifyMessage input;
  input.has_mail = true;
  input.mail.mail_id = 1000;
  input.mail.from_character_id = 2000;
  input.mail.subject = "notify";
  input.mail.has_attachment = true;
  input.mail.is_read = false;
  input.mail.claimed = false;
  input.mail.send_time = 1;
  input.mail.expire_time = 2;
  input.mail.gold = 3;
  input.mail.attachment_count = 1;
  input.unread_count = 6;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::MailNotifyBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.has_mail, expected.has_mail);
        EXPECT_EQ(actual.mail.mail_id, expected.mail.mail_id);
        EXPECT_EQ(actual.mail.subject, expected.mail.subject);
        EXPECT_EQ(actual.unread_count, expected.unread_count);
      });
}

TEST(TypedMessageCodecTest, AchievementListReqRspBindingRoundTrip) {
  mir2::common::AchievementListRequest req;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AchievementListReqBinding>(
      req, [](const auto&, const auto&) {});

  mir2::common::AchievementListResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.achievements.push_back({1, 3, 10, false, false, 0, 100});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AchievementListRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        ASSERT_EQ(actual.achievements.size(), expected.achievements.size());
        EXPECT_EQ(actual.achievements[0].achievement_id,
                  expected.achievements[0].achievement_id);
      });
}

TEST(TypedMessageCodecTest, AchievementClaimReqRspBindingRoundTrip) {
  mir2::common::AchievementClaimRequest req;
  req.achievement_id = 8;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AchievementClaimReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.achievement_id, expected.achievement_id);
      });

  mir2::common::AchievementClaimResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.achievement_id = 8;
  rsp.reward_gold = 300;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AchievementClaimRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.achievement_id, expected.achievement_id);
        EXPECT_EQ(actual.reward_gold, expected.reward_gold);
      });
}

TEST(TypedMessageCodecTest, AchievementUpdateBindingRoundTrip) {
  mir2::common::AchievementUpdateMessage input;
  input.has_achievement = true;
  input.achievement = {88, 7, 10, false, false, 0, 100};
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AchievementUpdateBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.has_achievement, expected.has_achievement);
        EXPECT_EQ(actual.achievement.achievement_id, expected.achievement.achievement_id);
        EXPECT_EQ(actual.achievement.progress, expected.achievement.progress);
      });
}

TEST(TypedMessageCodecTest, AuctionListReqRspBindingRoundTrip) {
  mir2::common::AuctionListRequest req;
  req.page = 3;
  req.page_size = 30;
  req.seller_only = true;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionListReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.page, expected.page);
        EXPECT_EQ(actual.page_size, expected.page_size);
        EXPECT_EQ(actual.seller_only, expected.seller_only);
      });

  mir2::common::AuctionListResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.total_count = 1;
  rsp.listings.push_back({1001, 2001, 3001, 2, 50, 100, 10, 20, false, false});
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionListRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.total_count, expected.total_count);
        ASSERT_EQ(actual.listings.size(), expected.listings.size());
        EXPECT_EQ(actual.listings[0].listing_id, expected.listings[0].listing_id);
        EXPECT_EQ(actual.listings[0].item_id, expected.listings[0].item_id);
      });
}

TEST(TypedMessageCodecTest, AuctionSellReqRspBindingRoundTrip) {
  mir2::common::AuctionSellRequest req;
  req.inventory_slot = 2;
  req.item_id = 5001;
  req.count = 3;
  req.unit_price = 200;
  req.duration_sec = 3600;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionSellReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.inventory_slot, expected.inventory_slot);
        EXPECT_EQ(actual.item_id, expected.item_id);
        EXPECT_EQ(actual.count, expected.count);
        EXPECT_EQ(actual.unit_price, expected.unit_price);
        EXPECT_EQ(actual.duration_sec, expected.duration_sec);
      });

  mir2::common::AuctionSellResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.listing_id = 8888;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionSellRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.listing_id, expected.listing_id);
      });
}

TEST(TypedMessageCodecTest, AuctionBuyReqRspBindingRoundTrip) {
  mir2::common::AuctionBuyRequest req;
  req.listing_id = 9999;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionBuyReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.listing_id, expected.listing_id);
      });

  mir2::common::AuctionBuyResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.listing_id = 9999;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionBuyRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.listing_id, expected.listing_id);
      });
}

TEST(TypedMessageCodecTest, AuctionCancelReqRspBindingRoundTrip) {
  mir2::common::AuctionCancelRequest req;
  req.listing_id = 1234;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionCancelReqBinding>(
      req, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.listing_id, expected.listing_id);
      });

  mir2::common::AuctionCancelResponse rsp;
  rsp.success = true;
  rsp.error_code = 0;
  rsp.listing_id = 1234;
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionCancelRspBinding>(
      rsp, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.success, expected.success);
        EXPECT_EQ(actual.error_code, expected.error_code);
        EXPECT_EQ(actual.listing_id, expected.listing_id);
      });
}

TEST(TypedMessageCodecTest, AuctionNotifyBindingRoundTrip) {
  mir2::common::AuctionNotifyMessage input;
  input.notify_type = mir2::proto::AuctionNotifyType::BOUGHT;
  input.has_listing = true;
  input.listing = {1234, 2002, 5005, 1, 777, 777, 10, 20, false, false};
  ExpectTypedRoundTrip<mir2::common::protocol::bindings::AuctionNotifyBinding>(
      input, [](const auto& actual, const auto& expected) {
        EXPECT_EQ(actual.notify_type, expected.notify_type);
        EXPECT_EQ(actual.has_listing, expected.has_listing);
        EXPECT_EQ(actual.listing.listing_id, expected.listing.listing_id);
        EXPECT_EQ(actual.listing.item_id, expected.listing.item_id);
      });
}

TEST(TypedMessageCodecTest, LoginReqBindingDecodesEmptyStrings) {
  flatbuffers::FlatBufferBuilder builder;
  const auto username = builder.CreateString("");
  const auto password = builder.CreateString("");
  const auto req = mir2::proto::CreateLoginReq(builder, username, password, 0);
  builder.Finish(req);

  const auto* payload = builder.GetBufferPointer();
  const auto size = builder.GetSize();

  EXPECT_TRUE(mir2::common::protocol::ValidateTypedPayload<
              mir2::common::protocol::bindings::LoginReqBinding>(payload, size));
  const auto decoded = mir2::common::protocol::DecodeTypedPayload<
      mir2::common::protocol::bindings::LoginReqBinding>(payload, size);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->username.empty());
  EXPECT_TRUE(decoded->password.empty());

  const auto status = mir2::common::ValidateLoginRequest(decoded.value());
  EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);
}

TEST(TypedMessageCodecTest, DecodeTypedPayloadRejectsCorruptedPayload) {
  std::vector<uint8_t> invalid_payload = {0x01, 0x02, 0x03, 0x04};
  EXPECT_FALSE(mir2::common::protocol::ValidateTypedPayload<
               mir2::common::protocol::bindings::MoveReqBinding>(
      invalid_payload.data(), invalid_payload.size()));
  EXPECT_FALSE(mir2::common::protocol::DecodeTypedPayload<
               mir2::common::protocol::bindings::MoveReqBinding>(
                   invalid_payload.data(), invalid_payload.size())
                   .has_value());
}
