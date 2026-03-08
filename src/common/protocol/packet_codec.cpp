#include "common/protocol/packet_codec.h"

#include <cstring>
#include <limits>

#include <flatbuffers/flatbuffers.h>

#include "common/compression.h"
#include "common/enums.h"
#include "chat_generated.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "guild_generated.h"
#include "internal_generated.h"
#include "item_generated.h"
#include "login_generated.h"
#include "mail_generated.h"
#include "party_generated.h"
#include "ranking_generated.h"
#include "system_generated.h"
#include "trade_generated.h"
#include "achievement_generated.h"
#include "auction_generated.h"

namespace mir2::common {

namespace {

constexpr uint32_t kLegacyV1Magic = 0x4D495232;  // "MIR2"

constexpr uint16_t kCrc16Table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

uint16_t UpdateCRC16(uint16_t crc, const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return crc;
    }
    for (size_t i = 0; i < length; ++i) {
        const uint8_t index = static_cast<uint8_t>((crc >> 8) ^ data[i]);
        crc = static_cast<uint16_t>((crc << 8) ^ kCrc16Table[index]);
    }
    return crc;
}

bool IsNpcMessage(uint16_t msg_id) {
    switch (msg_id) {
        case static_cast<uint16_t>(MsgId::kNpcInteractReq):
        case static_cast<uint16_t>(MsgId::kNpcInteractRsp):
        case static_cast<uint16_t>(MsgId::kNpcDialogShow):
        case static_cast<uint16_t>(MsgId::kNpcMenuSelect):
        case static_cast<uint16_t>(MsgId::kNpcShopOpen):
        case static_cast<uint16_t>(MsgId::kNpcShopClose):
        case static_cast<uint16_t>(MsgId::kNpcQuestAccept):
        case static_cast<uint16_t>(MsgId::kNpcQuestComplete):
            return true;
        default:
            return false;
    }
}

bool IsOptionalPayload(uint16_t msg_id) {
    switch (msg_id) {
        case static_cast<uint16_t>(MsgId::kHeartbeat):
        case static_cast<uint16_t>(MsgId::kLogout):
        case static_cast<uint16_t>(MsgId::kKcpUpgradeRequest):
            return true;
        default:
            return false;
    }
}

bool VerifyFlatBufferPayload(uint16_t msg_id, const uint8_t* data, size_t size) {
    if (IsNpcMessage(msg_id)) {
        // NPC payloads are JSON encoded; validated by npc_message_codec.
        return true;
    }

    if (!data || size == 0) {
        return IsOptionalPayload(msg_id) || msg_id == 0;
    }

    flatbuffers::Verifier verifier(data, size);
    switch (msg_id) {
        case static_cast<uint16_t>(MsgId::kLoginReq):
            return verifier.VerifyBuffer<mir2::proto::LoginReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kLoginRsp):
            return verifier.VerifyBuffer<mir2::proto::LoginRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kLogout):
            return verifier.VerifyBuffer<mir2::proto::LogoutReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kCreateRoleReq):
            return verifier.VerifyBuffer<mir2::proto::CreateRoleReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kCreateRoleRsp):
            return verifier.VerifyBuffer<mir2::proto::CreateRoleRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kSelectRoleReq):
            return verifier.VerifyBuffer<mir2::proto::SelectRoleReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kSelectRoleRsp):
            return verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kRoleListReq):
            return verifier.VerifyBuffer<mir2::proto::RoleListReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kRoleListRsp):
            return verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kRoleUpdate):
            return verifier.VerifyBuffer<mir2::proto::RoleUpdate>(nullptr);
        case static_cast<uint16_t>(MsgId::kEnterGameRsp):
            return verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMoveReq):
            return verifier.VerifyBuffer<mir2::proto::MoveReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kMoveRsp):
            return verifier.VerifyBuffer<mir2::proto::MoveRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kEntityEnter):
            return verifier.VerifyBuffer<mir2::proto::EntityEnter>(nullptr);
        case static_cast<uint16_t>(MsgId::kEntityLeave):
            return verifier.VerifyBuffer<mir2::proto::EntityLeave>(nullptr);
        case static_cast<uint16_t>(MsgId::kEntityMove):
            return verifier.VerifyBuffer<mir2::proto::EntityMove>(nullptr);
        case static_cast<uint16_t>(MsgId::kEntityUpdate):
            return verifier.VerifyBuffer<mir2::proto::EntityUpdate>(nullptr);
        case static_cast<uint16_t>(MsgId::kChangeMap):
            return verifier.VerifyBuffer<mir2::proto::ChangeMap>(nullptr);
        case static_cast<uint16_t>(MsgId::kTeleport):
            return verifier.VerifyBuffer<mir2::proto::Teleport>(nullptr);
        case static_cast<uint16_t>(MsgId::kStateSync):
            return verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr);
        case static_cast<uint16_t>(MsgId::kBonusPointReq):
            return verifier.VerifyBuffer<mir2::proto::BonusPointReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kBonusPointRsp):
            return verifier.VerifyBuffer<mir2::proto::BonusPointRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAttackReq):
            return verifier.VerifyBuffer<mir2::proto::AttackReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAttackRsp):
            return verifier.VerifyBuffer<mir2::proto::AttackRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kSkillReq):
            return verifier.VerifyBuffer<mir2::proto::SkillReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kSkillRsp):
            return verifier.VerifyBuffer<mir2::proto::SkillRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kDamage):
            return verifier.VerifyBuffer<mir2::proto::Damage>(nullptr);
        case static_cast<uint16_t>(MsgId::kDeath):
            return verifier.VerifyBuffer<mir2::proto::Death>(nullptr);
        case static_cast<uint16_t>(MsgId::kRespawn):
            return verifier.VerifyBuffer<mir2::proto::Respawn>(nullptr);
        case static_cast<uint16_t>(MsgId::kBuffAdd):
            return verifier.VerifyBuffer<mir2::proto::BuffAdd>(nullptr);
        case static_cast<uint16_t>(MsgId::kBuffRemove):
            return verifier.VerifyBuffer<mir2::proto::BuffRemove>(nullptr);
        case static_cast<uint16_t>(MsgId::kSkillEffect):
            return verifier.VerifyBuffer<mir2::proto::SkillEffect>(nullptr);
        case static_cast<uint16_t>(MsgId::kPlayEffect):
            return verifier.VerifyBuffer<mir2::proto::PlayEffect>(nullptr);
        case static_cast<uint16_t>(MsgId::kPlaySound):
            return verifier.VerifyBuffer<mir2::proto::PlaySound>(nullptr);
        case static_cast<uint16_t>(MsgId::kInventoryUpdate):
            return verifier.VerifyBuffer<mir2::proto::InventoryUpdate>(nullptr);
        case static_cast<uint16_t>(MsgId::kUseItemReq):
            return verifier.VerifyBuffer<mir2::proto::UseItemReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kUseItemRsp):
            return verifier.VerifyBuffer<mir2::proto::UseItemRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kDropItemReq):
            return verifier.VerifyBuffer<mir2::proto::DropItemReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kDropItemRsp):
            return verifier.VerifyBuffer<mir2::proto::DropItemRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kPickupItemReq):
            return verifier.VerifyBuffer<mir2::proto::PickupItemReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kPickupItemRsp):
            return verifier.VerifyBuffer<mir2::proto::PickupItemRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kEquipReq):
            return verifier.VerifyBuffer<mir2::proto::EquipReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kUnequipReq):
            return verifier.VerifyBuffer<mir2::proto::UnequipReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kEquipRsp):
            return verifier.VerifyBuffer<mir2::proto::EquipRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kUnequipRsp):
            return verifier.VerifyBuffer<mir2::proto::UnequipRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kChatReq): {
            if (verifier.VerifyBuffer<mir2::proto::ChatReq>(nullptr)) {
                return true;
            }
            flatbuffers::Verifier second_verifier(data, size);
            return second_verifier.VerifyBuffer<mir2::proto::ChatMessage>(nullptr);
        }
        case static_cast<uint16_t>(MsgId::kChatRsp):
            return verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kPrivateChat):
        case static_cast<uint16_t>(MsgId::kGuildChat):
        case static_cast<uint16_t>(MsgId::kTeamChat):
        case static_cast<uint16_t>(MsgId::kAreaChat):
        case static_cast<uint16_t>(MsgId::kSystemMsg):
            return verifier.VerifyBuffer<mir2::proto::ChatMessage>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildCreateReq):
            return verifier.VerifyBuffer<mir2::proto::CreateGuildRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildCreateRsp):
            return verifier.VerifyBuffer<mir2::proto::CreateGuildResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildJoinReq):
            return verifier.VerifyBuffer<mir2::proto::JoinGuildRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildJoinRsp):
            return verifier.VerifyBuffer<mir2::proto::JoinGuildResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildLeaveReq):
            return verifier.VerifyBuffer<mir2::proto::LeaveGuildRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildLeaveRsp):
            return verifier.VerifyBuffer<mir2::proto::LeaveGuildResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildKickReq):
            return verifier.VerifyBuffer<mir2::proto::KickGuildRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildKickRsp):
            return verifier.VerifyBuffer<mir2::proto::KickGuildResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildDeclareWarReq):
            return verifier.VerifyBuffer<mir2::proto::DeclareWarRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildDeclareWarRsp):
            return verifier.VerifyBuffer<mir2::proto::DeclareWarResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildCancelWarReq):
            return verifier.VerifyBuffer<mir2::proto::CancelWarRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildCancelWarRsp):
            return verifier.VerifyBuffer<mir2::proto::CancelWarResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildMakeAllyReq):
            return verifier.VerifyBuffer<mir2::proto::MakeAllianceRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildMakeAllyRsp):
            return verifier.VerifyBuffer<mir2::proto::MakeAllianceResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildBreakAllyReq):
            return verifier.VerifyBuffer<mir2::proto::BreakAllianceRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildBreakAllyRsp):
            return verifier.VerifyBuffer<mir2::proto::BreakAllianceResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildUpdateNoticeReq):
            return verifier.VerifyBuffer<mir2::proto::UpdateNoticeRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildUpdateNoticeRsp):
            return verifier.VerifyBuffer<mir2::proto::UpdateNoticeResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildUpdateRankReq):
            return verifier.VerifyBuffer<mir2::proto::UpdateRankRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildUpdateRankRsp):
            return verifier.VerifyBuffer<mir2::proto::UpdateRankResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kGuildInfoSync):
            return verifier.VerifyBuffer<mir2::proto::GuildInfoSync>(nullptr);
        case static_cast<uint16_t>(MsgId::kHeartbeat):
            return verifier.VerifyBuffer<mir2::proto::Heartbeat>(nullptr);
        case static_cast<uint16_t>(MsgId::kHeartbeatRsp):
            return verifier.VerifyBuffer<mir2::proto::HeartbeatRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kKick):
            return verifier.VerifyBuffer<mir2::proto::Kick>(nullptr);
        case static_cast<uint16_t>(MsgId::kServerNotice):
            return verifier.VerifyBuffer<mir2::proto::ServerNotice>(nullptr);
        case static_cast<uint16_t>(MsgId::kKcpUpgradeRequest):
            return verifier.VerifyBuffer<mir2::proto::KcpUpgradeRequest>(nullptr);
        case static_cast<uint16_t>(MsgId::kKcpUpgradeResponse):
            return verifier.VerifyBuffer<mir2::proto::KcpUpgradeResponse>(nullptr);
        case static_cast<uint16_t>(MsgId::kKcpHeartbeat):
            return verifier.VerifyBuffer<mir2::proto::KcpHeartbeat>(nullptr);
        case static_cast<uint16_t>(MsgId::kKcpHeartbeatAck):
            return verifier.VerifyBuffer<mir2::proto::KcpHeartbeatAck>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeReq):
            return verifier.VerifyBuffer<mir2::proto::TradeReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeRsp):
            return verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeAddItemReq):
            return verifier.VerifyBuffer<mir2::proto::TradeAddItemReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeAddItemRsp):
            return verifier.VerifyBuffer<mir2::proto::TradeAddItemRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeSetGoldReq):
            return verifier.VerifyBuffer<mir2::proto::TradeSetGoldReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeSetGoldRsp):
            return verifier.VerifyBuffer<mir2::proto::TradeSetGoldRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeConfirmReq):
            return verifier.VerifyBuffer<mir2::proto::TradeConfirmReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeConfirmRsp):
            return verifier.VerifyBuffer<mir2::proto::TradeConfirmRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeCancelReq):
            return verifier.VerifyBuffer<mir2::proto::TradeCancelReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeCancelRsp):
            return verifier.VerifyBuffer<mir2::proto::TradeCancelRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeUpdate):
            return verifier.VerifyBuffer<mir2::proto::TradeUpdate>(nullptr);
        case static_cast<uint16_t>(MsgId::kTradeComplete):
            return verifier.VerifyBuffer<mir2::proto::TradeComplete>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyInviteReq):
            return verifier.VerifyBuffer<mir2::proto::PartyInviteReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyInviteRsp):
            return verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyJoinReq):
            return verifier.VerifyBuffer<mir2::proto::PartyJoinReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyJoinRsp):
            return verifier.VerifyBuffer<mir2::proto::PartyJoinRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyLeaveReq):
            return verifier.VerifyBuffer<mir2::proto::PartyLeaveReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyLeaveRsp):
            return verifier.VerifyBuffer<mir2::proto::PartyLeaveRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyKickReq):
            return verifier.VerifyBuffer<mir2::proto::PartyKickReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyKickRsp):
            return verifier.VerifyBuffer<mir2::proto::PartyKickRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kPartyUpdate):
            return verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr);
        case static_cast<uint16_t>(MsgId::kRankingReq):
            return verifier.VerifyBuffer<mir2::proto::RankingReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kRankingRsp):
            return verifier.VerifyBuffer<mir2::proto::RankingRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kRankingMyRankReq):
            return verifier.VerifyBuffer<mir2::proto::RankingMyRankReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kRankingMyRankRsp):
            return verifier.VerifyBuffer<mir2::proto::RankingMyRankRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailSendReq):
            return verifier.VerifyBuffer<mir2::proto::MailSendReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailSendRsp):
            return verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailListReq):
            return verifier.VerifyBuffer<mir2::proto::MailListReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailListRsp):
            return verifier.VerifyBuffer<mir2::proto::MailListRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailReadReq):
            return verifier.VerifyBuffer<mir2::proto::MailReadReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailReadRsp):
            return verifier.VerifyBuffer<mir2::proto::MailReadRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailDeleteReq):
            return verifier.VerifyBuffer<mir2::proto::MailDeleteReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailDeleteRsp):
            return verifier.VerifyBuffer<mir2::proto::MailDeleteRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailClaimReq):
            return verifier.VerifyBuffer<mir2::proto::MailClaimReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailClaimRsp):
            return verifier.VerifyBuffer<mir2::proto::MailClaimRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kMailNotify):
            return verifier.VerifyBuffer<mir2::proto::MailNotify>(nullptr);
        case static_cast<uint16_t>(MsgId::kAchievementListReq):
            return verifier.VerifyBuffer<mir2::proto::AchievementListReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAchievementListRsp):
            return verifier.VerifyBuffer<mir2::proto::AchievementListRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAchievementClaimReq):
            return verifier.VerifyBuffer<mir2::proto::AchievementClaimReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAchievementClaimRsp):
            return verifier.VerifyBuffer<mir2::proto::AchievementClaimRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAchievementUpdate):
            return verifier.VerifyBuffer<mir2::proto::AchievementUpdate>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionListReq):
            return verifier.VerifyBuffer<mir2::proto::AuctionListReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionListRsp):
            return verifier.VerifyBuffer<mir2::proto::AuctionListRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionSellReq):
            return verifier.VerifyBuffer<mir2::proto::AuctionSellReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionSellRsp):
            return verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionBuyReq):
            return verifier.VerifyBuffer<mir2::proto::AuctionBuyReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionBuyRsp):
            return verifier.VerifyBuffer<mir2::proto::AuctionBuyRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionCancelReq):
            return verifier.VerifyBuffer<mir2::proto::AuctionCancelReq>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionCancelRsp):
            return verifier.VerifyBuffer<mir2::proto::AuctionCancelRsp>(nullptr);
        case static_cast<uint16_t>(MsgId::kAuctionNotify):
            return verifier.VerifyBuffer<mir2::proto::AuctionNotify>(nullptr);
        case static_cast<uint16_t>(InternalMsgId::kServiceHello):
            return verifier.VerifyBuffer<mir2::internal::ServiceHello>(nullptr);
        case static_cast<uint16_t>(InternalMsgId::kServiceHelloAck):
            return verifier.VerifyBuffer<mir2::internal::ServiceHelloAck>(nullptr);
        case static_cast<uint16_t>(InternalMsgId::kRoutedMessage):
            return verifier.VerifyBuffer<mir2::internal::RoutedMessage>(nullptr);
        case static_cast<uint16_t>(InternalMsgId::kContextRestore): {
            if (verifier.VerifyBuffer<mir2::proto::ContextRestore>(nullptr)) {
                return true;
            }
            flatbuffers::Verifier second_verifier(data, size);
            return second_verifier.VerifyBuffer<mir2::proto::ContextRestoreResponse>(nullptr);
        }
        case static_cast<uint16_t>(InternalMsgId::kLogicReady):
            return verifier.VerifyBuffer<mir2::proto::LogicReady>(nullptr);
        case static_cast<uint16_t>(InternalMsgId::kBackpressureControl):
            return verifier.VerifyBuffer<mir2::proto::BackpressureControl>(nullptr);
        case static_cast<uint16_t>(InternalMsgId::kKcpReset):
            return verifier.VerifyBuffer<mir2::proto::KcpReset>(nullptr);
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE):
            return verifier.VerifyBuffer<mir2::proto::CreateGuildRequest>(nullptr);
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN):
            return verifier.VerifyBuffer<mir2::proto::JoinGuildRequest>(nullptr);
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE):
            return verifier.VerifyBuffer<mir2::proto::LeaveGuildRequest>(nullptr);
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR):
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR):
            return verifier.VerifyBuffer<mir2::proto::DeclareWarRequest>(nullptr);
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY):
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY):
            return verifier.VerifyBuffer<mir2::proto::MakeAllianceRequest>(nullptr);
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK):
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE):
        case static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK):
            // Guild schema currently has no dedicated request tables for these
            // opcodes; keep them pass-through until request schemas are added.
            return true;
        default:
            return false;
    }
}

}  // namespace

std::array<uint8_t, PacketHeaderV2::kSize> PacketHeaderV2::ToBytes() const {
    std::array<uint8_t, kSize> buffer{};
    size_t offset = 0;
    std::memcpy(buffer.data() + offset, &magic, sizeof(magic));
    offset += sizeof(magic);
    std::memcpy(buffer.data() + offset, &version, sizeof(version));
    offset += sizeof(version);
    std::memcpy(buffer.data() + offset, &flags, sizeof(flags));
    offset += sizeof(flags);
    std::memcpy(buffer.data() + offset, &msg_id, sizeof(msg_id));
    offset += sizeof(msg_id);
    std::memcpy(buffer.data() + offset, &payload_size, sizeof(payload_size));
    offset += sizeof(payload_size);
    std::memcpy(buffer.data() + offset, &sequence, sizeof(sequence));
    offset += sizeof(sequence);
    std::memcpy(buffer.data() + offset, &checksum, sizeof(checksum));
    return buffer;
}

bool PacketHeaderV2::FromBytes(const uint8_t* data, size_t len, PacketHeaderV2* out) {
    if (!data || len < kSize || !out) {
        return false;
    }
    size_t offset = 0;
    std::memcpy(&out->magic, data + offset, sizeof(out->magic));
    offset += sizeof(out->magic);
    std::memcpy(&out->version, data + offset, sizeof(out->version));
    offset += sizeof(out->version);
    std::memcpy(&out->flags, data + offset, sizeof(out->flags));
    offset += sizeof(out->flags);
    std::memcpy(&out->msg_id, data + offset, sizeof(out->msg_id));
    offset += sizeof(out->msg_id);
    std::memcpy(&out->payload_size, data + offset, sizeof(out->payload_size));
    offset += sizeof(out->payload_size);
    std::memcpy(&out->sequence, data + offset, sizeof(out->sequence));
    offset += sizeof(out->sequence);
    std::memcpy(&out->checksum, data + offset, sizeof(out->checksum));
    return out->magic == kMagic;
}

uint16_t CalcCRC16(const uint8_t* data, size_t length) {
    return UpdateCRC16(0xFFFF, data, length);
}

std::vector<uint8_t> EncodePacketV2(uint16_t msg_id,
                                    const uint8_t* payload,
                                    size_t payload_size,
                                    uint16_t sequence,
                                    uint8_t flags) {
    if (payload_size > kMaxPayloadSize ||
        payload_size > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return {};
    }

    std::vector<uint8_t> compressed_payload;
    const uint8_t compressed_flag = PacketHeaderV2::kFlagCompressed;
    const uint8_t* payload_data = payload;
    size_t payload_data_size = payload_size;
    if (payload && payload_size > kCompressionThreshold) {
        compressed_payload = CompressLZ4(payload, payload_size);
        if (!compressed_payload.empty() &&
            compressed_payload.size() * 10 < payload_size * 9) {
            payload_data = compressed_payload.data();
            payload_data_size = compressed_payload.size();
            flags = static_cast<uint8_t>(flags | compressed_flag);
        }
    }

    PacketHeaderV2 header;
    header.msg_id = msg_id;
    header.payload_size = static_cast<uint32_t>(payload_data_size);
    header.sequence = sequence;
    header.flags = flags;

    auto header_bytes = header.ToBytes();
    uint16_t crc = UpdateCRC16(0xFFFF, header_bytes.data(),
                               PacketHeaderV2::kSize - sizeof(header.checksum));
    crc = UpdateCRC16(crc, payload_data, payload_data_size);
    header.checksum = crc;
    header_bytes = header.ToBytes();

    std::vector<uint8_t> buffer;
    buffer.reserve(PacketHeaderV2::kSize + payload_data_size);
    buffer.insert(buffer.end(), header_bytes.begin(), header_bytes.end());
    if (payload_data && payload_data_size > 0) {
        buffer.insert(buffer.end(), payload_data, payload_data + payload_data_size);
    }
    return buffer;
}

DecodeStatus DecodePacketV2(const uint8_t* data,
                            size_t length,
                            NetworkPacket* out_packet,
                            uint16_t* out_sequence,
                            uint8_t* out_flags) {
    if (!data || !out_packet || length < PacketHeaderV2::kSize) {
        return DecodeStatus::kTruncated;
    }

    uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic == kLegacyV1Magic) {
        return DecodeStatus::kProtocolNotSupported;
    }

    PacketHeaderV2 header{};
    if (!PacketHeaderV2::FromBytes(data, length, &header)) {
        return DecodeStatus::kInvalidMagic;
    }
    if (header.version != PacketHeaderV2::kVersion) {
        return DecodeStatus::kInvalidVersion;
    }

    if (header.payload_size > kMaxPayloadSize ||
        header.payload_size > std::numeric_limits<size_t>::max() - PacketHeaderV2::kSize) {
        return DecodeStatus::kPayloadTooLarge;
    }

    const size_t expected_size = PacketHeaderV2::kSize + static_cast<size_t>(header.payload_size);
    if (length < expected_size) {
        return DecodeStatus::kTruncated;
    }

    uint16_t crc = UpdateCRC16(0xFFFF, data, PacketHeaderV2::kSize - sizeof(header.checksum));
    crc = UpdateCRC16(crc, data + PacketHeaderV2::kSize, header.payload_size);
    if (crc != header.checksum) {
        return DecodeStatus::kInvalidChecksum;
    }

    out_packet->msg_id = header.msg_id;
    const uint8_t compressed_flag = PacketHeaderV2::kFlagCompressed;
    if ((header.flags & compressed_flag) != 0) {
        std::vector<uint8_t> decompressed;
        if (!DecompressLZ4(data + PacketHeaderV2::kSize,
                           header.payload_size,
                           &decompressed,
                           kMaxPayloadSize)) {
            return DecodeStatus::kDecompressFailed;
        }
        if (!VerifyFlatBufferPayload(header.msg_id,
                                     decompressed.data(),
                                     decompressed.size())) {
            return DecodeStatus::kInvalidPayload;
        }
        out_packet->payload = std::move(decompressed);
    } else {
        const uint8_t* payload_data = data + PacketHeaderV2::kSize;
        const size_t payload_size = static_cast<size_t>(header.payload_size);
        if (!VerifyFlatBufferPayload(header.msg_id, payload_data, payload_size)) {
            return DecodeStatus::kInvalidPayload;
        }
        out_packet->payload.assign(payload_data, payload_data + payload_size);
    }
    if (out_sequence) {
        *out_sequence = header.sequence;
    }
    if (out_flags) {
        *out_flags = header.flags;
    }
    return DecodeStatus::kOk;
}

bool ValidateChannelFlag(uint8_t flags, ChannelType actual_channel) {
    const bool is_kcp_flagged =
        (flags & PacketHeaderV2::kFlagChannelKcp) != 0;
    switch (actual_channel) {
        case ChannelType::kTcp:
            return !is_kcp_flagged;
        case ChannelType::kKcp:
            return is_kcp_flagged;
        default:
            return false;
    }
}

ProtocolVersion DetectProtocolVersion(const uint8_t* magic_bytes) {
    if (!magic_bytes) {
        return ProtocolVersion::kV1;
    }
    uint32_t magic = 0;
    std::memcpy(&magic, magic_bytes, sizeof(magic));
    if (magic == PacketHeaderV2::kMagic) {
        return ProtocolVersion::kV2;
    }
    return ProtocolVersion::kV1;
}

}  // namespace mir2::common
