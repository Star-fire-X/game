/**
 * @file typed_message_bindings.h
 * @brief Canonical typed FlatBuffers bindings for message_codec-managed messages.
 */

#ifndef MIR2_COMMON_PROTOCOL_TYPED_MESSAGE_BINDINGS_H_
#define MIR2_COMMON_PROTOCOL_TYPED_MESSAGE_BINDINGS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "combat_generated.h"
#include "common/protocol/message_codec.h"
#include "common/protocol/typed_message_codec.h"
#include "game_generated.h"
#include "guild_generated.h"
#include "item_generated.h"
#include "login_generated.h"
#include "ranking_generated.h"
#include "mail_generated.h"
#include "achievement_generated.h"
#include "auction_generated.h"
#include "party_generated.h"
#include "trade_generated.h"

namespace mir2::common::protocol::bindings {

template <mir2::common::MsgId Id>
struct HasTypedBinding : std::false_type {};

template <mir2::common::MsgId Id>
inline constexpr bool kHasTypedBinding = HasTypedBinding<Id>::value;

struct LoginReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kLoginReq,
          mir2::proto::LoginReq,
          mir2::common::LoginRequest> {
  static bool FromFbs(const mir2::proto::LoginReq& fbs, Native* out) {
    if (!out || !fbs.username() || !fbs.password()) {
      return false;
    }
    out->username = fbs.username()->str();
    out->password = fbs.password()->str();
    out->version = fbs.version() ? fbs.version()->str() : "";
    return true;
  }

  static flatbuffers::Offset<mir2::proto::LoginReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    const auto username = builder.CreateString(native.username);
    const auto password = builder.CreateString(native.password);
    const auto version = native.version.empty()
                             ? 0
                             : builder.CreateString(native.version);
    return mir2::proto::CreateLoginReq(builder, username, password, version);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kLoginReq> : std::true_type {};

struct LoginRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kLoginRsp,
          mir2::proto::LoginRsp,
          mir2::common::LoginResponse> {
  static bool FromFbs(const mir2::proto::LoginRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->account_id = fbs.account_id();
    out->session_token = fbs.session_token() ? fbs.session_token()->str() : "";
    return true;
  }

  static flatbuffers::Offset<mir2::proto::LoginRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    const auto token = native.session_token.empty()
                           ? 0
                           : builder.CreateString(native.session_token);
    return mir2::proto::CreateLoginRsp(
        builder, native.code, native.account_id, token);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kLoginRsp> : std::true_type {};

struct CreateRoleReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kCreateRoleReq,
          mir2::proto::CreateRoleReq,
          mir2::common::CreateCharacterRequest> {
  static bool FromFbs(const mir2::proto::CreateRoleReq& fbs, Native* out) {
    if (!out || !fbs.name()) {
      return false;
    }
    out->name = fbs.name()->str();
    out->profession = fbs.profession();
    out->gender = fbs.gender();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::CreateRoleReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    const auto name = builder.CreateString(native.name);
    return mir2::proto::CreateCreateRoleReq(
        builder, name, native.profession, native.gender);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kCreateRoleReq> : std::true_type {};

struct CreateRoleRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kCreateRoleRsp,
          mir2::proto::CreateRoleRsp,
          mir2::common::CreateCharacterResponse> {
  static bool FromFbs(const mir2::proto::CreateRoleRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->player_id = fbs.player_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::CreateRoleRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateCreateRoleRsp(
        builder, native.code, native.player_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kCreateRoleRsp> : std::true_type {};

struct MoveReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMoveReq,
          mir2::proto::MoveReq,
          mir2::common::MoveRequest> {
  static bool FromFbs(const mir2::proto::MoveReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_x = fbs.target_x();
    out->target_y = fbs.target_y();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MoveReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMoveReq(builder, native.target_x, native.target_y);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMoveReq> : std::true_type {};

struct MoveRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMoveRsp,
          mir2::proto::MoveRsp,
          mir2::common::MoveResponse> {
  static bool FromFbs(const mir2::proto::MoveRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->x = fbs.x();
    out->y = fbs.y();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MoveRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMoveRsp(builder, native.code, native.x, native.y);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMoveRsp> : std::true_type {};

struct AttackReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAttackReq,
          mir2::proto::AttackReq,
          mir2::common::AttackRequest> {
  static bool FromFbs(const mir2::proto::AttackReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_id = fbs.target_id();
    out->target_type = fbs.target_type();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AttackReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAttackReq(
        builder, native.target_id, native.target_type);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAttackReq> : std::true_type {};

struct AttackRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAttackRsp,
          mir2::proto::AttackRsp,
          mir2::common::AttackResponse> {
  static bool FromFbs(const mir2::proto::AttackRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->attacker_id = fbs.attacker_id();
    out->target_id = fbs.target_id();
    out->damage = fbs.damage();
    out->target_hp = fbs.target_hp();
    out->target_dead = fbs.target_dead();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AttackRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAttackRsp(
        builder, native.code, native.attacker_id, native.target_id,
        native.damage, native.target_hp, native.target_dead);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAttackRsp> : std::true_type {};

struct SkillReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kSkillReq,
          mir2::proto::SkillReq,
          mir2::common::SkillRequest> {
  static bool FromFbs(const mir2::proto::SkillReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->skill_id = fbs.skill_id();
    out->target_id = fbs.target_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::SkillReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateSkillReq(builder, native.skill_id, native.target_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kSkillReq> : std::true_type {};

struct SkillRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kSkillRsp,
          mir2::proto::SkillRsp,
          mir2::common::SkillResponse> {
  static bool FromFbs(const mir2::proto::SkillRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->caster_id = fbs.caster_id();
    out->target_id = fbs.target_id();
    out->damage = fbs.damage();
    out->healing = fbs.healing();
    out->target_dead = fbs.target_dead();
    out->skill_id = fbs.skill_id();
    out->result = fbs.result();
    out->cooldown_ms = fbs.cooldown_ms();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::SkillRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateSkillRsp(builder,
                                       native.code,
                                       native.caster_id,
                                       native.target_id,
                                       native.damage,
                                       native.healing,
                                       native.target_dead,
                                       native.skill_id,
                                       native.result,
                                       native.cooldown_ms);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kSkillRsp> : std::true_type {};

struct UseItemReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kUseItemReq,
          mir2::proto::UseItemReq,
          mir2::common::UseItemRequest> {
  static bool FromFbs(const mir2::proto::UseItemReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->slot = fbs.slot();
    out->item_id = fbs.item_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UseItemReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateUseItemReq(builder, native.slot, native.item_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kUseItemReq> : std::true_type {};

struct UseItemRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kUseItemRsp,
          mir2::proto::UseItemRsp,
          mir2::common::UseItemResponse> {
  static bool FromFbs(const mir2::proto::UseItemRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->slot = fbs.slot();
    out->item_id = fbs.item_id();
    out->remaining = fbs.remaining();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UseItemRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateUseItemRsp(
        builder, native.code, native.slot, native.item_id, native.remaining);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kUseItemRsp> : std::true_type {};

struct EquipReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kEquipReq,
          mir2::proto::EquipReq,
          mir2::common::EquipRequest> {
  static bool FromFbs(const mir2::proto::EquipReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->slot = fbs.slot();
    out->item_id = fbs.item_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::EquipReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateEquipReq(builder, native.slot, native.item_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kEquipReq> : std::true_type {};

struct EquipRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kEquipRsp,
          mir2::proto::EquipRsp,
          mir2::common::EquipResponse> {
  static bool FromFbs(const mir2::proto::EquipRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->slot = fbs.slot();
    out->item_id = fbs.item_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::EquipRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateEquipRsp(
        builder, native.code, native.slot, native.item_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kEquipRsp> : std::true_type {};

struct UnequipReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kUnequipReq,
          mir2::proto::UnequipReq,
          mir2::common::UnequipRequest> {
  static bool FromFbs(const mir2::proto::UnequipReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->slot = fbs.slot();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UnequipReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateUnequipReq(builder, native.slot);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kUnequipReq> : std::true_type {};

struct UnequipRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kUnequipRsp,
          mir2::proto::UnequipRsp,
          mir2::common::UnequipResponse> {
  static bool FromFbs(const mir2::proto::UnequipRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->slot = fbs.slot();
    out->item_id = fbs.item_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UnequipRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateUnequipRsp(
        builder, native.code, native.slot, native.item_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kUnequipRsp> : std::true_type {};

struct ChatReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kChatReq,
          mir2::proto::ChatReq,
          mir2::common::ChatRequest> {
  static bool FromFbs(const mir2::proto::ChatReq& fbs, Native* out) {
    if (!out || !fbs.content()) {
      return false;
    }
    out->channel = fbs.channel();
    out->content = fbs.content()->str();
    out->target_id = fbs.target_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::ChatReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    const auto content = builder.CreateString(native.content);
    return mir2::proto::CreateChatReq(
        builder, native.channel, content, native.target_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kChatReq> : std::true_type {};

struct ChatRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kChatRsp,
          mir2::proto::ChatRsp,
          mir2::common::ChatResponse> {
  static bool FromFbs(const mir2::proto::ChatRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::ChatRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateChatRsp(builder, native.code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kChatRsp> : std::true_type {};

struct GuildCreateReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildCreateReq,
          mir2::proto::CreateGuildRequest,
          mir2::common::GuildCreateRequest> {
  static bool FromFbs(const mir2::proto::CreateGuildRequest& fbs, Native* out) {
    if (!out || !fbs.guild_name()) {
      return false;
    }
    out->guild_name = fbs.guild_name()->str();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::CreateGuildRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    const auto guild_name = builder.CreateString(native.guild_name);
    return mir2::proto::CreateCreateGuildRequest(builder, guild_name);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildCreateReq> : std::true_type {};

struct GuildCreateRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildCreateRsp,
          mir2::proto::CreateGuildResponse,
          mir2::common::GuildCreateResponse> {
  static bool FromFbs(const mir2::proto::CreateGuildResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->guild_id = fbs.guild_info() ? fbs.guild_info()->id() : 0;
    return true;
  }

  static flatbuffers::Offset<mir2::proto::CreateGuildResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    flatbuffers::Offset<mir2::proto::GuildInfo> guild_info_offset = 0;
    if (native.guild_id != 0) {
      guild_info_offset = mir2::proto::CreateGuildInfo(builder, native.guild_id);
    }
    return mir2::proto::CreateCreateGuildResponse(
        builder, native.success, native.error_code, guild_info_offset);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildCreateRsp> : std::true_type {};

struct GuildJoinReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildJoinReq,
          mir2::proto::JoinGuildRequest,
          mir2::common::GuildJoinRequest> {
  static bool FromFbs(const mir2::proto::JoinGuildRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->guild_id = fbs.guild_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::JoinGuildRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateJoinGuildRequest(builder, native.guild_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildJoinReq> : std::true_type {};

struct GuildJoinRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildJoinRsp,
          mir2::proto::JoinGuildResponse,
          mir2::common::GuildJoinResponse> {
  static bool FromFbs(const mir2::proto::JoinGuildResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::JoinGuildResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateJoinGuildResponse(
        builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildJoinRsp> : std::true_type {};

struct GuildLeaveReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildLeaveReq,
          mir2::proto::LeaveGuildRequest,
          mir2::common::GuildLeaveRequest> {
  static bool FromFbs(const mir2::proto::LeaveGuildRequest&,
                      Native* out) {
    return out != nullptr;
  }

  static flatbuffers::Offset<mir2::proto::LeaveGuildRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native&) {
    return mir2::proto::CreateLeaveGuildRequest(builder);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildLeaveReq> : std::true_type {};

struct GuildLeaveRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildLeaveRsp,
          mir2::proto::LeaveGuildResponse,
          mir2::common::GuildLeaveResponse> {
  static bool FromFbs(const mir2::proto::LeaveGuildResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::LeaveGuildResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateLeaveGuildResponse(
        builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildLeaveRsp> : std::true_type {};

struct PickupItemReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPickupItemReq,
          mir2::proto::PickupItemReq,
          mir2::common::PickupItemRequest> {
  static bool FromFbs(const mir2::proto::PickupItemReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->item_id = fbs.item_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PickupItemReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePickupItemReq(builder, native.item_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPickupItemReq> : std::true_type {};

struct PickupItemRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPickupItemRsp,
          mir2::proto::PickupItemRsp,
          mir2::common::PickupItemResponse> {
  static bool FromFbs(const mir2::proto::PickupItemRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->item_id = fbs.item_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PickupItemRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePickupItemRsp(builder, native.code, native.item_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPickupItemRsp> : std::true_type {};

struct DropItemReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kDropItemReq,
          mir2::proto::DropItemReq,
          mir2::common::DropItemRequest> {
  static bool FromFbs(const mir2::proto::DropItemReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->slot = fbs.slot();
    out->item_id = fbs.item_id();
    out->count = fbs.count();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::DropItemReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateDropItemReq(
        builder, native.slot, native.item_id, native.count);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kDropItemReq> : std::true_type {};

struct DropItemRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kDropItemRsp,
          mir2::proto::DropItemRsp,
          mir2::common::DropItemResponse> {
  static bool FromFbs(const mir2::proto::DropItemRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->code = fbs.code();
    out->item_id = fbs.item_id();
    out->count = fbs.count();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::DropItemRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateDropItemRsp(builder, native.code, native.item_id, native.count);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kDropItemRsp> : std::true_type {};

struct GuildKickReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildKickReq,
          mir2::proto::KickGuildRequest,
          mir2::common::GuildKickRequest> {
  static bool FromFbs(const mir2::proto::KickGuildRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_character_id = fbs.target_character_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::KickGuildRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateKickGuildRequest(builder, native.target_character_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildKickReq> : std::true_type {};

struct GuildKickRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildKickRsp,
          mir2::proto::KickGuildResponse,
          mir2::common::GuildKickResponse> {
  static bool FromFbs(const mir2::proto::KickGuildResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::KickGuildResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateKickGuildResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildKickRsp> : std::true_type {};

struct GuildDeclareWarReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildDeclareWarReq,
          mir2::proto::DeclareWarRequest,
          mir2::common::GuildDeclareWarRequest> {
  static bool FromFbs(const mir2::proto::DeclareWarRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_guild_id = fbs.target_guild_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::DeclareWarRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateDeclareWarRequest(builder, native.target_guild_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildDeclareWarReq> : std::true_type {};

struct GuildDeclareWarRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildDeclareWarRsp,
          mir2::proto::DeclareWarResponse,
          mir2::common::GuildDeclareWarResponse> {
  static bool FromFbs(const mir2::proto::DeclareWarResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::DeclareWarResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateDeclareWarResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildDeclareWarRsp> : std::true_type {};

struct GuildCancelWarReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildCancelWarReq,
          mir2::proto::CancelWarRequest,
          mir2::common::GuildCancelWarRequest> {
  static bool FromFbs(const mir2::proto::CancelWarRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_guild_id = fbs.target_guild_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::CancelWarRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateCancelWarRequest(builder, native.target_guild_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildCancelWarReq> : std::true_type {};

struct GuildCancelWarRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildCancelWarRsp,
          mir2::proto::CancelWarResponse,
          mir2::common::GuildCancelWarResponse> {
  static bool FromFbs(const mir2::proto::CancelWarResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::CancelWarResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateCancelWarResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildCancelWarRsp> : std::true_type {};

struct GuildMakeAllyReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildMakeAllyReq,
          mir2::proto::MakeAllianceRequest,
          mir2::common::GuildMakeAllyRequest> {
  static bool FromFbs(const mir2::proto::MakeAllianceRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_guild_id = fbs.target_guild_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MakeAllianceRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMakeAllianceRequest(builder, native.target_guild_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildMakeAllyReq> : std::true_type {};

struct GuildMakeAllyRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildMakeAllyRsp,
          mir2::proto::MakeAllianceResponse,
          mir2::common::GuildMakeAllyResponse> {
  static bool FromFbs(const mir2::proto::MakeAllianceResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MakeAllianceResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMakeAllianceResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildMakeAllyRsp> : std::true_type {};

struct GuildBreakAllyReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildBreakAllyReq,
          mir2::proto::BreakAllianceRequest,
          mir2::common::GuildBreakAllyRequest> {
  static bool FromFbs(const mir2::proto::BreakAllianceRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_guild_id = fbs.target_guild_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::BreakAllianceRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateBreakAllianceRequest(builder, native.target_guild_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildBreakAllyReq> : std::true_type {};

struct GuildBreakAllyRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildBreakAllyRsp,
          mir2::proto::BreakAllianceResponse,
          mir2::common::GuildBreakAllyResponse> {
  static bool FromFbs(const mir2::proto::BreakAllianceResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::BreakAllianceResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateBreakAllianceResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildBreakAllyRsp> : std::true_type {};

struct TradeReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeReq,
          mir2::proto::TradeReq,
          mir2::common::TradeRequest> {
  static bool FromFbs(const mir2::proto::TradeReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_character_id = fbs.target_character_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeReq(builder, native.target_character_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeReq> : std::true_type {};

struct TradeRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeRsp,
          mir2::proto::TradeRsp,
          mir2::common::TradeResponse> {
  static bool FromFbs(const mir2::proto::TradeRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->trade_id = fbs.trade_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeRsp(
        builder, native.success, native.error_code, native.trade_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeRsp> : std::true_type {};

struct TradeAddItemReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeAddItemReq,
          mir2::proto::TradeAddItemReq,
          mir2::common::TradeAddItemRequest> {
  static bool FromFbs(const mir2::proto::TradeAddItemReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->trade_id = fbs.trade_id();
    out->inventory_slot = fbs.inventory_slot();
    out->item_id = fbs.item_id();
    out->count = fbs.count();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeAddItemReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeAddItemReq(
        builder, native.trade_id, native.inventory_slot, native.item_id, native.count);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeAddItemReq> : std::true_type {};

struct TradeAddItemRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeAddItemRsp,
          mir2::proto::TradeAddItemRsp,
          mir2::common::TradeAddItemResponse> {
  static bool FromFbs(const mir2::proto::TradeAddItemRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeAddItemRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeAddItemRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeAddItemRsp> : std::true_type {};

struct TradeSetGoldReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeSetGoldReq,
          mir2::proto::TradeSetGoldReq,
          mir2::common::TradeSetGoldRequest> {
  static bool FromFbs(const mir2::proto::TradeSetGoldReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->trade_id = fbs.trade_id();
    out->gold = fbs.gold();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeSetGoldReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeSetGoldReq(builder, native.trade_id, native.gold);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeSetGoldReq> : std::true_type {};

struct TradeSetGoldRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeSetGoldRsp,
          mir2::proto::TradeSetGoldRsp,
          mir2::common::TradeSetGoldResponse> {
  static bool FromFbs(const mir2::proto::TradeSetGoldRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeSetGoldRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeSetGoldRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeSetGoldRsp> : std::true_type {};

struct TradeConfirmReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeConfirmReq,
          mir2::proto::TradeConfirmReq,
          mir2::common::TradeConfirmRequest> {
  static bool FromFbs(const mir2::proto::TradeConfirmReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->trade_id = fbs.trade_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeConfirmReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeConfirmReq(builder, native.trade_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeConfirmReq> : std::true_type {};

struct TradeConfirmRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeConfirmRsp,
          mir2::proto::TradeConfirmRsp,
          mir2::common::TradeConfirmResponse> {
  static bool FromFbs(const mir2::proto::TradeConfirmRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeConfirmRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeConfirmRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeConfirmRsp> : std::true_type {};

struct TradeCancelReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeCancelReq,
          mir2::proto::TradeCancelReq,
          mir2::common::TradeCancelRequest> {
  static bool FromFbs(const mir2::proto::TradeCancelReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->trade_id = fbs.trade_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeCancelReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeCancelReq(builder, native.trade_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeCancelReq> : std::true_type {};

struct TradeCancelRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeCancelRsp,
          mir2::proto::TradeCancelRsp,
          mir2::common::TradeCancelResponse> {
  static bool FromFbs(const mir2::proto::TradeCancelRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeCancelRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeCancelRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeCancelRsp> : std::true_type {};

struct TradeUpdateBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeUpdate,
          mir2::proto::TradeUpdate,
          mir2::common::TradeUpdateMessage> {
  static bool FromFbs(const mir2::proto::TradeUpdate& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->trade_id = fbs.trade_id();
    out->left_character_id = fbs.left_character_id();
    out->right_character_id = fbs.right_character_id();
    out->left_gold = fbs.left_gold();
    out->right_gold = fbs.right_gold();
    out->left_confirmed = fbs.left_confirmed();
    out->right_confirmed = fbs.right_confirmed();
    out->left_items.clear();
    out->right_items.clear();

    if (const auto* left_items = fbs.left_items()) {
      out->left_items.reserve(left_items->size());
      for (const auto* item : *left_items) {
        if (!item) {
          continue;
        }
        mir2::common::TradeItemInfo info;
        info.inventory_slot = item->inventory_slot();
        info.item_id = item->item_id();
        info.count = item->count();
        out->left_items.push_back(info);
      }
    }

    if (const auto* right_items = fbs.right_items()) {
      out->right_items.reserve(right_items->size());
      for (const auto* item : *right_items) {
        if (!item) {
          continue;
        }
        mir2::common::TradeItemInfo info;
        info.inventory_slot = item->inventory_slot();
        info.item_id = item->item_id();
        info.count = item->count();
        out->right_items.push_back(info);
      }
    }

    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeUpdate> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> left_offsets;
    left_offsets.reserve(native.left_items.size());
    for (const auto& item : native.left_items) {
      left_offsets.push_back(mir2::proto::CreateTradeItemInfo(
          builder, item.inventory_slot, item.item_id, item.count));
    }

    std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> right_offsets;
    right_offsets.reserve(native.right_items.size());
    for (const auto& item : native.right_items) {
      right_offsets.push_back(mir2::proto::CreateTradeItemInfo(
          builder, item.inventory_slot, item.item_id, item.count));
    }

    const auto left_vec = builder.CreateVector(left_offsets);
    const auto right_vec = builder.CreateVector(right_offsets);
    return mir2::proto::CreateTradeUpdate(builder,
                                          native.trade_id,
                                          native.left_character_id,
                                          native.right_character_id,
                                          left_vec,
                                          right_vec,
                                          native.left_gold,
                                          native.right_gold,
                                          native.left_confirmed,
                                          native.right_confirmed);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeUpdate> : std::true_type {};

struct TradeCompleteBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kTradeComplete,
          mir2::proto::TradeComplete,
          mir2::common::TradeCompleteMessage> {
  static bool FromFbs(const mir2::proto::TradeComplete& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->trade_id = fbs.trade_id();
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::TradeComplete> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateTradeComplete(
        builder, native.trade_id, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kTradeComplete> : std::true_type {};

struct PartyInviteReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyInviteReq,
          mir2::proto::PartyInviteReq,
          mir2::common::PartyInviteRequest> {
  static bool FromFbs(const mir2::proto::PartyInviteReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_character_id = fbs.target_character_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyInviteReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyInviteReq(builder, native.target_character_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyInviteReq> : std::true_type {};

struct PartyInviteRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyInviteRsp,
          mir2::proto::PartyInviteRsp,
          mir2::common::PartyInviteResponse> {
  static bool FromFbs(const mir2::proto::PartyInviteRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyInviteRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyInviteRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyInviteRsp> : std::true_type {};

struct PartyJoinReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyJoinReq,
          mir2::proto::PartyJoinReq,
          mir2::common::PartyJoinRequest> {
  static bool FromFbs(const mir2::proto::PartyJoinReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->party_id = fbs.party_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyJoinReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyJoinReq(builder, native.party_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyJoinReq> : std::true_type {};

struct PartyJoinRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyJoinRsp,
          mir2::proto::PartyJoinRsp,
          mir2::common::PartyJoinResponse> {
  static bool FromFbs(const mir2::proto::PartyJoinRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyJoinRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyJoinRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyJoinRsp> : std::true_type {};

struct PartyLeaveReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyLeaveReq,
          mir2::proto::PartyLeaveReq,
          mir2::common::PartyLeaveRequest> {
  static bool FromFbs(const mir2::proto::PartyLeaveReq&, Native* out) {
    return out != nullptr;
  }

  static flatbuffers::Offset<mir2::proto::PartyLeaveReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native&) {
    return mir2::proto::CreatePartyLeaveReq(builder);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyLeaveReq> : std::true_type {};

struct PartyLeaveRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyLeaveRsp,
          mir2::proto::PartyLeaveRsp,
          mir2::common::PartyLeaveResponse> {
  static bool FromFbs(const mir2::proto::PartyLeaveRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyLeaveRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyLeaveRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyLeaveRsp> : std::true_type {};

struct PartyKickReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyKickReq,
          mir2::proto::PartyKickReq,
          mir2::common::PartyKickRequest> {
  static bool FromFbs(const mir2::proto::PartyKickReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_character_id = fbs.target_character_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyKickReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyKickReq(builder, native.target_character_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyKickReq> : std::true_type {};

struct PartyKickRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyKickRsp,
          mir2::proto::PartyKickRsp,
          mir2::common::PartyKickResponse> {
  static bool FromFbs(const mir2::proto::PartyKickRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyKickRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreatePartyKickRsp(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyKickRsp> : std::true_type {};

struct PartyUpdateBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kPartyUpdate,
          mir2::proto::PartyUpdate,
          mir2::common::PartyUpdateMessage> {
  static bool FromFbs(const mir2::proto::PartyUpdate& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->party_id = fbs.party_id();
    out->leader_character_id = fbs.leader_character_id();
    out->members.clear();
    if (const auto* members = fbs.members()) {
      out->members.reserve(members->size());
      for (const auto* member : *members) {
        if (!member) {
          continue;
        }
        mir2::common::PartyMemberInfo info;
        info.character_id = member->character_id();
        info.name = member->name() ? member->name()->str() : std::string{};
        info.hp = member->hp();
        info.max_hp = member->max_hp();
        info.map_id = member->map_id();
        info.x = member->x();
        info.y = member->y();
        info.online = member->online();
        out->members.push_back(std::move(info));
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::PartyUpdate> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::PartyMemberInfo>> member_offsets;
    member_offsets.reserve(native.members.size());
    for (const auto& member : native.members) {
      const auto name = builder.CreateString(member.name);
      member_offsets.push_back(mir2::proto::CreatePartyMemberInfo(
          builder,
          member.character_id,
          name,
          member.hp,
          member.max_hp,
          member.map_id,
          member.x,
          member.y,
          member.online));
    }
    const auto members = builder.CreateVector(member_offsets);
    return mir2::proto::CreatePartyUpdate(
        builder, native.party_id, native.leader_character_id, members);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kPartyUpdate> : std::true_type {};

struct GuildUpdateNoticeReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildUpdateNoticeReq,
          mir2::proto::UpdateNoticeRequest,
          mir2::common::GuildUpdateNoticeRequest> {
  static bool FromFbs(const mir2::proto::UpdateNoticeRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->notice_lines.clear();
    if (const auto* lines = fbs.notice_lines()) {
      out->notice_lines.reserve(lines->size());
      for (const auto* line : *lines) {
        out->notice_lines.push_back(line ? line->str() : std::string{});
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UpdateNoticeRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<flatbuffers::String>> lines;
    lines.reserve(native.notice_lines.size());
    for (const auto& line : native.notice_lines) {
      lines.push_back(builder.CreateString(line));
    }
    const auto line_vec = builder.CreateVector(lines);
    return mir2::proto::CreateUpdateNoticeRequest(builder, line_vec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildUpdateNoticeReq> : std::true_type {};

struct GuildUpdateNoticeRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildUpdateNoticeRsp,
          mir2::proto::UpdateNoticeResponse,
          mir2::common::GuildUpdateNoticeResponse> {
  static bool FromFbs(const mir2::proto::UpdateNoticeResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UpdateNoticeResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateUpdateNoticeResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildUpdateNoticeRsp> : std::true_type {};

struct GuildUpdateRankReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildUpdateRankReq,
          mir2::proto::UpdateRankRequest,
          mir2::common::GuildUpdateRankRequest> {
  static bool FromFbs(const mir2::proto::UpdateRankRequest& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->members.clear();
    if (const auto* members = fbs.members()) {
      out->members.reserve(members->size());
      for (const auto* member : *members) {
        if (!member) {
          continue;
        }
        out->members.push_back(
            mir2::common::GuildRankUpdateMember{
                .character_id = member->character_id(),
                .rank = member->rank(),
            });
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UpdateRankRequest> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::RankUpdateMember>> members;
    members.reserve(native.members.size());
    for (const auto& member : native.members) {
      members.push_back(
          mir2::proto::CreateRankUpdateMember(builder, member.character_id, member.rank));
    }
    const auto member_vec = builder.CreateVector(members);
    return mir2::proto::CreateUpdateRankRequest(builder, member_vec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildUpdateRankReq> : std::true_type {};

struct GuildUpdateRankRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildUpdateRankRsp,
          mir2::proto::UpdateRankResponse,
          mir2::common::GuildUpdateRankResponse> {
  static bool FromFbs(const mir2::proto::UpdateRankResponse& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::UpdateRankResponse> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateUpdateRankResponse(builder, native.success, native.error_code);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildUpdateRankRsp> : std::true_type {};

struct GuildInfoSyncBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kGuildInfoSync,
          mir2::proto::GuildInfoSync,
          mir2::common::GuildInfoSyncMessage> {
  static bool FromFbs(const mir2::proto::GuildInfoSync& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->has_guild = false;
    out->notice_list.clear();
    out->ranks.clear();
    out->war_guilds.clear();
    out->ally_guild_ids.clear();
    out->fight_members.clear();

    const auto* guild = fbs.guild_info();
    if (!guild) {
      return true;
    }
    out->has_guild = true;
    out->guild_id = guild->id();
    out->guild_name = guild->name() ? guild->name()->str() : std::string{};
    out->level = guild->level();
    out->member_count = guild->member_count();
    out->leader_id = guild->leader_id();
    out->leader_name = guild->leader_name() ? guild->leader_name()->str() : std::string{};
    out->max_members = guild->max_members();
    out->allow_ally = guild->allow_ally();
    out->in_team_fight = guild->in_team_fight();
    out->match_point = guild->match_point();

    if (const auto* notices = guild->notice_list()) {
      out->notice_list.reserve(notices->size());
      for (const auto* notice : *notices) {
        out->notice_list.push_back(notice ? notice->str() : std::string{});
      }
    }

    if (const auto* ranks = guild->ranks()) {
      out->ranks.reserve(ranks->size());
      for (const auto* rank : *ranks) {
        if (!rank) {
          continue;
        }
        mir2::common::GuildRankInfoData rank_info;
        rank_info.rank = rank->rank();
        rank_info.rank_name = rank->rank_name() ? rank->rank_name()->str() : std::string{};
        if (const auto* members = rank->members()) {
          rank_info.members.reserve(members->size());
          for (const auto* member : *members) {
            rank_info.members.push_back(member ? member->str() : std::string{});
          }
        }
        out->ranks.push_back(std::move(rank_info));
      }
    }

    if (const auto* wars = guild->war_guilds()) {
      out->war_guilds.reserve(wars->size());
      for (const auto* war : *wars) {
        if (!war) {
          continue;
        }
        out->war_guilds.push_back(
            mir2::common::GuildWarInfoData{
                .enemy_guild_id = war->enemy_guild_id(),
                .start_time = war->start_time(),
                .remain_time = war->remain_time(),
            });
      }
    }

    if (const auto* allies = guild->ally_guild_ids()) {
      out->ally_guild_ids.reserve(allies->size());
      for (const auto ally : *allies) {
        out->ally_guild_ids.push_back(ally);
      }
    }

    if (const auto* fight_members = guild->fight_members()) {
      out->fight_members.reserve(fight_members->size());
      for (const auto* name : *fight_members) {
        out->fight_members.push_back(name ? name->str() : std::string{});
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::GuildInfoSync> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    flatbuffers::Offset<mir2::proto::GuildInfo> guild_offset = 0;
    if (native.has_guild) {
      const auto guild_name = builder.CreateString(native.guild_name);
      const auto leader_name = builder.CreateString(native.leader_name);

      std::vector<flatbuffers::Offset<flatbuffers::String>> notice_offsets;
      notice_offsets.reserve(native.notice_list.size());
      for (const auto& notice : native.notice_list) {
        notice_offsets.push_back(builder.CreateString(notice));
      }
      const auto notices = builder.CreateVector(notice_offsets);

      std::vector<flatbuffers::Offset<mir2::proto::GuildRankInfo>> rank_offsets;
      rank_offsets.reserve(native.ranks.size());
      for (const auto& rank : native.ranks) {
        std::vector<flatbuffers::Offset<flatbuffers::String>> members;
        members.reserve(rank.members.size());
        for (const auto& member : rank.members) {
          members.push_back(builder.CreateString(member));
        }
        const auto member_vec = builder.CreateVector(members);
        const auto rank_name = builder.CreateString(rank.rank_name);
        rank_offsets.push_back(
            mir2::proto::CreateGuildRankInfo(builder, rank.rank, rank_name, member_vec));
      }
      const auto ranks = builder.CreateVector(rank_offsets);

      std::vector<flatbuffers::Offset<mir2::proto::GuildWarInfo>> war_offsets;
      war_offsets.reserve(native.war_guilds.size());
      for (const auto& war : native.war_guilds) {
        war_offsets.push_back(
            mir2::proto::CreateGuildWarInfo(builder, war.enemy_guild_id, war.start_time, war.remain_time));
      }
      const auto wars = builder.CreateVector(war_offsets);
      const auto allies = builder.CreateVector(native.ally_guild_ids);

      std::vector<flatbuffers::Offset<flatbuffers::String>> fight_members;
      fight_members.reserve(native.fight_members.size());
      for (const auto& name : native.fight_members) {
        fight_members.push_back(builder.CreateString(name));
      }
      const auto fight_member_vec = builder.CreateVector(fight_members);

      guild_offset = mir2::proto::CreateGuildInfo(builder,
                                                  native.guild_id,
                                                  guild_name,
                                                  native.level,
                                                  native.member_count,
                                                  native.leader_id,
                                                  leader_name,
                                                  native.max_members,
                                                  notices,
                                                  ranks,
                                                  wars,
                                                  allies,
                                                  native.allow_ally,
                                                  native.in_team_fight,
                                                  native.match_point,
                                                  fight_member_vec);
    }
    return mir2::proto::CreateGuildInfoSync(builder, guild_offset);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kGuildInfoSync> : std::true_type {};

struct RankingReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kRankingReq,
          mir2::proto::RankingReq,
          mir2::common::RankingRequest> {
  static bool FromFbs(const mir2::proto::RankingReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->ranking_type = fbs.ranking_type();
    out->page = fbs.page();
    out->page_size = fbs.page_size();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::RankingReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateRankingReq(
        builder, native.ranking_type, native.page, native.page_size);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kRankingReq> : std::true_type {};

struct RankingRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kRankingRsp,
          mir2::proto::RankingRsp,
          mir2::common::RankingResponse> {
  static bool FromFbs(const mir2::proto::RankingRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->ranking_type = fbs.ranking_type();
    out->total_count = fbs.total_count();
    out->entries.clear();
    if (const auto* entries = fbs.entries()) {
      out->entries.reserve(entries->size());
      for (const auto* entry : *entries) {
        if (!entry) {
          continue;
        }
        out->entries.push_back(
            mir2::common::RankingEntryInfo{
                .rank = entry->rank(),
                .entity_id = entry->entity_id(),
                .name = entry->name() ? entry->name()->str() : std::string{},
                .value = entry->value(),
                .extra = entry->extra() ? entry->extra()->str() : std::string{},
            });
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::RankingRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::RankEntry>> entries;
    entries.reserve(native.entries.size());
    for (const auto& entry : native.entries) {
      const auto name = builder.CreateString(entry.name);
      const auto extra = builder.CreateString(entry.extra);
      entries.push_back(
          mir2::proto::CreateRankEntry(
              builder, entry.rank, entry.entity_id, name, entry.value, extra));
    }
    const auto entry_vec = builder.CreateVector(entries);
    return mir2::proto::CreateRankingRsp(builder,
                                         native.success,
                                         native.error_code,
                                         native.ranking_type,
                                         native.total_count,
                                         entry_vec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kRankingRsp> : std::true_type {};

struct RankingMyRankReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kRankingMyRankReq,
          mir2::proto::RankingMyRankReq,
          mir2::common::RankingMyRankRequest> {
  static bool FromFbs(const mir2::proto::RankingMyRankReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->ranking_type = fbs.ranking_type();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::RankingMyRankReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateRankingMyRankReq(builder, native.ranking_type);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kRankingMyRankReq> : std::true_type {};

struct RankingMyRankRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kRankingMyRankRsp,
          mir2::proto::RankingMyRankRsp,
          mir2::common::RankingMyRankResponse> {
  static bool FromFbs(const mir2::proto::RankingMyRankRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->ranking_type = fbs.ranking_type();
    out->rank = fbs.rank();
    out->value = fbs.value();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::RankingMyRankRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateRankingMyRankRsp(
        builder,
        native.success,
        native.error_code,
        native.ranking_type,
        native.rank,
        native.value);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kRankingMyRankRsp> : std::true_type {};

struct MailSendReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailSendReq,
          mir2::proto::MailSendReq,
          mir2::common::MailSendRequest> {
  static bool FromFbs(const mir2::proto::MailSendReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->target_character_id = fbs.target_character_id();
    out->subject = fbs.subject() ? fbs.subject()->str() : std::string{};
    out->content = fbs.content() ? fbs.content()->str() : std::string{};
    out->gold = fbs.gold();
    out->expire_time = fbs.expire_time();
    out->items.clear();
    if (const auto* items = fbs.items()) {
      out->items.reserve(items->size());
      for (const auto* item : *items) {
        if (!item) {
          continue;
        }
        out->items.push_back(
            mir2::common::MailAttachmentItemInfo{
                .item_id = item->item_id(),
                .count = item->count(),
            });
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailSendReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    const auto subject = builder.CreateString(native.subject);
    const auto content = builder.CreateString(native.content);
    std::vector<flatbuffers::Offset<mir2::proto::MailAttachmentItem>> items;
    items.reserve(native.items.size());
    for (const auto& item : native.items) {
      items.push_back(mir2::proto::CreateMailAttachmentItem(builder, item.item_id, item.count));
    }
    const auto item_vec = builder.CreateVector(items);
    return mir2::proto::CreateMailSendReq(builder,
                                          native.target_character_id,
                                          subject,
                                          content,
                                          native.gold,
                                          item_vec,
                                          native.expire_time);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailSendReq> : std::true_type {};

struct MailSendRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailSendRsp,
          mir2::proto::MailSendRsp,
          mir2::common::MailSendResponse> {
  static bool FromFbs(const mir2::proto::MailSendRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->mail_id = fbs.mail_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailSendRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMailSendRsp(
        builder, native.success, native.error_code, native.mail_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailSendRsp> : std::true_type {};

struct MailListReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailListReq,
          mir2::proto::MailListReq,
          mir2::common::MailListRequest> {
  static bool FromFbs(const mir2::proto::MailListReq&, Native* out) {
    return out != nullptr;
  }

  static flatbuffers::Offset<mir2::proto::MailListReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native&) {
    return mir2::proto::CreateMailListReq(builder);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailListReq> : std::true_type {};

struct MailListRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailListRsp,
          mir2::proto::MailListRsp,
          mir2::common::MailListResponse> {
  static bool FromFbs(const mir2::proto::MailListRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->mails.clear();
    if (const auto* mails = fbs.mails()) {
      out->mails.reserve(mails->size());
      for (const auto* mail : *mails) {
        if (!mail) {
          continue;
        }
        out->mails.push_back(
            mir2::common::MailSummaryInfo{
                .mail_id = mail->mail_id(),
                .from_character_id = mail->from_character_id(),
                .subject = mail->subject() ? mail->subject()->str() : std::string{},
                .has_attachment = mail->has_attachment(),
                .is_read = mail->is_read(),
                .claimed = mail->claimed(),
                .send_time = mail->send_time(),
                .expire_time = mail->expire_time(),
                .gold = mail->gold(),
                .attachment_count = mail->attachment_count(),
            });
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailListRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::MailSummary>> mails;
    mails.reserve(native.mails.size());
    for (const auto& mail : native.mails) {
      const auto subject = builder.CreateString(mail.subject);
      mails.push_back(mir2::proto::CreateMailSummary(builder,
                                                     mail.mail_id,
                                                     mail.from_character_id,
                                                     subject,
                                                     mail.has_attachment,
                                                     mail.is_read,
                                                     mail.claimed,
                                                     mail.send_time,
                                                     mail.expire_time,
                                                     mail.gold,
                                                     mail.attachment_count));
    }
    const auto mail_vec = builder.CreateVector(mails);
    return mir2::proto::CreateMailListRsp(builder, native.success, native.error_code, mail_vec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailListRsp> : std::true_type {};

struct MailReadReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailReadReq,
          mir2::proto::MailReadReq,
          mir2::common::MailReadRequest> {
  static bool FromFbs(const mir2::proto::MailReadReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->mail_id = fbs.mail_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailReadReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMailReadReq(builder, native.mail_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailReadReq> : std::true_type {};

struct MailReadRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailReadRsp,
          mir2::proto::MailReadRsp,
          mir2::common::MailReadResponse> {
  static bool FromFbs(const mir2::proto::MailReadRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->has_mail = false;
    out->mail = {};
    if (const auto* mail = fbs.mail()) {
      out->has_mail = true;
      out->mail.mail_id = mail->mail_id();
      out->mail.from_character_id = mail->from_character_id();
      out->mail.subject = mail->subject() ? mail->subject()->str() : std::string{};
      out->mail.content = mail->content() ? mail->content()->str() : std::string{};
      out->mail.is_read = mail->is_read();
      out->mail.claimed = mail->claimed();
      out->mail.send_time = mail->send_time();
      out->mail.expire_time = mail->expire_time();
      out->mail.gold = mail->gold();
      out->mail.items.clear();
      if (const auto* items = mail->items()) {
        out->mail.items.reserve(items->size());
        for (const auto* item : *items) {
          if (!item) {
            continue;
          }
          out->mail.items.push_back(
              mir2::common::MailAttachmentItemInfo{
                  .item_id = item->item_id(),
                  .count = item->count(),
              });
        }
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailReadRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    flatbuffers::Offset<mir2::proto::MailDetail> mail_offset = 0;
    if (native.has_mail) {
      std::vector<flatbuffers::Offset<mir2::proto::MailAttachmentItem>> items;
      items.reserve(native.mail.items.size());
      for (const auto& item : native.mail.items) {
        items.push_back(
            mir2::proto::CreateMailAttachmentItem(builder, item.item_id, item.count));
      }
      const auto item_vec = builder.CreateVector(items);
      const auto subject = builder.CreateString(native.mail.subject);
      const auto content = builder.CreateString(native.mail.content);
      mail_offset = mir2::proto::CreateMailDetail(builder,
                                                  native.mail.mail_id,
                                                  native.mail.from_character_id,
                                                  subject,
                                                  content,
                                                  native.mail.is_read,
                                                  native.mail.claimed,
                                                  native.mail.send_time,
                                                  native.mail.expire_time,
                                                  native.mail.gold,
                                                  item_vec);
    }
    return mir2::proto::CreateMailReadRsp(
        builder, native.success, native.error_code, mail_offset);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailReadRsp> : std::true_type {};

struct MailDeleteReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailDeleteReq,
          mir2::proto::MailDeleteReq,
          mir2::common::MailDeleteRequest> {
  static bool FromFbs(const mir2::proto::MailDeleteReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->mail_id = fbs.mail_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailDeleteReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMailDeleteReq(builder, native.mail_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailDeleteReq> : std::true_type {};

struct MailDeleteRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailDeleteRsp,
          mir2::proto::MailDeleteRsp,
          mir2::common::MailDeleteResponse> {
  static bool FromFbs(const mir2::proto::MailDeleteRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->mail_id = fbs.mail_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailDeleteRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMailDeleteRsp(
        builder, native.success, native.error_code, native.mail_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailDeleteRsp> : std::true_type {};

struct MailClaimReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailClaimReq,
          mir2::proto::MailClaimReq,
          mir2::common::MailClaimRequest> {
  static bool FromFbs(const mir2::proto::MailClaimReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->mail_id = fbs.mail_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailClaimReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMailClaimReq(builder, native.mail_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailClaimReq> : std::true_type {};

struct MailClaimRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailClaimRsp,
          mir2::proto::MailClaimRsp,
          mir2::common::MailClaimResponse> {
  static bool FromFbs(const mir2::proto::MailClaimRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->mail_id = fbs.mail_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailClaimRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateMailClaimRsp(
        builder, native.success, native.error_code, native.mail_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailClaimRsp> : std::true_type {};

struct MailNotifyBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kMailNotify,
          mir2::proto::MailNotify,
          mir2::common::MailNotifyMessage> {
  static bool FromFbs(const mir2::proto::MailNotify& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->has_mail = false;
    out->mail = {};
    out->unread_count = fbs.unread_count();
    if (const auto* mail = fbs.mail()) {
      out->has_mail = true;
      out->mail.mail_id = mail->mail_id();
      out->mail.from_character_id = mail->from_character_id();
      out->mail.subject = mail->subject() ? mail->subject()->str() : std::string{};
      out->mail.has_attachment = mail->has_attachment();
      out->mail.is_read = mail->is_read();
      out->mail.claimed = mail->claimed();
      out->mail.send_time = mail->send_time();
      out->mail.expire_time = mail->expire_time();
      out->mail.gold = mail->gold();
      out->mail.attachment_count = mail->attachment_count();
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::MailNotify> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    flatbuffers::Offset<mir2::proto::MailSummary> summary = 0;
    if (native.has_mail) {
      const auto subject = builder.CreateString(native.mail.subject);
      summary = mir2::proto::CreateMailSummary(builder,
                                               native.mail.mail_id,
                                               native.mail.from_character_id,
                                               subject,
                                               native.mail.has_attachment,
                                               native.mail.is_read,
                                               native.mail.claimed,
                                               native.mail.send_time,
                                               native.mail.expire_time,
                                               native.mail.gold,
                                               native.mail.attachment_count);
    }
    return mir2::proto::CreateMailNotify(builder, summary, native.unread_count);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kMailNotify> : std::true_type {};

struct AchievementListReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAchievementListReq,
          mir2::proto::AchievementListReq,
          mir2::common::AchievementListRequest> {
  static bool FromFbs(const mir2::proto::AchievementListReq&, Native* out) {
    return out != nullptr;
  }

  static flatbuffers::Offset<mir2::proto::AchievementListReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native&) {
    return mir2::proto::CreateAchievementListReq(builder);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAchievementListReq> : std::true_type {};

struct AchievementListRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAchievementListRsp,
          mir2::proto::AchievementListRsp,
          mir2::common::AchievementListResponse> {
  static bool FromFbs(const mir2::proto::AchievementListRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->achievements.clear();
    if (const auto* achievements = fbs.achievements()) {
      out->achievements.reserve(achievements->size());
      for (const auto* achievement : *achievements) {
        if (!achievement) {
          continue;
        }
        out->achievements.push_back(
            mir2::common::AchievementProgressInfo{
                .achievement_id = achievement->achievement_id(),
                .progress = achievement->progress(),
                .target = achievement->target(),
                .completed = achievement->completed(),
                .claimed = achievement->claimed(),
                .completed_time = achievement->completed_time(),
                .reward_gold = achievement->reward_gold(),
            });
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AchievementListRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::AchievementProgress>> achievements;
    achievements.reserve(native.achievements.size());
    for (const auto& achievement : native.achievements) {
      achievements.push_back(
          mir2::proto::CreateAchievementProgress(builder,
                                                 achievement.achievement_id,
                                                 achievement.progress,
                                                 achievement.target,
                                                 achievement.completed,
                                                 achievement.claimed,
                                                 achievement.completed_time,
                                                 achievement.reward_gold));
    }
    const auto achievement_vec = builder.CreateVector(achievements);
    return mir2::proto::CreateAchievementListRsp(
        builder, native.success, native.error_code, achievement_vec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAchievementListRsp> : std::true_type {};

struct AchievementClaimReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAchievementClaimReq,
          mir2::proto::AchievementClaimReq,
          mir2::common::AchievementClaimRequest> {
  static bool FromFbs(const mir2::proto::AchievementClaimReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->achievement_id = fbs.achievement_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AchievementClaimReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAchievementClaimReq(builder, native.achievement_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAchievementClaimReq> : std::true_type {};

struct AchievementClaimRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAchievementClaimRsp,
          mir2::proto::AchievementClaimRsp,
          mir2::common::AchievementClaimResponse> {
  static bool FromFbs(const mir2::proto::AchievementClaimRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->achievement_id = fbs.achievement_id();
    out->reward_gold = fbs.reward_gold();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AchievementClaimRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAchievementClaimRsp(builder,
                                                  native.success,
                                                  native.error_code,
                                                  native.achievement_id,
                                                  native.reward_gold);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAchievementClaimRsp> : std::true_type {};

struct AchievementUpdateBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAchievementUpdate,
          mir2::proto::AchievementUpdate,
          mir2::common::AchievementUpdateMessage> {
  static bool FromFbs(const mir2::proto::AchievementUpdate& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->has_achievement = false;
    out->achievement = {};
    if (const auto* achievement = fbs.achievement()) {
      out->has_achievement = true;
      out->achievement.achievement_id = achievement->achievement_id();
      out->achievement.progress = achievement->progress();
      out->achievement.target = achievement->target();
      out->achievement.completed = achievement->completed();
      out->achievement.claimed = achievement->claimed();
      out->achievement.completed_time = achievement->completed_time();
      out->achievement.reward_gold = achievement->reward_gold();
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AchievementUpdate> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    flatbuffers::Offset<mir2::proto::AchievementProgress> achievement = 0;
    if (native.has_achievement) {
      achievement = mir2::proto::CreateAchievementProgress(builder,
                                                           native.achievement.achievement_id,
                                                           native.achievement.progress,
                                                           native.achievement.target,
                                                           native.achievement.completed,
                                                           native.achievement.claimed,
                                                           native.achievement.completed_time,
                                                           native.achievement.reward_gold);
    }
    return mir2::proto::CreateAchievementUpdate(builder, achievement);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAchievementUpdate> : std::true_type {};

struct AuctionListReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionListReq,
          mir2::proto::AuctionListReq,
          mir2::common::AuctionListRequest> {
  static bool FromFbs(const mir2::proto::AuctionListReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->page = fbs.page();
    out->page_size = fbs.page_size();
    out->seller_only = fbs.seller_only();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionListReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionListReq(
        builder, native.page, native.page_size, native.seller_only);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionListReq> : std::true_type {};

struct AuctionListRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionListRsp,
          mir2::proto::AuctionListRsp,
          mir2::common::AuctionListResponse> {
  static bool FromFbs(const mir2::proto::AuctionListRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->total_count = fbs.total_count();
    out->listings.clear();
    if (const auto* listings = fbs.listings()) {
      out->listings.reserve(listings->size());
      for (const auto* listing : *listings) {
        if (!listing) {
          continue;
        }
        out->listings.push_back(
            mir2::common::AuctionListingInfo{
                .listing_id = listing->listing_id(),
                .seller_character_id = listing->seller_character_id(),
                .item_id = listing->item_id(),
                .count = listing->count(),
                .unit_price = listing->unit_price(),
                .total_price = listing->total_price(),
                .created_at_ms = listing->created_at_ms(),
                .expires_at_ms = listing->expires_at_ms(),
                .sold = listing->sold(),
                .cancelled = listing->cancelled(),
            });
      }
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionListRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    std::vector<flatbuffers::Offset<mir2::proto::AuctionListing>> listings;
    listings.reserve(native.listings.size());
    for (const auto& listing : native.listings) {
      listings.push_back(mir2::proto::CreateAuctionListing(builder,
                                                           listing.listing_id,
                                                           listing.seller_character_id,
                                                           listing.item_id,
                                                           listing.count,
                                                           listing.unit_price,
                                                           listing.total_price,
                                                           listing.created_at_ms,
                                                           listing.expires_at_ms,
                                                           listing.sold,
                                                           listing.cancelled));
    }
    const auto listing_vec = builder.CreateVector(listings);
    return mir2::proto::CreateAuctionListRsp(
        builder, native.success, native.error_code, native.total_count, listing_vec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionListRsp> : std::true_type {};

struct AuctionSellReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionSellReq,
          mir2::proto::AuctionSellReq,
          mir2::common::AuctionSellRequest> {
  static bool FromFbs(const mir2::proto::AuctionSellReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->inventory_slot = fbs.inventory_slot();
    out->item_id = fbs.item_id();
    out->count = fbs.count();
    out->unit_price = fbs.unit_price();
    out->duration_sec = fbs.duration_sec();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionSellReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionSellReq(builder,
                                             native.inventory_slot,
                                             native.item_id,
                                             native.count,
                                             native.unit_price,
                                             native.duration_sec);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionSellReq> : std::true_type {};

struct AuctionSellRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionSellRsp,
          mir2::proto::AuctionSellRsp,
          mir2::common::AuctionSellResponse> {
  static bool FromFbs(const mir2::proto::AuctionSellRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->listing_id = fbs.listing_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionSellRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionSellRsp(
        builder, native.success, native.error_code, native.listing_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionSellRsp> : std::true_type {};

struct AuctionBuyReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionBuyReq,
          mir2::proto::AuctionBuyReq,
          mir2::common::AuctionBuyRequest> {
  static bool FromFbs(const mir2::proto::AuctionBuyReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->listing_id = fbs.listing_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionBuyReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionBuyReq(builder, native.listing_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionBuyReq> : std::true_type {};

struct AuctionBuyRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionBuyRsp,
          mir2::proto::AuctionBuyRsp,
          mir2::common::AuctionBuyResponse> {
  static bool FromFbs(const mir2::proto::AuctionBuyRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->listing_id = fbs.listing_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionBuyRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionBuyRsp(
        builder, native.success, native.error_code, native.listing_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionBuyRsp> : std::true_type {};

struct AuctionCancelReqBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionCancelReq,
          mir2::proto::AuctionCancelReq,
          mir2::common::AuctionCancelRequest> {
  static bool FromFbs(const mir2::proto::AuctionCancelReq& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->listing_id = fbs.listing_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionCancelReq> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionCancelReq(builder, native.listing_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionCancelReq> : std::true_type {};

struct AuctionCancelRspBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionCancelRsp,
          mir2::proto::AuctionCancelRsp,
          mir2::common::AuctionCancelResponse> {
  static bool FromFbs(const mir2::proto::AuctionCancelRsp& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->success = fbs.success();
    out->error_code = fbs.error_code();
    out->listing_id = fbs.listing_id();
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionCancelRsp> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    return mir2::proto::CreateAuctionCancelRsp(
        builder, native.success, native.error_code, native.listing_id);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionCancelRsp> : std::true_type {};

struct AuctionNotifyBinding final
    : public mir2::common::protocol::MessageBinding<
          mir2::common::MsgId::kAuctionNotify,
          mir2::proto::AuctionNotify,
          mir2::common::AuctionNotifyMessage> {
  static bool FromFbs(const mir2::proto::AuctionNotify& fbs, Native* out) {
    if (!out) {
      return false;
    }
    out->notify_type = fbs.notify_type();
    out->has_listing = false;
    out->listing = {};
    if (const auto* listing = fbs.listing()) {
      out->has_listing = true;
      out->listing.listing_id = listing->listing_id();
      out->listing.seller_character_id = listing->seller_character_id();
      out->listing.item_id = listing->item_id();
      out->listing.count = listing->count();
      out->listing.unit_price = listing->unit_price();
      out->listing.total_price = listing->total_price();
      out->listing.created_at_ms = listing->created_at_ms();
      out->listing.expires_at_ms = listing->expires_at_ms();
      out->listing.sold = listing->sold();
      out->listing.cancelled = listing->cancelled();
    }
    return true;
  }

  static flatbuffers::Offset<mir2::proto::AuctionNotify> ToFbs(
      flatbuffers::FlatBufferBuilder& builder,
      const Native& native) {
    flatbuffers::Offset<mir2::proto::AuctionListing> listing = 0;
    if (native.has_listing) {
      listing = mir2::proto::CreateAuctionListing(builder,
                                                  native.listing.listing_id,
                                                  native.listing.seller_character_id,
                                                  native.listing.item_id,
                                                  native.listing.count,
                                                  native.listing.unit_price,
                                                  native.listing.total_price,
                                                  native.listing.created_at_ms,
                                                  native.listing.expires_at_ms,
                                                  native.listing.sold,
                                                  native.listing.cancelled);
    }
    return mir2::proto::CreateAuctionNotify(builder, native.notify_type, listing);
  }
};
template <>
struct HasTypedBinding<mir2::common::MsgId::kAuctionNotify> : std::true_type {};

inline constexpr std::array<mir2::common::MsgId, 93> kTypedBindingMsgIds = {
    mir2::common::MsgId::kLoginReq,
    mir2::common::MsgId::kLoginRsp,
    mir2::common::MsgId::kCreateRoleReq,
    mir2::common::MsgId::kCreateRoleRsp,
    mir2::common::MsgId::kMoveReq,
    mir2::common::MsgId::kMoveRsp,
    mir2::common::MsgId::kAttackReq,
    mir2::common::MsgId::kAttackRsp,
    mir2::common::MsgId::kSkillReq,
    mir2::common::MsgId::kSkillRsp,
    mir2::common::MsgId::kUseItemReq,
    mir2::common::MsgId::kUseItemRsp,
    mir2::common::MsgId::kPickupItemReq,
    mir2::common::MsgId::kPickupItemRsp,
    mir2::common::MsgId::kDropItemReq,
    mir2::common::MsgId::kDropItemRsp,
    mir2::common::MsgId::kEquipReq,
    mir2::common::MsgId::kEquipRsp,
    mir2::common::MsgId::kUnequipReq,
    mir2::common::MsgId::kUnequipRsp,
    mir2::common::MsgId::kChatReq,
    mir2::common::MsgId::kChatRsp,
    mir2::common::MsgId::kGuildCreateReq,
    mir2::common::MsgId::kGuildCreateRsp,
    mir2::common::MsgId::kGuildJoinReq,
    mir2::common::MsgId::kGuildJoinRsp,
    mir2::common::MsgId::kGuildLeaveReq,
    mir2::common::MsgId::kGuildLeaveRsp,
    mir2::common::MsgId::kGuildKickReq,
    mir2::common::MsgId::kGuildKickRsp,
    mir2::common::MsgId::kGuildDeclareWarReq,
    mir2::common::MsgId::kGuildDeclareWarRsp,
    mir2::common::MsgId::kGuildCancelWarReq,
    mir2::common::MsgId::kGuildCancelWarRsp,
    mir2::common::MsgId::kGuildMakeAllyReq,
    mir2::common::MsgId::kGuildMakeAllyRsp,
    mir2::common::MsgId::kGuildBreakAllyReq,
    mir2::common::MsgId::kGuildBreakAllyRsp,
    mir2::common::MsgId::kTradeReq,
    mir2::common::MsgId::kTradeRsp,
    mir2::common::MsgId::kTradeAddItemReq,
    mir2::common::MsgId::kTradeAddItemRsp,
    mir2::common::MsgId::kTradeSetGoldReq,
    mir2::common::MsgId::kTradeSetGoldRsp,
    mir2::common::MsgId::kTradeConfirmReq,
    mir2::common::MsgId::kTradeConfirmRsp,
    mir2::common::MsgId::kTradeCancelReq,
    mir2::common::MsgId::kTradeCancelRsp,
    mir2::common::MsgId::kTradeUpdate,
    mir2::common::MsgId::kTradeComplete,
    mir2::common::MsgId::kPartyInviteReq,
    mir2::common::MsgId::kPartyInviteRsp,
    mir2::common::MsgId::kPartyJoinReq,
    mir2::common::MsgId::kPartyJoinRsp,
    mir2::common::MsgId::kPartyLeaveReq,
    mir2::common::MsgId::kPartyLeaveRsp,
    mir2::common::MsgId::kPartyKickReq,
    mir2::common::MsgId::kPartyKickRsp,
    mir2::common::MsgId::kPartyUpdate,
    mir2::common::MsgId::kGuildUpdateNoticeReq,
    mir2::common::MsgId::kGuildUpdateNoticeRsp,
    mir2::common::MsgId::kGuildUpdateRankReq,
    mir2::common::MsgId::kGuildUpdateRankRsp,
    mir2::common::MsgId::kGuildInfoSync,
    mir2::common::MsgId::kRankingReq,
    mir2::common::MsgId::kRankingRsp,
    mir2::common::MsgId::kRankingMyRankReq,
    mir2::common::MsgId::kRankingMyRankRsp,
    mir2::common::MsgId::kMailSendReq,
    mir2::common::MsgId::kMailSendRsp,
    mir2::common::MsgId::kMailListReq,
    mir2::common::MsgId::kMailListRsp,
    mir2::common::MsgId::kMailReadReq,
    mir2::common::MsgId::kMailReadRsp,
    mir2::common::MsgId::kMailDeleteReq,
    mir2::common::MsgId::kMailDeleteRsp,
    mir2::common::MsgId::kMailClaimReq,
    mir2::common::MsgId::kMailClaimRsp,
    mir2::common::MsgId::kMailNotify,
    mir2::common::MsgId::kAchievementListReq,
    mir2::common::MsgId::kAchievementListRsp,
    mir2::common::MsgId::kAchievementClaimReq,
    mir2::common::MsgId::kAchievementClaimRsp,
    mir2::common::MsgId::kAchievementUpdate,
    mir2::common::MsgId::kAuctionListReq,
    mir2::common::MsgId::kAuctionListRsp,
    mir2::common::MsgId::kAuctionSellReq,
    mir2::common::MsgId::kAuctionSellRsp,
    mir2::common::MsgId::kAuctionBuyReq,
    mir2::common::MsgId::kAuctionBuyRsp,
    mir2::common::MsgId::kAuctionCancelReq,
    mir2::common::MsgId::kAuctionCancelRsp,
    mir2::common::MsgId::kAuctionNotify,
};

inline constexpr int kTypedCodecCoverageGatePercent = 100;

template <size_t N>
constexpr bool HasNoDuplicateMsgIds(
    const std::array<mir2::common::MsgId, N>& msg_ids) {
  for (size_t i = 0; i < N; ++i) {
    for (size_t j = i + 1; j < N; ++j) {
      if (msg_ids[i] == msg_ids[j]) {
        return false;
      }
    }
  }
  return true;
}

template <size_t N>
constexpr bool ContainsMsgId(const std::array<mir2::common::MsgId, N>& msg_ids,
                             mir2::common::MsgId msg_id) {
  for (const auto candidate : msg_ids) {
    if (candidate == msg_id) {
      return true;
    }
  }
  return false;
}

template <size_t L, size_t R>
constexpr bool IsSubset(const std::array<mir2::common::MsgId, L>& lhs,
                        const std::array<mir2::common::MsgId, R>& rhs) {
  for (const auto msg_id : lhs) {
    if (!ContainsMsgId(rhs, msg_id)) {
      return false;
    }
  }
  return true;
}

static_assert(HasNoDuplicateMsgIds(kTypedBindingMsgIds),
              "Typed message binding matrix contains duplicate msg ids.");
static_assert(IsSubset(kTypedBindingMsgIds, mir2::common::kMessageCodecManagedMsgIds),
              "Typed message binding matrix contains msg ids outside message_codec matrix.");

static_assert(kHasTypedBinding<mir2::common::MsgId::kLoginReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kLoginRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kCreateRoleReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kCreateRoleRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMoveReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMoveRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAttackReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAttackRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kSkillReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kSkillRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kUseItemReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kUseItemRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPickupItemReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPickupItemRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kDropItemReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kDropItemRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kEquipReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kEquipRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kUnequipReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kUnequipRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kChatReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kChatRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildCreateReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildCreateRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildJoinReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildJoinRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildLeaveReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildLeaveRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildKickReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildKickRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildDeclareWarReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildDeclareWarRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildCancelWarReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildCancelWarRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildMakeAllyReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildMakeAllyRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildBreakAllyReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildBreakAllyRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeAddItemReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeAddItemRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeSetGoldReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeSetGoldRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeConfirmReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeConfirmRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeCancelReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeCancelRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeUpdate>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kTradeComplete>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyInviteReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyInviteRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyJoinReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyJoinRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyLeaveReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyLeaveRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyKickReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyKickRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kPartyUpdate>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildUpdateNoticeReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildUpdateNoticeRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildUpdateRankReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildUpdateRankRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kGuildInfoSync>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kRankingReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kRankingRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kRankingMyRankReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kRankingMyRankRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailSendReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailSendRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailListReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailListRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailReadReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailReadRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailDeleteReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailDeleteRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailClaimReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailClaimRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kMailNotify>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAchievementListReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAchievementListRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAchievementClaimReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAchievementClaimRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAchievementUpdate>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionListReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionListRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionSellReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionSellRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionBuyReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionBuyRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionCancelReq>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionCancelRsp>);
static_assert(kHasTypedBinding<mir2::common::MsgId::kAuctionNotify>);

}  // namespace mir2::common::protocol::bindings

#endif  // MIR2_COMMON_PROTOCOL_TYPED_MESSAGE_BINDINGS_H_
