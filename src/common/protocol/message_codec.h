/**
 * @file message_codec.h
 * @brief Shared FlatBuffers message codec helpers.
 */

#ifndef LEGEND2_COMMON_PROTOCOL_MESSAGE_CODEC_H
#define LEGEND2_COMMON_PROTOCOL_MESSAGE_CODEC_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chat_generated.h"
#include "common/enums.h"
#include "common/types/constants.h"
#include "combat_generated.h"
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

namespace mir2::common {

/**
 * @brief Encode/decode status for message helpers.
 */
enum class MessageCodecStatus : uint8_t {
    kOk = 0,
    kInvalidMsgId,
    kInvalidPayload,
    kMissingField,
    kStringTooLong,
    kValueOutOfRange
};

constexpr uint16_t kLoginRequestMsgId = static_cast<uint16_t>(MsgId::kLoginReq);
constexpr uint16_t kLoginResponseMsgId = static_cast<uint16_t>(MsgId::kLoginRsp);
constexpr uint16_t kCreateCharacterRequestMsgId = static_cast<uint16_t>(MsgId::kCreateRoleReq);
constexpr uint16_t kCreateCharacterResponseMsgId = static_cast<uint16_t>(MsgId::kCreateRoleRsp);
constexpr uint16_t kMoveRequestMsgId = static_cast<uint16_t>(MsgId::kMoveReq);
constexpr uint16_t kMoveResponseMsgId = static_cast<uint16_t>(MsgId::kMoveRsp);
constexpr uint16_t kAttackRequestMsgId = static_cast<uint16_t>(MsgId::kAttackReq);
constexpr uint16_t kAttackResponseMsgId = static_cast<uint16_t>(MsgId::kAttackRsp);
constexpr uint16_t kSkillRequestMsgId = static_cast<uint16_t>(MsgId::kSkillReq);
constexpr uint16_t kSkillResponseMsgId = static_cast<uint16_t>(MsgId::kSkillRsp);
constexpr uint16_t kUseItemRequestMsgId = static_cast<uint16_t>(MsgId::kUseItemReq);
constexpr uint16_t kUseItemResponseMsgId = static_cast<uint16_t>(MsgId::kUseItemRsp);
constexpr uint16_t kPickupItemRequestMsgId = static_cast<uint16_t>(MsgId::kPickupItemReq);
constexpr uint16_t kPickupItemResponseMsgId = static_cast<uint16_t>(MsgId::kPickupItemRsp);
constexpr uint16_t kDropItemRequestMsgId = static_cast<uint16_t>(MsgId::kDropItemReq);
constexpr uint16_t kDropItemResponseMsgId = static_cast<uint16_t>(MsgId::kDropItemRsp);
constexpr uint16_t kEquipRequestMsgId = static_cast<uint16_t>(MsgId::kEquipReq);
constexpr uint16_t kEquipResponseMsgId = static_cast<uint16_t>(MsgId::kEquipRsp);
constexpr uint16_t kUnequipRequestMsgId = static_cast<uint16_t>(MsgId::kUnequipReq);
constexpr uint16_t kUnequipResponseMsgId = static_cast<uint16_t>(MsgId::kUnequipRsp);
constexpr uint16_t kChatRequestMsgId = static_cast<uint16_t>(MsgId::kChatReq);
constexpr uint16_t kChatResponseMsgId = static_cast<uint16_t>(MsgId::kChatRsp);
constexpr uint16_t kGuildCreateRequestMsgId = static_cast<uint16_t>(MsgId::kGuildCreateReq);
constexpr uint16_t kGuildCreateResponseMsgId = static_cast<uint16_t>(MsgId::kGuildCreateRsp);
constexpr uint16_t kGuildJoinRequestMsgId = static_cast<uint16_t>(MsgId::kGuildJoinReq);
constexpr uint16_t kGuildJoinResponseMsgId = static_cast<uint16_t>(MsgId::kGuildJoinRsp);
constexpr uint16_t kGuildLeaveRequestMsgId = static_cast<uint16_t>(MsgId::kGuildLeaveReq);
constexpr uint16_t kGuildLeaveResponseMsgId = static_cast<uint16_t>(MsgId::kGuildLeaveRsp);
constexpr uint16_t kGuildKickRequestMsgId = static_cast<uint16_t>(MsgId::kGuildKickReq);
constexpr uint16_t kGuildKickResponseMsgId = static_cast<uint16_t>(MsgId::kGuildKickRsp);
constexpr uint16_t kGuildDeclareWarRequestMsgId = static_cast<uint16_t>(MsgId::kGuildDeclareWarReq);
constexpr uint16_t kGuildDeclareWarResponseMsgId = static_cast<uint16_t>(MsgId::kGuildDeclareWarRsp);
constexpr uint16_t kGuildCancelWarRequestMsgId = static_cast<uint16_t>(MsgId::kGuildCancelWarReq);
constexpr uint16_t kGuildCancelWarResponseMsgId = static_cast<uint16_t>(MsgId::kGuildCancelWarRsp);
constexpr uint16_t kGuildMakeAllyRequestMsgId = static_cast<uint16_t>(MsgId::kGuildMakeAllyReq);
constexpr uint16_t kGuildMakeAllyResponseMsgId = static_cast<uint16_t>(MsgId::kGuildMakeAllyRsp);
constexpr uint16_t kGuildBreakAllyRequestMsgId = static_cast<uint16_t>(MsgId::kGuildBreakAllyReq);
constexpr uint16_t kGuildBreakAllyResponseMsgId = static_cast<uint16_t>(MsgId::kGuildBreakAllyRsp);
constexpr uint16_t kTradeRequestMsgId = static_cast<uint16_t>(MsgId::kTradeReq);
constexpr uint16_t kTradeResponseMsgId = static_cast<uint16_t>(MsgId::kTradeRsp);
constexpr uint16_t kTradeAddItemRequestMsgId = static_cast<uint16_t>(MsgId::kTradeAddItemReq);
constexpr uint16_t kTradeAddItemResponseMsgId = static_cast<uint16_t>(MsgId::kTradeAddItemRsp);
constexpr uint16_t kTradeSetGoldRequestMsgId = static_cast<uint16_t>(MsgId::kTradeSetGoldReq);
constexpr uint16_t kTradeSetGoldResponseMsgId = static_cast<uint16_t>(MsgId::kTradeSetGoldRsp);
constexpr uint16_t kTradeConfirmRequestMsgId = static_cast<uint16_t>(MsgId::kTradeConfirmReq);
constexpr uint16_t kTradeConfirmResponseMsgId = static_cast<uint16_t>(MsgId::kTradeConfirmRsp);
constexpr uint16_t kTradeCancelRequestMsgId = static_cast<uint16_t>(MsgId::kTradeCancelReq);
constexpr uint16_t kTradeCancelResponseMsgId = static_cast<uint16_t>(MsgId::kTradeCancelRsp);
constexpr uint16_t kTradeUpdateMsgId = static_cast<uint16_t>(MsgId::kTradeUpdate);
constexpr uint16_t kTradeCompleteMsgId = static_cast<uint16_t>(MsgId::kTradeComplete);
constexpr uint16_t kPartyInviteRequestMsgId = static_cast<uint16_t>(MsgId::kPartyInviteReq);
constexpr uint16_t kPartyInviteResponseMsgId = static_cast<uint16_t>(MsgId::kPartyInviteRsp);
constexpr uint16_t kPartyJoinRequestMsgId = static_cast<uint16_t>(MsgId::kPartyJoinReq);
constexpr uint16_t kPartyJoinResponseMsgId = static_cast<uint16_t>(MsgId::kPartyJoinRsp);
constexpr uint16_t kPartyLeaveRequestMsgId = static_cast<uint16_t>(MsgId::kPartyLeaveReq);
constexpr uint16_t kPartyLeaveResponseMsgId = static_cast<uint16_t>(MsgId::kPartyLeaveRsp);
constexpr uint16_t kPartyKickRequestMsgId = static_cast<uint16_t>(MsgId::kPartyKickReq);
constexpr uint16_t kPartyKickResponseMsgId = static_cast<uint16_t>(MsgId::kPartyKickRsp);
constexpr uint16_t kPartyUpdateMsgId = static_cast<uint16_t>(MsgId::kPartyUpdate);
constexpr uint16_t kGuildUpdateNoticeRequestMsgId =
    static_cast<uint16_t>(MsgId::kGuildUpdateNoticeReq);
constexpr uint16_t kGuildUpdateNoticeResponseMsgId =
    static_cast<uint16_t>(MsgId::kGuildUpdateNoticeRsp);
constexpr uint16_t kGuildUpdateRankRequestMsgId =
    static_cast<uint16_t>(MsgId::kGuildUpdateRankReq);
constexpr uint16_t kGuildUpdateRankResponseMsgId =
    static_cast<uint16_t>(MsgId::kGuildUpdateRankRsp);
constexpr uint16_t kGuildInfoSyncMsgId = static_cast<uint16_t>(MsgId::kGuildInfoSync);
constexpr uint16_t kRankingRequestMsgId = static_cast<uint16_t>(MsgId::kRankingReq);
constexpr uint16_t kRankingResponseMsgId = static_cast<uint16_t>(MsgId::kRankingRsp);
constexpr uint16_t kRankingMyRankRequestMsgId = static_cast<uint16_t>(MsgId::kRankingMyRankReq);
constexpr uint16_t kRankingMyRankResponseMsgId = static_cast<uint16_t>(MsgId::kRankingMyRankRsp);
constexpr uint16_t kMailSendRequestMsgId = static_cast<uint16_t>(MsgId::kMailSendReq);
constexpr uint16_t kMailSendResponseMsgId = static_cast<uint16_t>(MsgId::kMailSendRsp);
constexpr uint16_t kMailListRequestMsgId = static_cast<uint16_t>(MsgId::kMailListReq);
constexpr uint16_t kMailListResponseMsgId = static_cast<uint16_t>(MsgId::kMailListRsp);
constexpr uint16_t kMailReadRequestMsgId = static_cast<uint16_t>(MsgId::kMailReadReq);
constexpr uint16_t kMailReadResponseMsgId = static_cast<uint16_t>(MsgId::kMailReadRsp);
constexpr uint16_t kMailDeleteRequestMsgId = static_cast<uint16_t>(MsgId::kMailDeleteReq);
constexpr uint16_t kMailDeleteResponseMsgId = static_cast<uint16_t>(MsgId::kMailDeleteRsp);
constexpr uint16_t kMailClaimRequestMsgId = static_cast<uint16_t>(MsgId::kMailClaimReq);
constexpr uint16_t kMailClaimResponseMsgId = static_cast<uint16_t>(MsgId::kMailClaimRsp);
constexpr uint16_t kMailNotifyMsgId = static_cast<uint16_t>(MsgId::kMailNotify);
constexpr uint16_t kAchievementListRequestMsgId =
    static_cast<uint16_t>(MsgId::kAchievementListReq);
constexpr uint16_t kAchievementListResponseMsgId =
    static_cast<uint16_t>(MsgId::kAchievementListRsp);
constexpr uint16_t kAchievementClaimRequestMsgId =
    static_cast<uint16_t>(MsgId::kAchievementClaimReq);
constexpr uint16_t kAchievementClaimResponseMsgId =
    static_cast<uint16_t>(MsgId::kAchievementClaimRsp);
constexpr uint16_t kAchievementUpdateMsgId = static_cast<uint16_t>(MsgId::kAchievementUpdate);
constexpr uint16_t kAuctionListRequestMsgId = static_cast<uint16_t>(MsgId::kAuctionListReq);
constexpr uint16_t kAuctionListResponseMsgId = static_cast<uint16_t>(MsgId::kAuctionListRsp);
constexpr uint16_t kAuctionSellRequestMsgId = static_cast<uint16_t>(MsgId::kAuctionSellReq);
constexpr uint16_t kAuctionSellResponseMsgId = static_cast<uint16_t>(MsgId::kAuctionSellRsp);
constexpr uint16_t kAuctionBuyRequestMsgId = static_cast<uint16_t>(MsgId::kAuctionBuyReq);
constexpr uint16_t kAuctionBuyResponseMsgId = static_cast<uint16_t>(MsgId::kAuctionBuyRsp);
constexpr uint16_t kAuctionCancelRequestMsgId = static_cast<uint16_t>(MsgId::kAuctionCancelReq);
constexpr uint16_t kAuctionCancelResponseMsgId =
    static_cast<uint16_t>(MsgId::kAuctionCancelRsp);
constexpr uint16_t kAuctionNotifyMsgId = static_cast<uint16_t>(MsgId::kAuctionNotify);

inline constexpr std::array<MsgId, 93> kMessageCodecManagedMsgIds = {
    MsgId::kLoginReq,
    MsgId::kLoginRsp,
    MsgId::kCreateRoleReq,
    MsgId::kCreateRoleRsp,
    MsgId::kMoveReq,
    MsgId::kMoveRsp,
    MsgId::kAttackReq,
    MsgId::kAttackRsp,
    MsgId::kSkillReq,
    MsgId::kSkillRsp,
    MsgId::kUseItemReq,
    MsgId::kUseItemRsp,
    MsgId::kPickupItemReq,
    MsgId::kPickupItemRsp,
    MsgId::kDropItemReq,
    MsgId::kDropItemRsp,
    MsgId::kEquipReq,
    MsgId::kEquipRsp,
    MsgId::kUnequipReq,
    MsgId::kUnequipRsp,
    MsgId::kChatReq,
    MsgId::kChatRsp,
    MsgId::kGuildCreateReq,
    MsgId::kGuildCreateRsp,
    MsgId::kGuildJoinReq,
    MsgId::kGuildJoinRsp,
    MsgId::kGuildLeaveReq,
    MsgId::kGuildLeaveRsp,
    MsgId::kGuildKickReq,
    MsgId::kGuildKickRsp,
    MsgId::kGuildDeclareWarReq,
    MsgId::kGuildDeclareWarRsp,
    MsgId::kGuildCancelWarReq,
    MsgId::kGuildCancelWarRsp,
    MsgId::kGuildMakeAllyReq,
    MsgId::kGuildMakeAllyRsp,
    MsgId::kGuildBreakAllyReq,
    MsgId::kGuildBreakAllyRsp,
    MsgId::kTradeReq,
    MsgId::kTradeRsp,
    MsgId::kTradeAddItemReq,
    MsgId::kTradeAddItemRsp,
    MsgId::kTradeSetGoldReq,
    MsgId::kTradeSetGoldRsp,
    MsgId::kTradeConfirmReq,
    MsgId::kTradeConfirmRsp,
    MsgId::kTradeCancelReq,
    MsgId::kTradeCancelRsp,
    MsgId::kTradeUpdate,
    MsgId::kTradeComplete,
    MsgId::kPartyInviteReq,
    MsgId::kPartyInviteRsp,
    MsgId::kPartyJoinReq,
    MsgId::kPartyJoinRsp,
    MsgId::kPartyLeaveReq,
    MsgId::kPartyLeaveRsp,
    MsgId::kPartyKickReq,
    MsgId::kPartyKickRsp,
    MsgId::kPartyUpdate,
    MsgId::kGuildUpdateNoticeReq,
    MsgId::kGuildUpdateNoticeRsp,
    MsgId::kGuildUpdateRankReq,
    MsgId::kGuildUpdateRankRsp,
    MsgId::kGuildInfoSync,
    MsgId::kRankingReq,
    MsgId::kRankingRsp,
    MsgId::kRankingMyRankReq,
    MsgId::kRankingMyRankRsp,
    MsgId::kMailSendReq,
    MsgId::kMailSendRsp,
    MsgId::kMailListReq,
    MsgId::kMailListRsp,
    MsgId::kMailReadReq,
    MsgId::kMailReadRsp,
    MsgId::kMailDeleteReq,
    MsgId::kMailDeleteRsp,
    MsgId::kMailClaimReq,
    MsgId::kMailClaimRsp,
    MsgId::kMailNotify,
    MsgId::kAchievementListReq,
    MsgId::kAchievementListRsp,
    MsgId::kAchievementClaimReq,
    MsgId::kAchievementClaimRsp,
    MsgId::kAchievementUpdate,
    MsgId::kAuctionListReq,
    MsgId::kAuctionListRsp,
    MsgId::kAuctionSellReq,
    MsgId::kAuctionSellRsp,
    MsgId::kAuctionBuyReq,
    MsgId::kAuctionBuyRsp,
    MsgId::kAuctionCancelReq,
    MsgId::kAuctionCancelRsp,
    MsgId::kAuctionNotify,
};

constexpr size_t kMaxLoginUsernameLength = constants::LOGIN_USERNAME_MAX_LENGTH;
constexpr size_t kMaxLoginPasswordLength = constants::LOGIN_PASSWORD_MAX_LENGTH;
constexpr size_t kMaxLoginVersionLength = 32;
constexpr size_t kMaxCharacterNameLength = 12;
constexpr size_t kMaxChatContentLength = 256;

struct LoginRequest {
    std::string username;
    std::string password;
    std::string version;
};

struct LoginResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t account_id = 0;
    std::string session_token;
};

struct CreateCharacterRequest {
    std::string name;
    mir2::proto::Profession profession = mir2::proto::Profession::NONE;
    mir2::proto::Gender gender = mir2::proto::Gender::MALE;
};

struct CreateCharacterResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t player_id = 0;
};

struct MoveRequest {
    int32_t target_x = 0;
    int32_t target_y = 0;
};

struct MoveResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    int32_t x = 0;
    int32_t y = 0;
};

struct AttackRequest {
    uint64_t target_id = 0;
    mir2::proto::EntityType target_type = mir2::proto::EntityType::NONE;
};

struct AttackResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t attacker_id = 0;
    uint64_t target_id = 0;
    int32_t damage = 0;
    int32_t target_hp = 0;
    bool target_dead = false;
};

struct SkillRequest {
    uint32_t skill_id = 0;
    uint64_t target_id = 0;
};

struct SkillResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t caster_id = 0;
    uint64_t target_id = 0;
    int32_t damage = 0;
    int32_t healing = 0;
    bool target_dead = false;
    uint32_t skill_id = 0;
    mir2::proto::SkillResult result = mir2::proto::SkillResult::HIT;
    uint32_t cooldown_ms = 0;
};

struct UseItemRequest {
    uint16_t slot = 0;
    uint32_t item_id = 0;
};

struct UseItemResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint16_t slot = 0;
    uint32_t item_id = 0;
    uint32_t remaining = 0;
};

struct PickupItemRequest {
    uint32_t item_id = 0;
};

struct PickupItemResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint32_t item_id = 0;
};

struct DropItemRequest {
    uint16_t slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct DropItemResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct EquipRequest {
    uint16_t slot = 0;
    uint32_t item_id = 0;
};

struct EquipResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint16_t slot = 0;
    uint32_t item_id = 0;
};

struct UnequipRequest {
    uint16_t slot = 0;
};

struct UnequipResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint16_t slot = 0;
    uint32_t item_id = 0;
};

struct ChatRequest {
    mir2::proto::ChatChannel channel = mir2::proto::ChatChannel::WORLD;
    std::string content;
    uint64_t target_id = 0;
};

struct ChatResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
};

struct GuildCreateRequest {
    std::string guild_name;
};

struct GuildCreateResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t guild_id = 0;
};

struct GuildJoinRequest {
    uint32_t guild_id = 0;
};

struct GuildJoinResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildLeaveRequest {};

struct GuildLeaveResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildKickRequest {
    uint32_t target_character_id = 0;
};

struct GuildKickResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildDeclareWarRequest {
    uint32_t target_guild_id = 0;
};

struct GuildDeclareWarResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildCancelWarRequest {
    uint32_t target_guild_id = 0;
};

struct GuildCancelWarResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildMakeAllyRequest {
    uint32_t target_guild_id = 0;
};

struct GuildMakeAllyResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildBreakAllyRequest {
    uint32_t target_guild_id = 0;
};

struct GuildBreakAllyResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct TradeItemInfo {
    uint16_t inventory_slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct TradeRequest {
    uint32_t target_character_id = 0;
};

struct TradeResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t trade_id = 0;
};

struct TradeAddItemRequest {
    uint64_t trade_id = 0;
    uint16_t inventory_slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct TradeAddItemResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct TradeSetGoldRequest {
    uint64_t trade_id = 0;
    uint32_t gold = 0;
};

struct TradeSetGoldResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct TradeConfirmRequest {
    uint64_t trade_id = 0;
};

struct TradeConfirmResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct TradeCancelRequest {
    uint64_t trade_id = 0;
};

struct TradeCancelResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct TradeUpdateMessage {
    uint64_t trade_id = 0;
    uint32_t left_character_id = 0;
    uint32_t right_character_id = 0;
    std::vector<TradeItemInfo> left_items;
    std::vector<TradeItemInfo> right_items;
    uint32_t left_gold = 0;
    uint32_t right_gold = 0;
    bool left_confirmed = false;
    bool right_confirmed = false;
};

struct TradeCompleteMessage {
    uint64_t trade_id = 0;
    bool success = false;
    int32_t error_code = 0;
};

struct PartyInviteRequest {
    uint32_t target_character_id = 0;
};

struct PartyInviteResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct PartyJoinRequest {
    uint64_t party_id = 0;
};

struct PartyJoinResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct PartyLeaveRequest {};

struct PartyLeaveResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct PartyKickRequest {
    uint32_t target_character_id = 0;
};

struct PartyKickResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct PartyMemberInfo {
    uint32_t character_id = 0;
    std::string name;
    uint32_t hp = 0;
    uint32_t max_hp = 0;
    uint32_t map_id = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    bool online = false;
};

struct PartyUpdateMessage {
    uint64_t party_id = 0;
    uint32_t leader_character_id = 0;
    std::vector<PartyMemberInfo> members;
};

struct GuildUpdateNoticeRequest {
    std::vector<std::string> notice_lines;
};

struct GuildUpdateNoticeResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildRankUpdateMember {
    uint32_t character_id = 0;
    uint8_t rank = 0;
};

struct GuildUpdateRankRequest {
    std::vector<GuildRankUpdateMember> members;
};

struct GuildUpdateRankResponse {
    bool success = false;
    int32_t error_code = 0;
};

struct GuildRankInfoData {
    uint8_t rank = 0;
    std::string rank_name;
    std::vector<std::string> members;
};

struct GuildWarInfoData {
    uint32_t enemy_guild_id = 0;
    uint64_t start_time = 0;
    uint64_t remain_time = 0;
};

struct GuildInfoSyncMessage {
    bool has_guild = false;
    uint64_t guild_id = 0;
    std::string guild_name;
    uint16_t level = 0;
    uint32_t member_count = 0;
    uint64_t leader_id = 0;
    std::string leader_name;
    uint32_t max_members = 0;
    std::vector<std::string> notice_list;
    std::vector<GuildRankInfoData> ranks;
    std::vector<GuildWarInfoData> war_guilds;
    std::vector<uint32_t> ally_guild_ids;
    bool allow_ally = false;
    bool in_team_fight = false;
    int32_t match_point = 0;
    std::vector<std::string> fight_members;
};

struct RankingEntryInfo {
    uint32_t rank = 0;
    uint64_t entity_id = 0;
    std::string name;
    int64_t value = 0;
    std::string extra;
};

struct RankingRequest {
    mir2::proto::RankingType ranking_type = mir2::proto::RankingType::LEVEL;
    uint32_t page = 1;
    uint32_t page_size = 20;
};

struct RankingResponse {
    bool success = false;
    int32_t error_code = 0;
    mir2::proto::RankingType ranking_type = mir2::proto::RankingType::LEVEL;
    uint32_t total_count = 0;
    std::vector<RankingEntryInfo> entries;
};

struct RankingMyRankRequest {
    mir2::proto::RankingType ranking_type = mir2::proto::RankingType::LEVEL;
};

struct RankingMyRankResponse {
    bool success = false;
    int32_t error_code = 0;
    mir2::proto::RankingType ranking_type = mir2::proto::RankingType::LEVEL;
    uint32_t rank = 0;
    int64_t value = 0;
};

struct MailAttachmentItemInfo {
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct MailSummaryInfo {
    uint64_t mail_id = 0;
    uint64_t from_character_id = 0;
    std::string subject;
    bool has_attachment = false;
    bool is_read = false;
    bool claimed = false;
    uint64_t send_time = 0;
    uint64_t expire_time = 0;
    uint32_t gold = 0;
    uint32_t attachment_count = 0;
};

struct MailDetailInfo {
    uint64_t mail_id = 0;
    uint64_t from_character_id = 0;
    std::string subject;
    std::string content;
    bool is_read = false;
    bool claimed = false;
    uint64_t send_time = 0;
    uint64_t expire_time = 0;
    uint32_t gold = 0;
    std::vector<MailAttachmentItemInfo> items;
};

struct MailSendRequest {
    uint64_t target_character_id = 0;
    std::string subject;
    std::string content;
    uint32_t gold = 0;
    std::vector<MailAttachmentItemInfo> items;
    uint64_t expire_time = 0;
};

struct MailSendResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t mail_id = 0;
};

struct MailListRequest {};

struct MailListResponse {
    bool success = false;
    int32_t error_code = 0;
    std::vector<MailSummaryInfo> mails;
};

struct MailReadRequest {
    uint64_t mail_id = 0;
};

struct MailReadResponse {
    bool success = false;
    int32_t error_code = 0;
    bool has_mail = false;
    MailDetailInfo mail;
};

struct MailDeleteRequest {
    uint64_t mail_id = 0;
};

struct MailDeleteResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t mail_id = 0;
};

struct MailClaimRequest {
    uint64_t mail_id = 0;
};

struct MailClaimResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t mail_id = 0;
};

struct MailNotifyMessage {
    bool has_mail = false;
    MailSummaryInfo mail;
    uint32_t unread_count = 0;
};

struct AchievementProgressInfo {
    uint32_t achievement_id = 0;
    uint32_t progress = 0;
    uint32_t target = 0;
    bool completed = false;
    bool claimed = false;
    uint64_t completed_time = 0;
    uint32_t reward_gold = 0;
};

struct AchievementListRequest {};

struct AchievementListResponse {
    bool success = false;
    int32_t error_code = 0;
    std::vector<AchievementProgressInfo> achievements;
};

struct AchievementClaimRequest {
    uint32_t achievement_id = 0;
};

struct AchievementClaimResponse {
    bool success = false;
    int32_t error_code = 0;
    uint32_t achievement_id = 0;
    uint32_t reward_gold = 0;
};

struct AchievementUpdateMessage {
    bool has_achievement = false;
    AchievementProgressInfo achievement;
};

struct AuctionListingInfo {
    uint64_t listing_id = 0;
    uint32_t seller_character_id = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
    uint32_t unit_price = 0;
    uint32_t total_price = 0;
    uint64_t created_at_ms = 0;
    uint64_t expires_at_ms = 0;
    bool sold = false;
    bool cancelled = false;
};

struct AuctionListRequest {
    uint32_t page = 1;
    uint32_t page_size = 20;
    bool seller_only = false;
};

struct AuctionListResponse {
    bool success = false;
    int32_t error_code = 0;
    uint32_t total_count = 0;
    std::vector<AuctionListingInfo> listings;
};

struct AuctionSellRequest {
    uint16_t inventory_slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
    uint32_t unit_price = 0;
    uint32_t duration_sec = 86400;
};

struct AuctionSellResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t listing_id = 0;
};

struct AuctionBuyRequest {
    uint64_t listing_id = 0;
};

struct AuctionBuyResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t listing_id = 0;
};

struct AuctionCancelRequest {
    uint64_t listing_id = 0;
};

struct AuctionCancelResponse {
    bool success = false;
    int32_t error_code = 0;
    uint64_t listing_id = 0;
};

struct AuctionNotifyMessage {
    mir2::proto::AuctionNotifyType notify_type =
        mir2::proto::AuctionNotifyType::LISTED;
    bool has_listing = false;
    AuctionListingInfo listing;
};

MessageCodecStatus ValidateLoginRequest(const LoginRequest& request);
MessageCodecStatus ValidateLoginResponse(const LoginResponse& response);
MessageCodecStatus ValidateCreateCharacterRequest(const CreateCharacterRequest& request);
MessageCodecStatus ValidateCreateCharacterResponse(const CreateCharacterResponse& response);
MessageCodecStatus ValidateMoveRequest(const MoveRequest& request);
MessageCodecStatus ValidateMoveResponse(const MoveResponse& response);
MessageCodecStatus ValidateAttackRequest(const AttackRequest& request);
MessageCodecStatus ValidateAttackResponse(const AttackResponse& response);
MessageCodecStatus ValidateSkillRequest(const SkillRequest& request);
MessageCodecStatus ValidateSkillResponse(const SkillResponse& response);
MessageCodecStatus ValidateUseItemRequest(const UseItemRequest& request);
MessageCodecStatus ValidateUseItemResponse(const UseItemResponse& response);
MessageCodecStatus ValidatePickupItemRequest(const PickupItemRequest& request);
MessageCodecStatus ValidatePickupItemResponse(const PickupItemResponse& response);
MessageCodecStatus ValidateDropItemRequest(const DropItemRequest& request);
MessageCodecStatus ValidateDropItemResponse(const DropItemResponse& response);
MessageCodecStatus ValidateEquipRequest(const EquipRequest& request);
MessageCodecStatus ValidateEquipResponse(const EquipResponse& response);
MessageCodecStatus ValidateUnequipRequest(const UnequipRequest& request);
MessageCodecStatus ValidateUnequipResponse(const UnequipResponse& response);
MessageCodecStatus ValidateChatRequest(const ChatRequest& request);
MessageCodecStatus ValidateChatResponse(const ChatResponse& response);
MessageCodecStatus ValidateGuildCreateRequest(const GuildCreateRequest& request);
MessageCodecStatus ValidateGuildCreateResponse(const GuildCreateResponse& response);
MessageCodecStatus ValidateGuildJoinRequest(const GuildJoinRequest& request);
MessageCodecStatus ValidateGuildJoinResponse(const GuildJoinResponse& response);
MessageCodecStatus ValidateGuildLeaveRequest(const GuildLeaveRequest& request);
MessageCodecStatus ValidateGuildLeaveResponse(const GuildLeaveResponse& response);
MessageCodecStatus ValidateGuildKickRequest(const GuildKickRequest& request);
MessageCodecStatus ValidateGuildKickResponse(const GuildKickResponse& response);
MessageCodecStatus ValidateGuildDeclareWarRequest(const GuildDeclareWarRequest& request);
MessageCodecStatus ValidateGuildDeclareWarResponse(const GuildDeclareWarResponse& response);
MessageCodecStatus ValidateGuildCancelWarRequest(const GuildCancelWarRequest& request);
MessageCodecStatus ValidateGuildCancelWarResponse(const GuildCancelWarResponse& response);
MessageCodecStatus ValidateGuildMakeAllyRequest(const GuildMakeAllyRequest& request);
MessageCodecStatus ValidateGuildMakeAllyResponse(const GuildMakeAllyResponse& response);
MessageCodecStatus ValidateGuildBreakAllyRequest(const GuildBreakAllyRequest& request);
MessageCodecStatus ValidateGuildBreakAllyResponse(const GuildBreakAllyResponse& response);
MessageCodecStatus ValidateTradeRequest(const TradeRequest& request);
MessageCodecStatus ValidateTradeResponse(const TradeResponse& response);
MessageCodecStatus ValidateTradeAddItemRequest(const TradeAddItemRequest& request);
MessageCodecStatus ValidateTradeAddItemResponse(const TradeAddItemResponse& response);
MessageCodecStatus ValidateTradeSetGoldRequest(const TradeSetGoldRequest& request);
MessageCodecStatus ValidateTradeSetGoldResponse(const TradeSetGoldResponse& response);
MessageCodecStatus ValidateTradeConfirmRequest(const TradeConfirmRequest& request);
MessageCodecStatus ValidateTradeConfirmResponse(const TradeConfirmResponse& response);
MessageCodecStatus ValidateTradeCancelRequest(const TradeCancelRequest& request);
MessageCodecStatus ValidateTradeCancelResponse(const TradeCancelResponse& response);
MessageCodecStatus ValidateTradeUpdateMessage(const TradeUpdateMessage& message);
MessageCodecStatus ValidateTradeCompleteMessage(const TradeCompleteMessage& message);
MessageCodecStatus ValidatePartyInviteRequest(const PartyInviteRequest& request);
MessageCodecStatus ValidatePartyInviteResponse(const PartyInviteResponse& response);
MessageCodecStatus ValidatePartyJoinRequest(const PartyJoinRequest& request);
MessageCodecStatus ValidatePartyJoinResponse(const PartyJoinResponse& response);
MessageCodecStatus ValidatePartyLeaveRequest(const PartyLeaveRequest& request);
MessageCodecStatus ValidatePartyLeaveResponse(const PartyLeaveResponse& response);
MessageCodecStatus ValidatePartyKickRequest(const PartyKickRequest& request);
MessageCodecStatus ValidatePartyKickResponse(const PartyKickResponse& response);
MessageCodecStatus ValidatePartyUpdateMessage(const PartyUpdateMessage& message);
MessageCodecStatus ValidateGuildUpdateNoticeRequest(const GuildUpdateNoticeRequest& request);
MessageCodecStatus ValidateGuildUpdateNoticeResponse(const GuildUpdateNoticeResponse& response);
MessageCodecStatus ValidateGuildUpdateRankRequest(const GuildUpdateRankRequest& request);
MessageCodecStatus ValidateGuildUpdateRankResponse(const GuildUpdateRankResponse& response);
MessageCodecStatus ValidateGuildInfoSyncMessage(const GuildInfoSyncMessage& message);
MessageCodecStatus ValidateRankingRequest(const RankingRequest& request);
MessageCodecStatus ValidateRankingResponse(const RankingResponse& response);
MessageCodecStatus ValidateRankingMyRankRequest(const RankingMyRankRequest& request);
MessageCodecStatus ValidateRankingMyRankResponse(const RankingMyRankResponse& response);
MessageCodecStatus ValidateMailSendRequest(const MailSendRequest& request);
MessageCodecStatus ValidateMailSendResponse(const MailSendResponse& response);
MessageCodecStatus ValidateMailListRequest(const MailListRequest& request);
MessageCodecStatus ValidateMailListResponse(const MailListResponse& response);
MessageCodecStatus ValidateMailReadRequest(const MailReadRequest& request);
MessageCodecStatus ValidateMailReadResponse(const MailReadResponse& response);
MessageCodecStatus ValidateMailDeleteRequest(const MailDeleteRequest& request);
MessageCodecStatus ValidateMailDeleteResponse(const MailDeleteResponse& response);
MessageCodecStatus ValidateMailClaimRequest(const MailClaimRequest& request);
MessageCodecStatus ValidateMailClaimResponse(const MailClaimResponse& response);
MessageCodecStatus ValidateMailNotifyMessage(const MailNotifyMessage& message);
MessageCodecStatus ValidateAchievementListRequest(const AchievementListRequest& request);
MessageCodecStatus ValidateAchievementListResponse(const AchievementListResponse& response);
MessageCodecStatus ValidateAchievementClaimRequest(const AchievementClaimRequest& request);
MessageCodecStatus ValidateAchievementClaimResponse(const AchievementClaimResponse& response);
MessageCodecStatus ValidateAchievementUpdateMessage(const AchievementUpdateMessage& message);
MessageCodecStatus ValidateAuctionListRequest(const AuctionListRequest& request);
MessageCodecStatus ValidateAuctionListResponse(const AuctionListResponse& response);
MessageCodecStatus ValidateAuctionSellRequest(const AuctionSellRequest& request);
MessageCodecStatus ValidateAuctionSellResponse(const AuctionSellResponse& response);
MessageCodecStatus ValidateAuctionBuyRequest(const AuctionBuyRequest& request);
MessageCodecStatus ValidateAuctionBuyResponse(const AuctionBuyResponse& response);
MessageCodecStatus ValidateAuctionCancelRequest(const AuctionCancelRequest& request);
MessageCodecStatus ValidateAuctionCancelResponse(const AuctionCancelResponse& response);
MessageCodecStatus ValidateAuctionNotifyMessage(const AuctionNotifyMessage& message);

std::vector<uint8_t> EncodeLoginRequest(const LoginRequest& request,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeLoginResponse(const LoginResponse& response,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeCreateCharacterRequest(const CreateCharacterRequest& request,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeCreateCharacterResponse(const CreateCharacterResponse& response,
                                                   MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMoveRequest(const MoveRequest& request,
                                       MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMoveResponse(const MoveResponse& response,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAttackRequest(const AttackRequest& request,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAttackResponse(const AttackResponse& response,
                                          MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeSkillRequest(const SkillRequest& request,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeSkillResponse(const SkillResponse& response,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeUseItemRequest(const UseItemRequest& request,
                                          MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeUseItemResponse(const UseItemResponse& response,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePickupItemRequest(const PickupItemRequest& request,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePickupItemResponse(const PickupItemResponse& response,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeDropItemRequest(const DropItemRequest& request,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeDropItemResponse(const DropItemResponse& response,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeEquipRequest(const EquipRequest& request,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeEquipResponse(const EquipResponse& response,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeUnequipRequest(const UnequipRequest& request,
                                          MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeUnequipResponse(const UnequipResponse& response,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeChatRequest(const ChatRequest& request,
                                       MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeChatResponse(const ChatResponse& response,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildCreateRequest(const GuildCreateRequest& request,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildCreateResponse(const GuildCreateResponse& response,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildJoinRequest(const GuildJoinRequest& request,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildJoinResponse(const GuildJoinResponse& response,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildLeaveRequest(const GuildLeaveRequest& request,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildLeaveResponse(const GuildLeaveResponse& response,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildKickRequest(const GuildKickRequest& request,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildKickResponse(const GuildKickResponse& response,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildDeclareWarRequest(const GuildDeclareWarRequest& request,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildDeclareWarResponse(const GuildDeclareWarResponse& response,
                                                   MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildCancelWarRequest(const GuildCancelWarRequest& request,
                                                 MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildCancelWarResponse(const GuildCancelWarResponse& response,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildMakeAllyRequest(const GuildMakeAllyRequest& request,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildMakeAllyResponse(const GuildMakeAllyResponse& response,
                                                 MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildBreakAllyRequest(const GuildBreakAllyRequest& request,
                                                 MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildBreakAllyResponse(const GuildBreakAllyResponse& response,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeRequest(const TradeRequest& request,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeResponse(const TradeResponse& response,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeAddItemRequest(const TradeAddItemRequest& request,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeAddItemResponse(const TradeAddItemResponse& response,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeSetGoldRequest(const TradeSetGoldRequest& request,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeSetGoldResponse(const TradeSetGoldResponse& response,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeConfirmRequest(const TradeConfirmRequest& request,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeConfirmResponse(const TradeConfirmResponse& response,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeCancelRequest(const TradeCancelRequest& request,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeCancelResponse(const TradeCancelResponse& response,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeUpdateMessage(const TradeUpdateMessage& message,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeTradeCompleteMessage(const TradeCompleteMessage& message,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyInviteRequest(const PartyInviteRequest& request,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyInviteResponse(const PartyInviteResponse& response,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyJoinRequest(const PartyJoinRequest& request,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyJoinResponse(const PartyJoinResponse& response,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyLeaveRequest(const PartyLeaveRequest& request,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyLeaveResponse(const PartyLeaveResponse& response,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyKickRequest(const PartyKickRequest& request,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyKickResponse(const PartyKickResponse& response,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodePartyUpdateMessage(const PartyUpdateMessage& message,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildUpdateNoticeRequest(const GuildUpdateNoticeRequest& request,
                                                    MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildUpdateNoticeResponse(const GuildUpdateNoticeResponse& response,
                                                     MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildUpdateRankRequest(const GuildUpdateRankRequest& request,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildUpdateRankResponse(const GuildUpdateRankResponse& response,
                                                   MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeGuildInfoSyncMessage(const GuildInfoSyncMessage& message,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeRankingRequest(const RankingRequest& request,
                                          MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeRankingResponse(const RankingResponse& response,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeRankingMyRankRequest(const RankingMyRankRequest& request,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeRankingMyRankResponse(const RankingMyRankResponse& response,
                                                 MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailSendRequest(const MailSendRequest& request,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailSendResponse(const MailSendResponse& response,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailListRequest(const MailListRequest& request,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailListResponse(const MailListResponse& response,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailReadRequest(const MailReadRequest& request,
                                           MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailReadResponse(const MailReadResponse& response,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailDeleteRequest(const MailDeleteRequest& request,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailDeleteResponse(const MailDeleteResponse& response,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailClaimRequest(const MailClaimRequest& request,
                                            MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailClaimResponse(const MailClaimResponse& response,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMailNotifyMessage(const MailNotifyMessage& message,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAchievementListRequest(const AchievementListRequest& request,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAchievementListResponse(const AchievementListResponse& response,
                                                   MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAchievementClaimRequest(const AchievementClaimRequest& request,
                                                   MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAchievementClaimResponse(const AchievementClaimResponse& response,
                                                    MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAchievementUpdateMessage(const AchievementUpdateMessage& message,
                                                    MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionListRequest(const AuctionListRequest& request,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionListResponse(const AuctionListResponse& response,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionSellRequest(const AuctionSellRequest& request,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionSellResponse(const AuctionSellResponse& response,
                                               MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionBuyRequest(const AuctionBuyRequest& request,
                                             MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionBuyResponse(const AuctionBuyResponse& response,
                                              MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionCancelRequest(const AuctionCancelRequest& request,
                                                MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionCancelResponse(const AuctionCancelResponse& response,
                                                 MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAuctionNotifyMessage(const AuctionNotifyMessage& message,
                                                MessageCodecStatus* out_status = nullptr);

MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      LoginRequest* out_request);
MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      LoginRequest* out_request);
MessageCodecStatus DecodeLoginResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       LoginResponse* out_response);
MessageCodecStatus DecodeLoginResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       LoginResponse* out_response);

MessageCodecStatus DecodeCreateCharacterRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                CreateCharacterRequest* out_request);
MessageCodecStatus DecodeCreateCharacterRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                CreateCharacterRequest* out_request);
MessageCodecStatus DecodeCreateCharacterResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 CreateCharacterResponse* out_response);
MessageCodecStatus DecodeCreateCharacterResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 CreateCharacterResponse* out_response);

MessageCodecStatus DecodeMoveRequest(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     MoveRequest* out_request);
MessageCodecStatus DecodeMoveRequest(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     MoveRequest* out_request);
MessageCodecStatus DecodeMoveResponse(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      MoveResponse* out_response);
MessageCodecStatus DecodeMoveResponse(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      MoveResponse* out_response);

MessageCodecStatus DecodeAttackRequest(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       AttackRequest* out_request);
MessageCodecStatus DecodeAttackRequest(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       AttackRequest* out_request);
MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        AttackResponse* out_response);
MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        AttackResponse* out_response);
MessageCodecStatus DecodeSkillRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      SkillRequest* out_request);
MessageCodecStatus DecodeSkillRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      SkillRequest* out_request);
MessageCodecStatus DecodeSkillResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       SkillResponse* out_response);
MessageCodecStatus DecodeSkillResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       SkillResponse* out_response);
MessageCodecStatus DecodeUseItemRequest(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        UseItemRequest* out_request);
MessageCodecStatus DecodeUseItemRequest(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        UseItemRequest* out_request);
MessageCodecStatus DecodeUseItemResponse(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         UseItemResponse* out_response);
MessageCodecStatus DecodeUseItemResponse(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         UseItemResponse* out_response);
MessageCodecStatus DecodePickupItemRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PickupItemRequest* out_request);
MessageCodecStatus DecodePickupItemRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PickupItemRequest* out_request);
MessageCodecStatus DecodePickupItemResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PickupItemResponse* out_response);
MessageCodecStatus DecodePickupItemResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PickupItemResponse* out_response);
MessageCodecStatus DecodeDropItemRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         DropItemRequest* out_request);
MessageCodecStatus DecodeDropItemRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         DropItemRequest* out_request);
MessageCodecStatus DecodeDropItemResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          DropItemResponse* out_response);
MessageCodecStatus DecodeDropItemResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          DropItemResponse* out_response);
MessageCodecStatus DecodeEquipRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      EquipRequest* out_request);
MessageCodecStatus DecodeEquipRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      EquipRequest* out_request);
MessageCodecStatus DecodeEquipResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       EquipResponse* out_response);
MessageCodecStatus DecodeEquipResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       EquipResponse* out_response);
MessageCodecStatus DecodeUnequipRequest(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        UnequipRequest* out_request);
MessageCodecStatus DecodeUnequipRequest(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        UnequipRequest* out_request);
MessageCodecStatus DecodeUnequipResponse(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         UnequipResponse* out_response);
MessageCodecStatus DecodeUnequipResponse(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         UnequipResponse* out_response);
MessageCodecStatus DecodeChatRequest(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     ChatRequest* out_request);
MessageCodecStatus DecodeChatRequest(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     ChatRequest* out_request);
MessageCodecStatus DecodeChatResponse(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      ChatResponse* out_response);
MessageCodecStatus DecodeChatResponse(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      ChatResponse* out_response);
MessageCodecStatus DecodeGuildCreateRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            GuildCreateRequest* out_request);
MessageCodecStatus DecodeGuildCreateRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            GuildCreateRequest* out_request);
MessageCodecStatus DecodeGuildCreateResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             GuildCreateResponse* out_response);
MessageCodecStatus DecodeGuildCreateResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             GuildCreateResponse* out_response);
MessageCodecStatus DecodeGuildJoinRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          GuildJoinRequest* out_request);
MessageCodecStatus DecodeGuildJoinRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          GuildJoinRequest* out_request);
MessageCodecStatus DecodeGuildJoinResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           GuildJoinResponse* out_response);
MessageCodecStatus DecodeGuildJoinResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           GuildJoinResponse* out_response);
MessageCodecStatus DecodeGuildLeaveRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           GuildLeaveRequest* out_request);
MessageCodecStatus DecodeGuildLeaveRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           GuildLeaveRequest* out_request);
MessageCodecStatus DecodeGuildLeaveResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            GuildLeaveResponse* out_response);
MessageCodecStatus DecodeGuildLeaveResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            GuildLeaveResponse* out_response);
MessageCodecStatus DecodeGuildKickRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          GuildKickRequest* out_request);
MessageCodecStatus DecodeGuildKickRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          GuildKickRequest* out_request);
MessageCodecStatus DecodeGuildKickResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           GuildKickResponse* out_response);
MessageCodecStatus DecodeGuildKickResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           GuildKickResponse* out_response);
MessageCodecStatus DecodeGuildDeclareWarRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildDeclareWarRequest* out_request);
MessageCodecStatus DecodeGuildDeclareWarRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildDeclareWarRequest* out_request);
MessageCodecStatus DecodeGuildDeclareWarResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 GuildDeclareWarResponse* out_response);
MessageCodecStatus DecodeGuildDeclareWarResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 GuildDeclareWarResponse* out_response);
MessageCodecStatus DecodeGuildCancelWarRequest(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               GuildCancelWarRequest* out_request);
MessageCodecStatus DecodeGuildCancelWarRequest(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               GuildCancelWarRequest* out_request);
MessageCodecStatus DecodeGuildCancelWarResponse(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildCancelWarResponse* out_response);
MessageCodecStatus DecodeGuildCancelWarResponse(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildCancelWarResponse* out_response);
MessageCodecStatus DecodeGuildMakeAllyRequest(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              GuildMakeAllyRequest* out_request);
MessageCodecStatus DecodeGuildMakeAllyRequest(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              GuildMakeAllyRequest* out_request);
MessageCodecStatus DecodeGuildMakeAllyResponse(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               GuildMakeAllyResponse* out_response);
MessageCodecStatus DecodeGuildMakeAllyResponse(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               GuildMakeAllyResponse* out_response);
MessageCodecStatus DecodeGuildBreakAllyRequest(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               GuildBreakAllyRequest* out_request);
MessageCodecStatus DecodeGuildBreakAllyRequest(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               GuildBreakAllyRequest* out_request);
MessageCodecStatus DecodeGuildBreakAllyResponse(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildBreakAllyResponse* out_response);
MessageCodecStatus DecodeGuildBreakAllyResponse(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildBreakAllyResponse* out_response);
MessageCodecStatus DecodeTradeRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      TradeRequest* out_request);
MessageCodecStatus DecodeTradeRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      TradeRequest* out_request);
MessageCodecStatus DecodeTradeResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       TradeResponse* out_response);
MessageCodecStatus DecodeTradeResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       TradeResponse* out_response);
MessageCodecStatus DecodeTradeAddItemRequest(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeAddItemRequest* out_request);
MessageCodecStatus DecodeTradeAddItemRequest(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeAddItemRequest* out_request);
MessageCodecStatus DecodeTradeAddItemResponse(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeAddItemResponse* out_response);
MessageCodecStatus DecodeTradeAddItemResponse(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeAddItemResponse* out_response);
MessageCodecStatus DecodeTradeSetGoldRequest(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeSetGoldRequest* out_request);
MessageCodecStatus DecodeTradeSetGoldRequest(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeSetGoldRequest* out_request);
MessageCodecStatus DecodeTradeSetGoldResponse(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeSetGoldResponse* out_response);
MessageCodecStatus DecodeTradeSetGoldResponse(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeSetGoldResponse* out_response);
MessageCodecStatus DecodeTradeConfirmRequest(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeConfirmRequest* out_request);
MessageCodecStatus DecodeTradeConfirmRequest(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeConfirmRequest* out_request);
MessageCodecStatus DecodeTradeConfirmResponse(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeConfirmResponse* out_response);
MessageCodecStatus DecodeTradeConfirmResponse(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeConfirmResponse* out_response);
MessageCodecStatus DecodeTradeCancelRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            TradeCancelRequest* out_request);
MessageCodecStatus DecodeTradeCancelRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            TradeCancelRequest* out_request);
MessageCodecStatus DecodeTradeCancelResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeCancelResponse* out_response);
MessageCodecStatus DecodeTradeCancelResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeCancelResponse* out_response);
MessageCodecStatus DecodeTradeUpdateMessage(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            TradeUpdateMessage* out_message);
MessageCodecStatus DecodeTradeUpdateMessage(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            TradeUpdateMessage* out_message);
MessageCodecStatus DecodeTradeCompleteMessage(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeCompleteMessage* out_message);
MessageCodecStatus DecodeTradeCompleteMessage(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeCompleteMessage* out_message);
MessageCodecStatus DecodePartyInviteRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PartyInviteRequest* out_request);
MessageCodecStatus DecodePartyInviteRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PartyInviteRequest* out_request);
MessageCodecStatus DecodePartyInviteResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             PartyInviteResponse* out_response);
MessageCodecStatus DecodePartyInviteResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             PartyInviteResponse* out_response);
MessageCodecStatus DecodePartyJoinRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          PartyJoinRequest* out_request);
MessageCodecStatus DecodePartyJoinRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          PartyJoinRequest* out_request);
MessageCodecStatus DecodePartyJoinResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PartyJoinResponse* out_response);
MessageCodecStatus DecodePartyJoinResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PartyJoinResponse* out_response);
MessageCodecStatus DecodePartyLeaveRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PartyLeaveRequest* out_request);
MessageCodecStatus DecodePartyLeaveRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PartyLeaveRequest* out_request);
MessageCodecStatus DecodePartyLeaveResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PartyLeaveResponse* out_response);
MessageCodecStatus DecodePartyLeaveResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PartyLeaveResponse* out_response);
MessageCodecStatus DecodePartyKickRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          PartyKickRequest* out_request);
MessageCodecStatus DecodePartyKickRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          PartyKickRequest* out_request);
MessageCodecStatus DecodePartyKickResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PartyKickResponse* out_response);
MessageCodecStatus DecodePartyKickResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PartyKickResponse* out_response);
MessageCodecStatus DecodePartyUpdateMessage(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PartyUpdateMessage* out_message);
MessageCodecStatus DecodePartyUpdateMessage(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PartyUpdateMessage* out_message);
MessageCodecStatus DecodeGuildUpdateNoticeRequest(uint16_t msg_id,
                                                  const uint8_t* data,
                                                  size_t size,
                                                  GuildUpdateNoticeRequest* out_request);
MessageCodecStatus DecodeGuildUpdateNoticeRequest(uint16_t msg_id,
                                                  const std::vector<uint8_t>& payload,
                                                  GuildUpdateNoticeRequest* out_request);
MessageCodecStatus DecodeGuildUpdateNoticeResponse(uint16_t msg_id,
                                                   const uint8_t* data,
                                                   size_t size,
                                                   GuildUpdateNoticeResponse* out_response);
MessageCodecStatus DecodeGuildUpdateNoticeResponse(uint16_t msg_id,
                                                   const std::vector<uint8_t>& payload,
                                                   GuildUpdateNoticeResponse* out_response);
MessageCodecStatus DecodeGuildUpdateRankRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildUpdateRankRequest* out_request);
MessageCodecStatus DecodeGuildUpdateRankRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildUpdateRankRequest* out_request);
MessageCodecStatus DecodeGuildUpdateRankResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 GuildUpdateRankResponse* out_response);
MessageCodecStatus DecodeGuildUpdateRankResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 GuildUpdateRankResponse* out_response);
MessageCodecStatus DecodeGuildInfoSyncMessage(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              GuildInfoSyncMessage* out_message);
MessageCodecStatus DecodeGuildInfoSyncMessage(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              GuildInfoSyncMessage* out_message);
MessageCodecStatus DecodeRankingRequest(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        RankingRequest* out_request);
MessageCodecStatus DecodeRankingRequest(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        RankingRequest* out_request);
MessageCodecStatus DecodeRankingResponse(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         RankingResponse* out_response);
MessageCodecStatus DecodeRankingResponse(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         RankingResponse* out_response);
MessageCodecStatus DecodeRankingMyRankRequest(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              RankingMyRankRequest* out_request);
MessageCodecStatus DecodeRankingMyRankRequest(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              RankingMyRankRequest* out_request);
MessageCodecStatus DecodeRankingMyRankResponse(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               RankingMyRankResponse* out_response);
MessageCodecStatus DecodeRankingMyRankResponse(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               RankingMyRankResponse* out_response);
MessageCodecStatus DecodeMailSendRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         MailSendRequest* out_request);
MessageCodecStatus DecodeMailSendRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         MailSendRequest* out_request);
MessageCodecStatus DecodeMailSendResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailSendResponse* out_response);
MessageCodecStatus DecodeMailSendResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailSendResponse* out_response);
MessageCodecStatus DecodeMailListRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         MailListRequest* out_request);
MessageCodecStatus DecodeMailListRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         MailListRequest* out_request);
MessageCodecStatus DecodeMailListResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailListResponse* out_response);
MessageCodecStatus DecodeMailListResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailListResponse* out_response);
MessageCodecStatus DecodeMailReadRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         MailReadRequest* out_request);
MessageCodecStatus DecodeMailReadRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         MailReadRequest* out_request);
MessageCodecStatus DecodeMailReadResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailReadResponse* out_response);
MessageCodecStatus DecodeMailReadResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailReadResponse* out_response);
MessageCodecStatus DecodeMailDeleteRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           MailDeleteRequest* out_request);
MessageCodecStatus DecodeMailDeleteRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           MailDeleteRequest* out_request);
MessageCodecStatus DecodeMailDeleteResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            MailDeleteResponse* out_response);
MessageCodecStatus DecodeMailDeleteResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            MailDeleteResponse* out_response);
MessageCodecStatus DecodeMailClaimRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailClaimRequest* out_request);
MessageCodecStatus DecodeMailClaimRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailClaimRequest* out_request);
MessageCodecStatus DecodeMailClaimResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           MailClaimResponse* out_response);
MessageCodecStatus DecodeMailClaimResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           MailClaimResponse* out_response);
MessageCodecStatus DecodeMailNotifyMessage(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           MailNotifyMessage* out_message);
MessageCodecStatus DecodeMailNotifyMessage(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           MailNotifyMessage* out_message);
MessageCodecStatus DecodeAchievementListRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                AchievementListRequest* out_request);
MessageCodecStatus DecodeAchievementListRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                AchievementListRequest* out_request);
MessageCodecStatus DecodeAchievementListResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 AchievementListResponse* out_response);
MessageCodecStatus DecodeAchievementListResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 AchievementListResponse* out_response);
MessageCodecStatus DecodeAchievementClaimRequest(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 AchievementClaimRequest* out_request);
MessageCodecStatus DecodeAchievementClaimRequest(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 AchievementClaimRequest* out_request);
MessageCodecStatus DecodeAchievementClaimResponse(uint16_t msg_id,
                                                  const uint8_t* data,
                                                  size_t size,
                                                  AchievementClaimResponse* out_response);
MessageCodecStatus DecodeAchievementClaimResponse(uint16_t msg_id,
                                                  const std::vector<uint8_t>& payload,
                                                  AchievementClaimResponse* out_response);
MessageCodecStatus DecodeAchievementUpdateMessage(uint16_t msg_id,
                                                  const uint8_t* data,
                                                  size_t size,
                                                  AchievementUpdateMessage* out_message);
MessageCodecStatus DecodeAchievementUpdateMessage(uint16_t msg_id,
                                                  const std::vector<uint8_t>& payload,
                                                  AchievementUpdateMessage* out_message);
MessageCodecStatus DecodeAuctionListRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            AuctionListRequest* out_request);
MessageCodecStatus DecodeAuctionListRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            AuctionListRequest* out_request);
MessageCodecStatus DecodeAuctionListResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             AuctionListResponse* out_response);
MessageCodecStatus DecodeAuctionListResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             AuctionListResponse* out_response);
MessageCodecStatus DecodeAuctionSellRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            AuctionSellRequest* out_request);
MessageCodecStatus DecodeAuctionSellRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            AuctionSellRequest* out_request);
MessageCodecStatus DecodeAuctionSellResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             AuctionSellResponse* out_response);
MessageCodecStatus DecodeAuctionSellResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             AuctionSellResponse* out_response);
MessageCodecStatus DecodeAuctionBuyRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           AuctionBuyRequest* out_request);
MessageCodecStatus DecodeAuctionBuyRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           AuctionBuyRequest* out_request);
MessageCodecStatus DecodeAuctionBuyResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            AuctionBuyResponse* out_response);
MessageCodecStatus DecodeAuctionBuyResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            AuctionBuyResponse* out_response);
MessageCodecStatus DecodeAuctionCancelRequest(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              AuctionCancelRequest* out_request);
MessageCodecStatus DecodeAuctionCancelRequest(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              AuctionCancelRequest* out_request);
MessageCodecStatus DecodeAuctionCancelResponse(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               AuctionCancelResponse* out_response);
MessageCodecStatus DecodeAuctionCancelResponse(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               AuctionCancelResponse* out_response);
MessageCodecStatus DecodeAuctionNotifyMessage(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              AuctionNotifyMessage* out_message);
MessageCodecStatus DecodeAuctionNotifyMessage(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              AuctionNotifyMessage* out_message);

}  // namespace mir2::common

#endif  // LEGEND2_COMMON_PROTOCOL_MESSAGE_CODEC_H
