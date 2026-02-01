#include "client/handlers/character_handler.h"

#include "common/protocol/message_codec.h"
#include "game_generated.h"
#include "login_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::game::handlers {

namespace {
const char* proto_error_to_string(mir2::proto::ErrorCode code) {
    switch (code) {
        case mir2::proto::ErrorCode::ERR_OK: return "OK";
        case mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND: return "Account not found";
        case mir2::proto::ErrorCode::ERR_PASSWORD_WRONG: return "Password incorrect";
        case mir2::proto::ErrorCode::ERR_NAME_EXISTS: return "Name already exists";
        case mir2::proto::ErrorCode::ERR_TARGET_DEAD: return "Target dead";
        case mir2::proto::ErrorCode::ERR_SKILL_COOLDOWN: return "Skill cooldown";
        case mir2::proto::ErrorCode::ERR_INVALID_ACTION: return "Invalid action";
        case mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND: return "Target not found";
        case mir2::proto::ErrorCode::ERR_TARGET_OUT_OF_RANGE: return "Target out of range";
        case mir2::proto::ErrorCode::ERR_INSUFFICIENT_MP: return "Insufficient MP";
        default: return "Unknown error";
    }
}

mir2::common::CharacterClass from_proto_profession(mir2::proto::Profession profession) {
    switch (profession) {
        case mir2::proto::Profession::WARRIOR:
            return mir2::common::CharacterClass::WARRIOR;
        case mir2::proto::Profession::WIZARD:
            return mir2::common::CharacterClass::MAGE;
        case mir2::proto::Profession::TAOIST:
            return mir2::common::CharacterClass::TAOIST;
        default:
            return mir2::common::CharacterClass::WARRIOR;
    }
}

mir2::common::Gender from_proto_gender(mir2::proto::Gender gender) {
    return gender == mir2::proto::Gender::FEMALE ? mir2::common::Gender::FEMALE : mir2::common::Gender::MALE;
}

const std::vector<mir2::common::CharacterData>& empty_character_list() {
    static const std::vector<mir2::common::CharacterData> empty;
    return empty;
}
} // namespace

CharacterHandler* CharacterHandler::instance_ = nullptr;

CharacterHandler::CharacterHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
    instance_ = this;
}

CharacterHandler::~CharacterHandler() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void CharacterHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    manager.register_handler(mir2::common::MsgId::kRoleListRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleCharacterListResponse(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kCreateRoleRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleCharacterCreateResponse(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kSelectRoleRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleCharacterSelectResponse(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kEnterGameRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleEnterGameResponse(packet);
                             });
}

void CharacterHandler::RegisterHandlers(mir2::client::INetworkManager& manager) {
    if (!instance_) {
        return;
    }
    instance_->BindHandlers(manager);
}

void CharacterHandler::HandleCharacterListResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr)) {
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::RoleListRsp>(packet.payload.data());
    if (!rsp) {
        return;
    }

    if (rsp->code() != mir2::proto::ErrorCode::ERR_OK) {
        if (callbacks_.on_character_list_failed) {
            callbacks_.on_character_list_failed(proto_error_to_string(rsp->code()));
        }
        return;
    }

    std::vector<mir2::common::CharacterData> characters;
    if (rsp->roles()) {
        characters.reserve(rsp->roles()->size());
        for (const auto* role : *rsp->roles()) {
            if (!role || !role->name()) {
                continue;
            }
            mir2::common::CharacterData data;
            data.id = static_cast<uint32_t>(role->player_id());
            const uint64_t account_id = callbacks_.get_account_id ? callbacks_.get_account_id() : 0;
            data.account_id = std::to_string(account_id);
            data.name = role->name()->str();
            data.char_class = from_proto_profession(role->profession());
            data.gender = from_proto_gender(role->gender());
            data.stats = mir2::common::get_class_base_stats(data.char_class);
            data.stats.level = static_cast<int>(role->level());
            data.stats.gold = static_cast<int>(role->gold());
            data.map_id = role->map_id();
            data.position = {role->x(), role->y()};
            characters.push_back(std::move(data));
        }
    }

    if (callbacks_.on_character_list) {
        callbacks_.on_character_list(std::move(characters));
    }
}

void CharacterHandler::HandleCharacterCreateResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    mir2::common::CreateCharacterResponse response;
    const auto status =
        mir2::common::DecodeCreateCharacterResponse(packet.msg_id, packet.payload, &response);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        return;
    }

    if (response.code == mir2::proto::ErrorCode::ERR_OK) {
        if (callbacks_.on_character_created) {
            callbacks_.on_character_created(response.player_id);
        }
        if (callbacks_.request_character_list) {
            callbacks_.request_character_list();
        }
        return;
    }

    if (callbacks_.on_character_create_failed) {
        callbacks_.on_character_create_failed(proto_error_to_string(response.code));
    }
}

void CharacterHandler::HandleCharacterSelectResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr)) {
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(packet.payload.data());
    if (!rsp) {
        return;
    }

    if (rsp->code() != mir2::proto::ErrorCode::ERR_OK) {
        if (callbacks_.on_select_role_failed) {
            callbacks_.on_select_role_failed(proto_error_to_string(rsp->code()));
        }
        return;
    }

    if (callbacks_.on_select_role_success) {
        callbacks_.on_select_role_success();
    }
}

void CharacterHandler::HandleEnterGameResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr)) {
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::EnterGameRsp>(packet.payload.data());
    if (!rsp || rsp->code() != mir2::proto::ErrorCode::ERR_OK || !rsp->player()) {
        if (callbacks_.on_enter_game_failed) {
            callbacks_.on_enter_game_failed(proto_error_to_string(
                rsp ? rsp->code() : mir2::proto::ErrorCode::ERR_UNKNOWN));
        }
        return;
    }

    const auto* info = rsp->player();
    mir2::common::CharacterData data;
    data.id = static_cast<uint32_t>(info->id());
    const uint64_t account_id = callbacks_.get_account_id ? callbacks_.get_account_id() : 0;
    data.account_id = std::to_string(account_id);
    data.name = info->name() ? info->name()->str() : "";
    data.char_class = from_proto_profession(info->profession());
    data.gender = mir2::common::Gender::MALE;
    const auto& list = callbacks_.get_character_list ? callbacks_.get_character_list()
                                                     : empty_character_list();
    for (const auto& entry : list) {
        if (entry.id == data.id) {
            data.gender = entry.gender;
            break;
        }
    }
    data.stats = mir2::common::get_class_base_stats(data.char_class);
    data.stats.level = static_cast<int>(info->level());
    data.stats.hp = info->hp();
    data.stats.max_hp = info->max_hp();
    data.stats.mp = info->mp();
    data.stats.max_mp = info->max_mp();
    data.stats.gold = static_cast<int>(info->gold());
    data.map_id = info->map_id();
    data.position = {info->x(), info->y()};

    if (callbacks_.on_enter_game_success) {
        callbacks_.on_enter_game_success(data);
    }
}

} // namespace mir2::game::handlers
