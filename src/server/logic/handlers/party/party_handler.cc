#include "logic/handlers/party/party_handler.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/party_component.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "party_generated.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildInviteRspPayload(bool success,
                                           mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreatePartyInviteRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildJoinRspPayload(bool success,
                                         mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreatePartyJoinRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildLeaveRspPayload(bool success,
                                          mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreatePartyLeaveRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildKickRspPayload(bool success,
                                         mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreatePartyKickRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

uint32_t SafeToUint32(int64_t value) {
  if (value <= 0) {
    return 0;
  }
  if (value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(value);
}

}  // namespace

PartyHandler::PartyHandler(ResponseSender& response_sender,
                           ClientRegistry& client_registry,
                           entt::registry& ecs_registry,
                           RoleStore* role_store)
    : response_sender_(response_sender),
      client_registry_(client_registry),
      ecs_registry_(ecs_registry),
      role_store_(role_store) {}

Task<void> PartyHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  try {
    if (!payload || payload_size == 0) {
      SYSLOG_WARN("PartyHandler ignored empty payload client_id={} msg_id={}",
                  ctx.client_id,
                  ctx.msg_id);
      co_return;
    }

    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kPartyInviteReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::PartyInviteReq>(nullptr)) {
          co_await SendInviteRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::PartyInviteReq>(payload);
        co_await HandleInvite(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kPartyJoinReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::PartyJoinReq>(nullptr)) {
          co_await SendJoinRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::PartyJoinReq>(payload);
        co_await HandleJoin(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kPartyLeaveReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::PartyLeaveReq>(nullptr)) {
          co_await SendLeaveRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::PartyLeaveReq>(payload);
        co_await HandleLeave(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kPartyKickReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::PartyKickReq>(nullptr)) {
          co_await SendKickRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::PartyKickReq>(payload);
        co_await HandleKick(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("PartyHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("PartyHandler exception msg_id={} client_id={} error={}",
                 ctx.msg_id,
                 ctx.client_id,
                 ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("PartyHandler exception msg_id={} client_id={} error=unknown",
                 ctx.msg_id,
                 ctx.client_id);
    co_return;
  }
}

Task<void> PartyHandler::HandleInvite(HandlerContext ctx,
                                      const mir2::proto::PartyInviteReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendInviteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto self_character_id = GetCharacterId(ctx.entity);
  const uint64_t target_character_id = req->target_character_id();
  if (!self_character_id.has_value() || target_character_id == 0 ||
      *self_character_id == target_character_id) {
    co_await SendInviteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kPartyInviteInvalid);
    co_return;
  }

  const auto target_entity = FindEntityByCharacterId(target_character_id, true);
  if (!target_entity.has_value()) {
    co_await SendInviteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound);
    co_return;
  }

  if (FindPartyEntityByMember(*target_entity).has_value()) {
    co_await SendInviteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kPartyInviteInvalid);
    co_return;
  }

  entt::entity party_entity = entt::null;
  const auto self_party = FindPartyEntityByMember(ctx.entity);
  if (self_party.has_value()) {
    party_entity = *self_party;
    auto* party = ecs_registry_.try_get<mir2::ecs::PartyComponent>(party_entity);
    if (!party || party->leader != ctx.entity) {
      co_await SendInviteRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
      co_return;
    }
    if (party->IsFull()) {
      co_await SendInviteRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kPartyFull);
      co_return;
    }
  } else {
    party_entity = EnsurePartyForLeader(ctx.entity);
  }

  if (party_entity == entt::null || !ecs_registry_.valid(party_entity)) {
    co_await SendInviteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kPartyNotFound);
    co_return;
  }

  auto& party = ecs_registry_.get<mir2::ecs::PartyComponent>(party_entity);
  if (!party.AddMember(*target_entity)) {
    co_await SendInviteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kPartyFull);
    co_return;
  }

  auto& target_member =
      ecs_registry_.get_or_emplace<mir2::ecs::PartyMemberComponent>(*target_entity);
  target_member.party_id = party.party_id;
  target_member.character_id = SafeToUint32(target_character_id);

  co_await SendInviteRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  const auto target_client = GetClientIdByCharacterId(target_character_id);
  if (target_client.has_value() && *target_client != ctx.client_id) {
    co_await SendInviteRsp(*target_client, true, mir2::common::ErrorCode::kOk);
  }

  const auto snapshot = BuildPartySnapshot(party_entity);
  if (snapshot.has_value()) {
    co_await SendPartyUpdate(*snapshot);
  }
}

Task<void> PartyHandler::HandleJoin(HandlerContext ctx,
                                    const mir2::proto::PartyJoinReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity) ||
      req->party_id() == 0) {
    co_await SendJoinRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kPartyNotFound);
    co_return;
  }

  if (const auto existing_party = FindPartyEntityByMember(ctx.entity);
      existing_party.has_value()) {
    auto* party = ecs_registry_.try_get<mir2::ecs::PartyComponent>(*existing_party);
    if (party && party->party_id == req->party_id()) {
      co_await SendJoinRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);
      const auto snapshot = BuildPartySnapshot(*existing_party);
      if (snapshot.has_value()) {
        co_await SendPartyUpdate(*snapshot);
      }
      co_return;
    }
    co_await SendJoinRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kPartyInviteInvalid);
    co_return;
  }

  const auto party_entity = FindPartyEntityById(SafeToUint32(req->party_id()));
  if (!party_entity.has_value()) {
    co_await SendJoinRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kPartyNotFound);
    co_return;
  }

  auto& party = ecs_registry_.get<mir2::ecs::PartyComponent>(*party_entity);
  if (party.IsFull()) {
    co_await SendJoinRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kPartyFull);
    co_return;
  }

  if (!party.AddMember(ctx.entity)) {
    co_await SendJoinRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kPartyInviteInvalid);
    co_return;
  }

  const auto self_character_id = GetCharacterId(ctx.entity).value_or(0);
  auto& self_member =
      ecs_registry_.get_or_emplace<mir2::ecs::PartyMemberComponent>(ctx.entity);
  self_member.party_id = party.party_id;
  self_member.character_id = SafeToUint32(self_character_id);

  co_await SendJoinRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  const auto snapshot = BuildPartySnapshot(*party_entity);
  if (snapshot.has_value()) {
    co_await SendPartyUpdate(*snapshot);
  }
}

Task<void> PartyHandler::HandleLeave(HandlerContext ctx,
                                     const mir2::proto::PartyLeaveReq* req) {
  (void)req;
  if (ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendLeaveRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto party_entity = FindPartyEntityByMember(ctx.entity);
  if (!party_entity.has_value()) {
    co_await SendLeaveRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kNoParty);
    co_return;
  }

  if (!ecs_registry_.valid(*party_entity)) {
    RemovePartyMember(ctx.entity);
    co_await SendLeaveRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kNoParty);
    co_return;
  }

  auto& party = ecs_registry_.get<mir2::ecs::PartyComponent>(*party_entity);
  const bool was_leader = party.leader == ctx.entity;
  party.RemoveMember(ctx.entity);
  RemovePartyMember(ctx.entity);

  co_await SendLeaveRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);
  co_await SendPartyClear(ctx.client_id);

  if (party.members.empty()) {
    ecs_registry_.destroy(*party_entity);
    co_return;
  }

  if (was_leader) {
    ReassignLeaderIfNeeded(*party_entity);
  }

  const auto snapshot = BuildPartySnapshot(*party_entity);
  if (snapshot.has_value()) {
    co_await SendPartyUpdate(*snapshot);
  }
}

Task<void> PartyHandler::HandleKick(HandlerContext ctx,
                                    const mir2::proto::PartyKickReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendKickRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto party_entity = FindPartyEntityByMember(ctx.entity);
  if (!party_entity.has_value() || !ecs_registry_.valid(*party_entity)) {
    co_await SendKickRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kNoParty);
    co_return;
  }

  auto& party = ecs_registry_.get<mir2::ecs::PartyComponent>(*party_entity);
  if (party.leader != ctx.entity) {
    co_await SendKickRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const uint64_t target_character_id = req->target_character_id();
  const auto target_entity = FindEntityByCharacterId(target_character_id, false);
  if (!target_entity.has_value() || !party.IsMember(*target_entity) ||
      *target_entity == ctx.entity) {
    co_await SendKickRsp(ctx.client_id,
                         false,
                         mir2::common::ErrorCode::kPartyNotFound);
    co_return;
  }

  party.RemoveMember(*target_entity);
  RemovePartyMember(*target_entity);

  co_await SendKickRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  const auto target_client = GetClientIdByCharacterId(target_character_id);
  if (target_client.has_value() && *target_client != ctx.client_id) {
    co_await SendKickRsp(*target_client, true, mir2::common::ErrorCode::kOk);
    co_await SendPartyClear(*target_client);
  }

  if (party.members.empty()) {
    ecs_registry_.destroy(*party_entity);
    co_return;
  }

  ReassignLeaderIfNeeded(*party_entity);
  const auto snapshot = BuildPartySnapshot(*party_entity);
  if (snapshot.has_value()) {
    co_await SendPartyUpdate(*snapshot);
  }
}

Task<void> PartyHandler::SendInviteRsp(uint64_t client_id,
                                       bool success,
                                       mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildInviteRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteRsp),
      std::move(payload));
}

Task<void> PartyHandler::SendJoinRsp(uint64_t client_id,
                                     bool success,
                                     mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildJoinRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPartyJoinRsp),
      std::move(payload));
}

Task<void> PartyHandler::SendLeaveRsp(uint64_t client_id,
                                      bool success,
                                      mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildLeaveRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPartyLeaveRsp),
      std::move(payload));
}

Task<void> PartyHandler::SendKickRsp(uint64_t client_id,
                                     bool success,
                                     mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildKickRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPartyKickRsp),
      std::move(payload));
}

Task<void> PartyHandler::SendPartyUpdate(const PartySnapshot& snapshot) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::PartyMemberInfo>> member_offsets;
  member_offsets.reserve(snapshot.members.size());

  for (const auto& member : snapshot.members) {
    const auto name_offset = builder.CreateString(member.name);
    member_offsets.emplace_back(mir2::proto::CreatePartyMemberInfo(
        builder,
        SafeToUint32(member.character_id),
        name_offset,
        member.hp,
        member.max_hp,
        member.map_id,
        member.x,
        member.y,
        member.online));
  }

  const auto members_vec = builder.CreateVector(member_offsets);
  const auto update = mir2::proto::CreatePartyUpdate(
      builder,
      snapshot.party_id,
      SafeToUint32(snapshot.leader_character_id),
      members_vec);
  builder.Finish(update);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  std::unordered_set<uint64_t> dedup;
  dedup.reserve(snapshot.members.size());

  for (const auto& member : snapshot.members) {
    if (member.client_id == 0) {
      continue;
    }
    if (!dedup.insert(member.client_id).second) {
      continue;
    }

    co_await response_sender_.SendAsync(
        member.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kPartyUpdate),
        payload);
  }
}

Task<void> PartyHandler::SendPartyClear(uint64_t client_id) {
  if (client_id == 0) {
    co_return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto update = mir2::proto::CreatePartyUpdate(builder, 0, 0, 0);
  builder.Finish(update);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPartyUpdate),
      std::move(payload));
}

std::optional<uint64_t> PartyHandler::GetCharacterId(entt::entity entity) const {
  if (entity == entt::null || !ecs_registry_.valid(entity)) {
    return std::nullopt;
  }

  const auto* identity = ecs_registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
  if (!identity || identity->id == 0) {
    return std::nullopt;
  }
  return identity->id;
}

std::optional<uint64_t> PartyHandler::GetClientIdByCharacterId(
    uint64_t character_id) const {
  if (character_id == 0) {
    return std::nullopt;
  }

  if (role_store_) {
    auto mapped = role_store_->GetClientIdByRoleId(character_id);
    if (mapped.has_value()) {
      return mapped;
    }
  }

  if (client_registry_.Contains(character_id)) {
    return character_id;
  }

  return character_id;
}

std::optional<entt::entity> PartyHandler::FindEntityByCharacterId(
    uint64_t character_id,
    bool require_online) const {
  if (character_id == 0) {
    return std::nullopt;
  }

  auto view = ecs_registry_.view<mir2::ecs::CharacterIdentityComponent>();
  for (const auto entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (identity.id != character_id) {
      continue;
    }

    if (require_online) {
      const auto* state = ecs_registry_.try_get<mir2::ecs::CharacterStateComponent>(entity);
      if (!state || !state->is_online) {
        return std::nullopt;
      }
    }

    return entity;
  }

  return std::nullopt;
}

std::optional<entt::entity> PartyHandler::FindPartyEntityById(uint32_t party_id) const {
  if (party_id == 0) {
    return std::nullopt;
  }

  auto view = ecs_registry_.view<mir2::ecs::PartyComponent>();
  for (const auto entity : view) {
    const auto& party = view.get<mir2::ecs::PartyComponent>(entity);
    if (party.party_id == party_id) {
      return entity;
    }
  }

  return std::nullopt;
}

std::optional<entt::entity> PartyHandler::FindPartyEntityByMember(
    entt::entity member) const {
  if (member == entt::null || !ecs_registry_.valid(member)) {
    return std::nullopt;
  }

  const auto* member_comp = ecs_registry_.try_get<mir2::ecs::PartyMemberComponent>(member);
  if (!member_comp || member_comp->party_id == 0) {
    return std::nullopt;
  }

  return FindPartyEntityById(member_comp->party_id);
}

std::optional<PartyHandler::PartySnapshot> PartyHandler::BuildPartySnapshot(
    entt::entity party_entity) const {
  if (party_entity == entt::null || !ecs_registry_.valid(party_entity)) {
    return std::nullopt;
  }

  const auto* party = ecs_registry_.try_get<mir2::ecs::PartyComponent>(party_entity);
  if (!party || party->party_id == 0) {
    return std::nullopt;
  }

  PartySnapshot snapshot;
  snapshot.party_entity = party_entity;
  snapshot.party_id = party->party_id;
  snapshot.leader_character_id = GetCharacterId(party->leader).value_or(0);
  snapshot.members.reserve(party->members.size());

  for (const auto member_entity : party->members) {
    if (member_entity == entt::null || !ecs_registry_.valid(member_entity)) {
      continue;
    }

    PartyMemberView member;
    member.entity = member_entity;
    member.character_id = GetCharacterId(member_entity).value_or(0);
    member.client_id =
        GetClientIdByCharacterId(member.character_id).value_or(member.character_id);

    if (const auto* identity =
            ecs_registry_.try_get<mir2::ecs::CharacterIdentityComponent>(member_entity)) {
      member.name = identity->name;
    }

    if (const auto* attrs =
            ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(member_entity)) {
      member.hp = SafeToUint32(attrs->hp);
      member.max_hp = SafeToUint32(attrs->max_hp);
    }

    if (const auto* state =
            ecs_registry_.try_get<mir2::ecs::CharacterStateComponent>(member_entity)) {
      member.map_id = state->map_id;
      member.x = SafeToUint32(state->position.x);
      member.y = SafeToUint32(state->position.y);
      member.online = state->is_online;
    }

    snapshot.members.push_back(std::move(member));
  }

  return snapshot;
}

entt::entity PartyHandler::EnsurePartyForLeader(entt::entity leader) {
  if (leader == entt::null || !ecs_registry_.valid(leader)) {
    return entt::null;
  }

  if (const auto existing = FindPartyEntityByMember(leader); existing.has_value()) {
    return *existing;
  }

  const entt::entity party_entity = ecs_registry_.create();
  auto& party = ecs_registry_.emplace<mir2::ecs::PartyComponent>(party_entity);
  party.party_id = next_party_id_.fetch_add(1, std::memory_order_relaxed);
  party.leader = leader;
  party.AddMember(leader);

  const auto leader_character_id = GetCharacterId(leader).value_or(0);
  auto& leader_member = ecs_registry_.get_or_emplace<mir2::ecs::PartyMemberComponent>(leader);
  leader_member.party_id = party.party_id;
  leader_member.character_id = SafeToUint32(leader_character_id);

  return party_entity;
}

void PartyHandler::RemovePartyMember(entt::entity member) {
  if (member == entt::null || !ecs_registry_.valid(member)) {
    return;
  }

  if (ecs_registry_.any_of<mir2::ecs::PartyMemberComponent>(member)) {
    ecs_registry_.remove<mir2::ecs::PartyMemberComponent>(member);
  }
}

void PartyHandler::ReassignLeaderIfNeeded(entt::entity party_entity) {
  if (party_entity == entt::null || !ecs_registry_.valid(party_entity)) {
    return;
  }

  auto* party = ecs_registry_.try_get<mir2::ecs::PartyComponent>(party_entity);
  if (!party || party->members.empty()) {
    return;
  }

  if (party->leader != entt::null && party->IsMember(party->leader) &&
      ecs_registry_.valid(party->leader)) {
    return;
  }

  for (const auto member : party->members) {
    if (member != entt::null && ecs_registry_.valid(member)) {
      party->leader = member;
      return;
    }
  }

  party->leader = entt::null;
}

}  // namespace mir2::logic
