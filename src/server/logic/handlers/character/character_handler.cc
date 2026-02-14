#include "logic/handlers/character/character_handler.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/character_data.h"
#include "common/enums.h"
#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "game_generated.h"
#include "logic/services/client_registry.h"
#include "log/logger.h"
#include "logic/coroutine_executor.h"
#include "logic/response_sender.h"
#include "logic/services/session_role_store.h"
#include "login_generated.h"
#include "system_generated.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildRoleListRsp(mir2::proto::ErrorCode code,
                                      const std::vector<RoleRecord>& roles) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::CharacterInfo>> role_offsets;
  role_offsets.reserve(roles.size());

  for (const auto& role : roles) {
    const auto name_offset = builder.CreateString(role.name);
    const auto info = mir2::proto::CreateCharacterInfo(
        builder, role.player_id, name_offset,
        static_cast<mir2::proto::Profession>(role.profession),
        static_cast<mir2::proto::Gender>(role.gender),
        role.level, role.map_id, role.x, role.y, role.gold);
    role_offsets.push_back(info);
  }

  const auto roles_vec = builder.CreateVector(role_offsets);
  const auto rsp = mir2::proto::CreateRoleListRsp(builder, code, roles_vec);
  builder.Finish(rsp);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildCreateRoleRsp(mir2::proto::ErrorCode code, uint64_t player_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateCreateRoleRsp(builder, code, player_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildSelectRoleRsp(mir2::proto::ErrorCode code, uint64_t player_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateSelectRoleRsp(builder, code, player_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildEnterGameRsp(mir2::proto::ErrorCode code,
                                       const RoleRecord* role,
                                       int hp = 100, int max_hp = 100,
                                       int mp = 50, int max_mp = 50) {
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<mir2::proto::PlayerInfo> player_offset = 0;

  if (role) {
    const auto name_offset = builder.CreateString(role->name);
    player_offset = mir2::proto::CreatePlayerInfo(
        builder, role->player_id, name_offset,
        static_cast<mir2::proto::Profession>(role->profession),
        role->level, hp, max_hp, mp, max_mp, role->map_id, role->x, role->y,
        role->gold);
  }

  const auto rsp = mir2::proto::CreateEnterGameRsp(builder, code, player_offset);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildDuplicateLoginKick() {
  flatbuffers::FlatBufferBuilder builder;
  const auto message_offset = builder.CreateString("Account logged in elsewhere");
  const auto reason_text_offset = builder.CreateString("duplicate login");
  const auto kick = mir2::proto::CreateKick(
      builder,
      mir2::proto::ErrorCode::ERR_KICK_DUPLICATE_LOGIN,
      message_offset,
      reason_text_offset);
  builder.Finish(kick);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

mir2::proto::ErrorCode ToProtoError(mir2::common::ErrorCode code) {
  switch (code) {
    case mir2::common::ErrorCode::kOk:
      return mir2::proto::ErrorCode::ERR_OK;
    case mir2::common::ErrorCode::kAccountNotFound:
      return mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND;
    case mir2::common::ErrorCode::kNameExists:
      return mir2::proto::ErrorCode::ERR_NAME_EXISTS;
    case mir2::common::ErrorCode::kInvalidAction:
    case mir2::common::ErrorCode::INVALID_CHARACTER_NAME:
    case mir2::common::ErrorCode::INVALID_CHARACTER_CLASS:
    case mir2::common::ErrorCode::INVALID_CREDENTIALS:
      return mir2::proto::ErrorCode::ERR_INVALID_ACTION;
    default:
      return mir2::proto::ErrorCode::ERR_UNKNOWN;
  }
}

bool TryToCharacterClass(mir2::proto::Profession profession,
                         mir2::common::CharacterClass* out) {
  if (!out) {
    return false;
  }
  switch (profession) {
    case mir2::proto::Profession::WARRIOR:
      *out = mir2::common::CharacterClass::WARRIOR;
      return true;
    case mir2::proto::Profession::WIZARD:
      *out = mir2::common::CharacterClass::MAGE;
      return true;
    case mir2::proto::Profession::TAOIST:
      *out = mir2::common::CharacterClass::TAOIST;
      return true;
    default:
      return false;
  }
}

bool TryToCharacterGender(mir2::proto::Gender gender, mir2::common::Gender* out) {
  if (!out) {
    return false;
  }
  switch (gender) {
    case mir2::proto::Gender::FEMALE:
      *out = mir2::common::Gender::FEMALE;
      return true;
    case mir2::proto::Gender::MALE:
      *out = mir2::common::Gender::MALE;
      return true;
    default:
      return false;
  }
}

}  // namespace

CharacterHandler::CharacterHandler(ResponseSender& response_sender,
                                   mir2::ecs::CharacterEntityManager& entity_manager,
                                   RoleStore& role_store,
                                   ClientRegistry& client_registry)
    : response_sender_(response_sender),
      entity_manager_(entity_manager),
      role_store_(role_store),
      client_registry_(client_registry) {}

Task<void> CharacterHandler::HandleMessage(HandlerContext ctx,
                                           const uint8_t* payload,
                                           size_t payload_size) {
  // Allow empty payload only for kLogout message
  if (!payload || payload_size == 0) {
    if (static_cast<mir2::common::MsgId>(ctx.msg_id) != mir2::common::MsgId::kLogout) {
      SYSLOG_WARN("CharacterHandler empty payload for msg_id={} (client_id={})",
                  ctx.msg_id, ctx.client_id);
      co_return;
    }
  }

  switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
    case mir2::common::MsgId::kRoleListReq: {
      flatbuffers::Verifier verifier(payload, payload_size);
      if (verifier.VerifyBuffer<mir2::proto::RoleListReq>(nullptr)) {
        const auto* req = flatbuffers::GetRoot<mir2::proto::RoleListReq>(payload);
        co_await HandleRoleList(std::move(ctx), req);
        co_return;
      }
      auto response_payload =
          BuildRoleListRsp(mir2::proto::ErrorCode::ERR_INVALID_ACTION, {});
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp),
          std::move(response_payload));
      co_return;
    }
    case mir2::common::MsgId::kCreateRoleReq: {
      flatbuffers::Verifier verifier(payload, payload_size);
      if (verifier.VerifyBuffer<mir2::proto::CreateRoleReq>(nullptr)) {
        const auto* req = flatbuffers::GetRoot<mir2::proto::CreateRoleReq>(payload);
        co_await HandleCreateRole(std::move(ctx), req);
        co_return;
      }
      auto response_payload = BuildCreateRoleRsp(
          mir2::proto::ErrorCode::ERR_INVALID_ACTION, 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
          std::move(response_payload));
      co_return;
    }
    case mir2::common::MsgId::kSelectRoleReq: {
      flatbuffers::Verifier verifier(payload, payload_size);
      if (verifier.VerifyBuffer<mir2::proto::SelectRoleReq>(nullptr)) {
        const auto* req = flatbuffers::GetRoot<mir2::proto::SelectRoleReq>(payload);
        co_await HandleSelectRole(std::move(ctx), req);
        co_return;
      }
      auto response_payload = BuildSelectRoleRsp(
          mir2::proto::ErrorCode::ERR_INVALID_ACTION, 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
          std::move(response_payload));
      co_return;
    }
    case mir2::common::MsgId::kLogout:
      co_await HandleLogout(std::move(ctx));
      co_return;
    default:
      SYSLOG_WARN("CharacterHandler unsupported msg_id={} (client_id={})",
                  ctx.msg_id, ctx.client_id);
      break;
  }
  co_return;
}

Task<void> CharacterHandler::HandleRoleList(HandlerContext ctx,
                                            const mir2::proto::RoleListReq* req) {
  if (!req) {
    co_return;
  }

  const uint64_t requested_account_id = req->account_id();
  const auto authenticated_account = role_store_.GetAccountId(ctx.client_id);
  if (!authenticated_account.has_value() || *authenticated_account == 0) {
    SYSLOG_WARN("CharacterHandler RoleList unauthenticated client_id={} requested_account={}",
                ctx.client_id,
                requested_account_id);
    auto response_payload = BuildRoleListRsp(mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, {});
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp),
        std::move(response_payload));
    co_return;
  }

  if (requested_account_id != 0 && requested_account_id != *authenticated_account) {
    SYSLOG_WARN("CharacterHandler RoleList account mismatch client_id={} requested={} bound={}",
                ctx.client_id,
                requested_account_id,
                *authenticated_account);
    auto response_payload = BuildRoleListRsp(mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, {});
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp),
        std::move(response_payload));
    co_return;
  }

  const uint64_t account_id = *authenticated_account;
  auto roles = role_store_.GetRoles(account_id);
  auto response_payload = BuildRoleListRsp(mir2::proto::ErrorCode::ERR_OK, roles);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp),
      std::move(response_payload));

  SYSLOG_INFO("CharacterHandler RoleList account_id={} count={}", account_id, roles.size());
}

Task<void> CharacterHandler::HandleCreateRole(HandlerContext ctx,
                                              const mir2::proto::CreateRoleReq* req) {
  if (!req) {
    co_return;
  }

  const std::string name = req->name() ? req->name()->str() : "";
  const auto account_id_opt = role_store_.GetAccountId(ctx.client_id);

  if (!account_id_opt.has_value() || *account_id_opt == 0) {
    SYSLOG_WARN("CharacterHandler CreateRole unauthenticated client_id={} name={}",
                ctx.client_id,
                name);
    auto response_payload = BuildCreateRoleRsp(
        mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, 0);
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
        std::move(response_payload));
    co_return;
  }

  const uint64_t account_id = *account_id_opt;

  mir2::common::CharacterClass character_class;
  mir2::common::Gender character_gender;
  if (!TryToCharacterClass(req->profession(), &character_class) ||
      !TryToCharacterGender(req->gender(), &character_gender)) {
    auto response_payload = BuildCreateRoleRsp(
        mir2::proto::ErrorCode::ERR_INVALID_ACTION, 0);
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
        std::move(response_payload));
    co_return;
  }

  mir2::common::CharacterCreateRequest create_request;
  create_request.account_id = std::to_string(account_id);
  create_request.name = name;
  create_request.char_class = character_class;
  create_request.gender = character_gender;

  const auto validation =
      mir2::common::validate_character_create_request(create_request);
  if (!validation.valid) {
    auto response_payload = BuildCreateRoleRsp(
        ToProtoError(validation.error_code), 0);
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
        std::move(response_payload));
    co_return;
  }

  RoleRecord record;
  auto code = role_store_.CreateRole(
      account_id, name,
      static_cast<uint8_t>(req->profession()),
      static_cast<uint8_t>(req->gender()),
      &record);

  if (code == mir2::common::ErrorCode::kOk) {
    if (record.player_id > std::numeric_limits<uint32_t>::max()) {
      SYSLOG_ERROR("CharacterHandler CreateRole player_id={} exceeds uint32_t max, rolling back",
                   record.player_id);
      const bool removed = role_store_.RemoveRole(account_id, record.player_id);
      if (!removed) {
        SYSLOG_ERROR("CharacterHandler CreateRole failed to rollback player_id={} account_id={}",
                     record.player_id, account_id);
      }
      code = mir2::common::ErrorCode::kUnknown;
      record.player_id = 0;
    }
  }

  if (code == mir2::common::ErrorCode::kOk) {
    entt::entity entity = entity_manager_.CreateFromRequest(
        static_cast<uint32_t>(record.player_id), create_request);
    if (entity == entt::null) {
      SYSLOG_ERROR("CharacterHandler CreateRole entity creation failed for player_id={}, rolling back",
                   record.player_id);
      const bool removed = role_store_.RemoveRole(account_id, record.player_id);
      if (!removed) {
        SYSLOG_ERROR("CharacterHandler CreateRole failed to rollback player_id={} account_id={}",
                     record.player_id, account_id);
      }
      code = mir2::common::ErrorCode::kUnknown;
      record.player_id = 0;
    }
  }

  const auto proto_code = ToProtoError(code);
  auto response_payload = BuildCreateRoleRsp(proto_code, record.player_id);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
      std::move(response_payload));

  if (code == mir2::common::ErrorCode::kOk) {
    SYSLOG_INFO("CharacterHandler CreateRole success account_id={} name={} player_id={}",
                account_id, name, record.player_id);
  } else {
    SYSLOG_WARN("CharacterHandler CreateRole failed account_id={} name={} code={}",
                account_id, name, static_cast<int>(code));
  }
}

Task<void> CharacterHandler::HandleSelectRole(HandlerContext ctx,
                                              const mir2::proto::SelectRoleReq* req) {
  if (!req) {
    co_return;
  }

  const auto account_id_opt = role_store_.GetAccountId(ctx.client_id);
  if (!account_id_opt.has_value() || *account_id_opt == 0) {
    SYSLOG_WARN("CharacterHandler SelectRole unauthenticated client_id={} player_id={}",
                ctx.client_id,
                req->player_id());
    auto select_payload = BuildSelectRoleRsp(mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, 0);
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
        std::move(select_payload));

    auto enter_payload = BuildEnterGameRsp(mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, nullptr);
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
        std::move(enter_payload));
    co_return;
  }

  const uint64_t player_id = req->player_id();
  const uint64_t account_id = *account_id_opt;
  const auto role_opt = role_store_.FindRole(account_id, player_id);
  const bool found = role_opt.has_value();
  const bool player_id_fits = player_id <= std::numeric_limits<uint32_t>::max();
  const bool can_login = found && player_id_fits;

  const auto code = can_login ? mir2::proto::ErrorCode::ERR_OK
                              : (found ? mir2::proto::ErrorCode::ERR_UNKNOWN
                                       : mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);

  // Send SelectRoleRsp.
  auto select_payload = BuildSelectRoleRsp(code, player_id);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
      std::move(select_payload));

  if (can_login) {
    entity_manager_.OnLogin(static_cast<uint32_t>(player_id));
    const auto evicted_client_id = role_store_.BindClientRole(ctx.client_id, player_id);
    if (evicted_client_id.has_value() && *evicted_client_id != ctx.client_id) {
      co_await response_sender_.SendAsync(
          *evicted_client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kKick),
          BuildDuplicateLoginKick());
      role_store_.UnbindClient(*evicted_client_id);
      client_registry_.Remove(*evicted_client_id);
      SYSLOG_WARN(
          "CharacterHandler SelectRole evicted old client on duplicate login "
          "(player_id={}, old_client_id={}, new_client_id={})",
          player_id,
          *evicted_client_id,
          ctx.client_id);
    }
  }

  // Send EnterGameRsp with real HP/MP values if available.
  int hp = 100, max_hp = 100, mp = 50, max_mp = 50;  // Default fallback values
  if (can_login) {
    const auto entity_opt = entity_manager_.TryGet(static_cast<uint32_t>(player_id));
    if (entity_opt) {
      const auto* registry = entity_manager_.TryGetRegistry(static_cast<uint32_t>(player_id));
      if (registry && registry->valid(*entity_opt)) {
        const auto* attributes = registry->try_get<mir2::ecs::CharacterAttributesComponent>(*entity_opt);
        if (attributes) {
          hp = attributes->hp;
          max_hp = attributes->max_hp;
          mp = attributes->mp;
          max_mp = attributes->max_mp;
        }
      }
    }
  }

  const RoleRecord* role_ptr = can_login ? &role_opt.value() : nullptr;
  auto enter_payload = BuildEnterGameRsp(code, role_ptr, hp, max_hp, mp, max_mp);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
      std::move(enter_payload));

  if (can_login) {
    SYSLOG_INFO("CharacterHandler SelectRole success player_id={} account_id={} hp={}/{} mp={}/{}",
                player_id, account_id, hp, max_hp, mp, max_mp);
  } else {
    SYSLOG_WARN("CharacterHandler SelectRole failed player_id={} found={} player_id_fits={}",
                player_id, found, player_id_fits);
  }
}

Task<void> CharacterHandler::HandleLogout(HandlerContext ctx) {
  client_registry_.Remove(ctx.client_id);
  const auto role_id_opt = role_store_.GetRoleId(ctx.client_id);
  if (role_id_opt) {
    entity_manager_.OnDisconnect(static_cast<uint32_t>(*role_id_opt));
  }
  role_store_.UnbindClient(ctx.client_id);

  SYSLOG_DEBUG("CharacterHandler Logout client_id={}", ctx.client_id);
  co_return;
}

}  // namespace mir2::logic
