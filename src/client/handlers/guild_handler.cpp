#include "client/handlers/guild_handler.h"

#include "common/enums.h"
#include "guild_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const GuildHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

mir2::proto::ErrorCode ToProtoError(int error_code) {
    return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(error_code));
}

std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

} // namespace

GuildHandler::GuildHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void GuildHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Guild handlers are bound per-instance to capture callbacks.
}

void GuildHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();
    auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id,
                                       auto fn) {
        manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
            if (auto self = weak_self.lock()) {
                (self.get()->*fn)(packet);
            }
        });
    };

    bind(mir2::common::MsgId::kGuildCreateRsp, &GuildHandler::HandleCreateGuildResponse);
    bind(mir2::common::MsgId::kGuildJoinRsp, &GuildHandler::HandleJoinGuildResponse);
    bind(mir2::common::MsgId::kGuildLeaveRsp, &GuildHandler::HandleLeaveGuildResponse);
    bind(mir2::common::MsgId::kGuildKickRsp, &GuildHandler::HandleKickGuildResponse);
    bind(mir2::common::MsgId::kGuildDeclareWarRsp, &GuildHandler::HandleDeclareWarResponse);
    bind(mir2::common::MsgId::kGuildCancelWarRsp, &GuildHandler::HandleCancelWarResponse);
    bind(mir2::common::MsgId::kGuildMakeAllyRsp, &GuildHandler::HandleMakeAllyResponse);
    bind(mir2::common::MsgId::kGuildBreakAllyRsp, &GuildHandler::HandleBreakAllyResponse);
    bind(mir2::common::MsgId::kGuildUpdateNoticeRsp, &GuildHandler::HandleUpdateNoticeResponse);
    bind(mir2::common::MsgId::kGuildUpdateRankRsp, &GuildHandler::HandleUpdateRankResponse);
    bind(mir2::common::MsgId::kGuildInfoSync, &GuildHandler::HandleGuildInfoSync);

    // Legacy guild ids compatibility (1-10).
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::CREATE),
         &GuildHandler::HandleCreateGuildResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::JOIN),
         &GuildHandler::HandleJoinGuildResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::LEAVE),
         &GuildHandler::HandleLeaveGuildResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::KICK),
         &GuildHandler::HandleKickGuildResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::DECLARE_WAR),
         &GuildHandler::HandleDeclareWarResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::CANCEL_WAR),
         &GuildHandler::HandleCancelWarResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::MAKE_ALLY),
         &GuildHandler::HandleMakeAllyResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::BREAK_ALLY),
         &GuildHandler::HandleBreakAllyResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::UPDATE_NOTICE),
         &GuildHandler::HandleUpdateNoticeResponse);
    bind(static_cast<mir2::common::MsgId>(mir2::proto::GuildMessageType::UPDATE_RANK),
         &GuildHandler::HandleUpdateRankResponse);
}

void GuildHandler::HandleCreateGuildResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::CreateGuildResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("CreateGuildResponse verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::CreateGuildResponse>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("CreateGuildResponse parse failed");
        }
        return;
    }

    if (callbacks_.on_create_response) {
        const uint64_t guild_id = rsp->guild_info() ? rsp->guild_info()->id() : 0;
        callbacks_.on_create_response(rsp->success(), ToProtoError(rsp->error_code()), guild_id);
    }
}

void GuildHandler::HandleJoinGuildResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::JoinGuildResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("JoinGuildResponse verification failed");
        }
        return;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::JoinGuildResponse>(packet.payload.data());
    if (rsp && callbacks_.on_join_response) {
        callbacks_.on_join_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleLeaveGuildResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::LeaveGuildResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("LeaveGuildResponse verification failed");
        }
        return;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::LeaveGuildResponse>(packet.payload.data());
    if (rsp && callbacks_.on_leave_response) {
        callbacks_.on_leave_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleKickGuildResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::KickGuildResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("KickGuildResponse verification failed");
        }
        return;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::KickGuildResponse>(packet.payload.data());
    if (rsp && callbacks_.on_kick_response) {
        callbacks_.on_kick_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleDeclareWarResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::DeclareWarResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("DeclareWarResponse verification failed");
        }
        return;
    }
    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::DeclareWarResponse>(packet.payload.data());
    if (rsp && callbacks_.on_declare_war_response) {
        callbacks_.on_declare_war_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleCancelWarResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::CancelWarResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("CancelWarResponse verification failed");
        }
        return;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::CancelWarResponse>(packet.payload.data());
    if (rsp && callbacks_.on_cancel_war_response) {
        callbacks_.on_cancel_war_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleMakeAllyResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MakeAllianceResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MakeAllianceResponse verification failed");
        }
        return;
    }
    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::MakeAllianceResponse>(packet.payload.data());
    if (rsp && callbacks_.on_make_ally_response) {
        callbacks_.on_make_ally_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleBreakAllyResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::BreakAllianceResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("BreakAllianceResponse verification failed");
        }
        return;
    }
    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::BreakAllianceResponse>(packet.payload.data());
    if (rsp && callbacks_.on_break_ally_response) {
        callbacks_.on_break_ally_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleUpdateNoticeResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::UpdateNoticeResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("UpdateNoticeResponse verification failed");
        }
        return;
    }
    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::UpdateNoticeResponse>(packet.payload.data());
    if (rsp && callbacks_.on_update_notice_response) {
        callbacks_.on_update_notice_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleUpdateRankResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::UpdateRankResponse>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("UpdateRankResponse verification failed");
        }
        return;
    }
    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::UpdateRankResponse>(packet.payload.data());
    if (rsp && callbacks_.on_update_rank_response) {
        callbacks_.on_update_rank_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void GuildHandler::HandleGuildInfoSync(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::GuildInfoSync>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("GuildInfoSync verification failed");
        }
        return;
    }

    const auto* sync = flatbuffers::GetRoot<mir2::proto::GuildInfoSync>(packet.payload.data());
    if (!sync) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("GuildInfoSync parse failed");
        }
        return;
    }

    if (!callbacks_.on_guild_info_sync) {
        return;
    }

    GuildInfoSyncData data;
    const auto* guild = sync->guild_info();
    if (!guild) {
        callbacks_.on_guild_info_sync(data);
        return;
    }

    data.has_guild = true;
    data.guild_id = guild->id();
    data.guild_name = guild->name() ? guild->name()->str() : std::string{};
    data.level = guild->level();
    data.member_count = guild->member_count();
    data.leader_id = guild->leader_id();
    data.leader_name = guild->leader_name() ? guild->leader_name()->str() : std::string{};
    data.max_members = guild->max_members();
    data.allow_ally = guild->allow_ally();
    data.in_team_fight = guild->in_team_fight();
    data.match_point = guild->match_point();

    if (const auto* notices = guild->notice_list()) {
        data.notice_list.reserve(notices->size());
        for (const auto* notice : *notices) {
            data.notice_list.push_back(notice ? notice->str() : std::string{});
        }
    }

    if (const auto* ranks = guild->ranks()) {
        data.ranks.reserve(ranks->size());
        for (const auto* rank : *ranks) {
            if (!rank) {
                continue;
            }
            GuildRankInfoData rank_data;
            rank_data.rank = rank->rank();
            rank_data.rank_name = rank->rank_name() ? rank->rank_name()->str() : std::string{};
            if (const auto* members = rank->members()) {
                rank_data.members.reserve(members->size());
                for (const auto* member_name : *members) {
                    rank_data.members.push_back(
                        member_name ? member_name->str() : std::string{});
                }
            }
            data.ranks.push_back(std::move(rank_data));
        }
    }

    if (const auto* wars = guild->war_guilds()) {
        data.war_guilds.reserve(wars->size());
        for (const auto* war : *wars) {
            if (!war) {
                continue;
            }
            GuildWarInfoData war_data;
            war_data.enemy_guild_id = war->enemy_guild_id();
            war_data.start_time = war->start_time();
            war_data.remain_time = war->remain_time();
            data.war_guilds.push_back(war_data);
        }
    }

    if (const auto* allies = guild->ally_guild_ids()) {
        data.ally_guild_ids.reserve(allies->size());
        for (uint32_t ally_id : *allies) {
            data.ally_guild_ids.push_back(ally_id);
        }
    }

    if (const auto* fight_members = guild->fight_members()) {
        data.fight_members.reserve(fight_members->size());
        for (const auto* member_name : *fight_members) {
            data.fight_members.push_back(member_name ? member_name->str() : std::string{});
        }
    }

    callbacks_.on_guild_info_sync(data);
}

void GuildHandler::SendCreateGuildRequest(mir2::client::INetworkManager& manager,
                                          const std::string& guild_name) {
    flatbuffers::FlatBufferBuilder builder;
    const auto guild_name_offset = builder.CreateString(guild_name);
    const auto req = mir2::proto::CreateCreateGuildRequest(builder, guild_name_offset);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildCreateReq, BuildPayload(builder));
}

void GuildHandler::SendJoinGuildRequest(mir2::client::INetworkManager& manager,
                                        uint32_t guild_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateJoinGuildRequest(builder, guild_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildJoinReq, BuildPayload(builder));
}

void GuildHandler::SendLeaveGuildRequest(mir2::client::INetworkManager& manager) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateLeaveGuildRequest(builder);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildLeaveReq, BuildPayload(builder));
}

void GuildHandler::SendKickGuildRequest(mir2::client::INetworkManager& manager,
                                        uint32_t target_character_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateKickGuildRequest(builder, target_character_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildKickReq, BuildPayload(builder));
}

void GuildHandler::SendDeclareWarRequest(mir2::client::INetworkManager& manager,
                                         uint32_t target_guild_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateDeclareWarRequest(builder, target_guild_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildDeclareWarReq, BuildPayload(builder));
}

void GuildHandler::SendCancelWarRequest(mir2::client::INetworkManager& manager,
                                        uint32_t target_guild_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateCancelWarRequest(builder, target_guild_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildCancelWarReq, BuildPayload(builder));
}

void GuildHandler::SendMakeAllyRequest(mir2::client::INetworkManager& manager,
                                       uint32_t target_guild_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateMakeAllianceRequest(builder, target_guild_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildMakeAllyReq, BuildPayload(builder));
}

void GuildHandler::SendBreakAllyRequest(mir2::client::INetworkManager& manager,
                                        uint32_t target_guild_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateBreakAllianceRequest(builder, target_guild_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildBreakAllyReq, BuildPayload(builder));
}

void GuildHandler::SendUpdateNoticeRequest(
    mir2::client::INetworkManager& manager,
    const std::vector<std::string>& notice_lines) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<flatbuffers::String>> notice_offsets;
    notice_offsets.reserve(notice_lines.size());
    for (const auto& line : notice_lines) {
        notice_offsets.push_back(builder.CreateString(line));
    }
    const auto notice_vec = builder.CreateVector(notice_offsets);
    const auto req = mir2::proto::CreateUpdateNoticeRequest(builder, notice_vec);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildUpdateNoticeReq, BuildPayload(builder));
}

void GuildHandler::SendUpdateRankRequest(
    mir2::client::INetworkManager& manager,
    const std::vector<GuildRankUpdateMember>& members) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mir2::proto::RankUpdateMember>> member_offsets;
    member_offsets.reserve(members.size());
    for (const auto& member : members) {
        member_offsets.push_back(
            mir2::proto::CreateRankUpdateMember(builder, member.character_id, member.rank));
    }
    const auto member_vec = builder.CreateVector(member_offsets);
    const auto req = mir2::proto::CreateUpdateRankRequest(builder, member_vec);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kGuildUpdateRankReq, BuildPayload(builder));
}

} // namespace mir2::game::handlers
