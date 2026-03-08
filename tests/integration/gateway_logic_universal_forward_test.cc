#include <gtest/gtest.h>

#include <asio.hpp>
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#define private public
#include "gateway/gateway_server.h"
#include "logic/logic_server.h"
#undef private

#include "client/network/dual_channel_client.h"
#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/universal_forward_msg_ids.h"
#include "common/network/i_channel.h"
#include "chat_generated.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "guild_generated.h"
#include "item_generated.h"
#include "login_generated.h"
#include "mail_generated.h"
#include "party_generated.h"
#include "ranking_generated.h"
#include "trade_generated.h"
#include "achievement_generated.h"
#include "auction_generated.h"
#include "integration/test_helpers.h"
#include "network/tcp_connection.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

namespace {

using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::InternalMsgId;
using mir2::common::MsgId;
using mir2::gateway::GatewayServer;
using mir2::logic::LogicServer;
using mir2::network::TcpSession;
using mir2::test::integration::WaitForCondition;

constexpr const char* kHost = "127.0.0.1";
constexpr uint16_t kRoutedMsgId =
    static_cast<uint16_t>(InternalMsgId::kRoutedMessage);
constexpr uint16_t kTradeReqMsgId = static_cast<uint16_t>(MsgId::kTradeReq);
constexpr uint16_t kTradeRspMsgId = static_cast<uint16_t>(MsgId::kTradeRsp);
constexpr uint16_t kTradeUpdateMsgId = static_cast<uint16_t>(MsgId::kTradeUpdate);
constexpr uint16_t kTradeCompleteMsgId = static_cast<uint16_t>(MsgId::kTradeComplete);
constexpr uint16_t kGuildCreateReqMsgId =
    static_cast<uint16_t>(MsgId::kGuildCreateReq);
constexpr uint16_t kGuildCreateRspMsgId =
    static_cast<uint16_t>(MsgId::kGuildCreateRsp);
constexpr uint16_t kGuildInfoSyncMsgId =
    static_cast<uint16_t>(MsgId::kGuildInfoSync);
constexpr uint16_t kPartyInviteReqMsgId =
    static_cast<uint16_t>(MsgId::kPartyInviteReq);
constexpr uint16_t kPartyInviteRspMsgId =
    static_cast<uint16_t>(MsgId::kPartyInviteRsp);
constexpr uint16_t kPartyUpdateMsgId = static_cast<uint16_t>(MsgId::kPartyUpdate);
constexpr uint64_t kE2ETradeId = 99001;
constexpr uint32_t kE2ETradeLeftCharacterId = 3001;
constexpr uint32_t kE2ETradeRightCharacterId = 3002;
constexpr uint32_t kE2ETradeLeftGold = 111;
constexpr uint32_t kE2ETradeRightGold = 222;
constexpr uint16_t kE2ETradeLeftSlot = 7;
constexpr uint32_t kE2ETradeLeftItemId = 1001;
constexpr uint32_t kE2ETradeLeftItemCount = 2;
constexpr uint64_t kE2EPartyId = 55001;
constexpr uint32_t kE2EPartyLeaderId = 4001;
constexpr uint32_t kE2EPartyMemberId = 4002;
constexpr uint64_t kE2EGuildId = 77001;
constexpr const char* kE2EGuildName = "GuildE2E";
constexpr const char* kE2EGuildLeaderName = "GuildLeader";

struct TestPorts {
  uint16_t gateway_tcp = 0;
  uint16_t gateway_udp = 0;
  uint16_t logic_tcp = 0;
};

uint16_t AllocateTcpPort() {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(
      io_context,
      asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
  return acceptor.local_endpoint().port();
}

uint16_t AllocateUdpPort() {
  asio::io_context io_context;
  asio::ip::udp::socket socket(
      io_context,
      asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
  return socket.local_endpoint().port();
}

TestPorts AllocateTestPorts() {
  TestPorts ports;
  do {
    ports.gateway_tcp = AllocateTcpPort();
    ports.logic_tcp = AllocateTcpPort();
  } while (ports.gateway_tcp == ports.logic_tcp);

  do {
    ports.gateway_udp = AllocateUdpPort();
  } while (ports.gateway_udp == ports.gateway_tcp || ports.gateway_udp == ports.logic_tcp);

  return ports;
}

std::filesystem::path CreateTempDir(const std::string& prefix) {
  const auto timestamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() /
             (prefix + "_" + std::to_string(timestamp));
  std::filesystem::create_directories(dir);
  return dir;
}

bool IsPermissionDeniedError(const std::error_code& code) {
  return code == std::errc::permission_denied ||
         code == std::errc::operation_not_permitted;
}

bool IsPermissionDeniedMessage(std::string_view message) {
  return message.find("Operation not permitted") != std::string_view::npos ||
         message.find("operation not permitted") != std::string_view::npos ||
         message.find("Permission denied") != std::string_view::npos ||
         message.find("permission denied") != std::string_view::npos;
}

std::string WriteTempConfig(const std::filesystem::path& dir,
                            const std::string& name,
                            const std::string& contents) {
  const auto path = dir / name;
  std::ofstream output(path, std::ios::binary);
  output << contents;
  output.close();
  return path.string();
}

std::string EnvOrDefault(const char* key, const char* default_value) {
  const char* value = std::getenv(key);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return default_value;
}

std::string BuildGatewayConfig(const TestPorts& ports,
                               const std::filesystem::path& log_dir,
                               const std::string& transport = "tcp",
                               const std::string& uds_path = "") {
  std::ostringstream out;
  out << "server:\n"
      << "  id: 110\n"
      << "  name: \"Test-Gateway\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.gateway_tcp << "\n"
      << "  udp_port: " << ports.gateway_udp << "\n"
      << "  metrics_port: 0\n"
      << "  io_threads: 1\n"
      << "  max_connections: 6000\n"
      << "  tick_interval_ms: 20\n"
      << "  heartbeat_timeout_ms: 0\n"
      << "log:\n"
      << "  level: \"info\"\n"
      << "  path: \"" << log_dir.string() << "\"\n"
      << "services:\n"
      << "  logic:\n"
      << "    host: \"" << kHost << "\"\n"
      << "    port: " << ports.logic_tcp << "\n"
      << "    transport: \"" << transport << "\"\n";
  if (!uds_path.empty()) {
    out << "    uds_path: \"" << uds_path << "\"\n";
  }
  return out.str();
}

std::string BuildLogicConfig(const TestPorts& ports,
                             const std::filesystem::path& log_dir,
                             const std::string& transport = "tcp",
                             const std::string& uds_path = "") {
  const std::string postgres_host = EnvOrDefault("POSTGRES_HOST", kHost);
  const std::string postgres_port = EnvOrDefault("POSTGRES_PORT", "5432");
  const std::string postgres_user = EnvOrDefault("POSTGRES_USER", "mir2");
  const std::string postgres_password = EnvOrDefault("POSTGRES_PASSWORD", "mir2_password");
  const std::string postgres_db = EnvOrDefault("POSTGRES_DB", "mir2_game");

  const std::string db_host = EnvOrDefault("MIR2_DB_HOST", postgres_host.c_str());
  const std::string db_port = EnvOrDefault("MIR2_DB_PORT", postgres_port.c_str());
  const std::string db_user = EnvOrDefault("MIR2_DB_USER", postgres_user.c_str());
  const std::string db_password =
      EnvOrDefault("MIR2_DB_PASSWORD", postgres_password.c_str());
  const std::string db_name = EnvOrDefault("MIR2_DB_NAME", postgres_db.c_str());

  std::ostringstream out;
  out << "server:\n"
      << "  id: 210\n"
      << "  name: \"Test-Logic\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.logic_tcp << "\n"
      << "  metrics_port: 0\n"
      << "  io_threads: 1\n"
      << "  max_connections: 6000\n"
      << "  tick_interval_ms: 20\n"
      << "log:\n"
      << "  level: \"info\"\n"
      << "  path: \"" << log_dir.string() << "\"\n"
      << "services:\n"
      << "  logic:\n"
      << "    host: \"" << kHost << "\"\n"
      << "    port: " << ports.logic_tcp << "\n"
      << "    transport: \"" << transport << "\"\n";
  if (!uds_path.empty()) {
    out << "    uds_path: \"" << uds_path << "\"\n";
  }
  out << "database:\n"
      << "  host: \"" << db_host << "\"\n"
      << "  port: " << db_port << "\n"
      << "  user: \"" << db_user << "\"\n"
      << "  password: \"" << db_password << "\"\n"
      << "  database: \"" << db_name << "\"\n";
  return out.str();
}

std::vector<uint8_t> BuildPayloadForMsgId(uint16_t msg_id) {
  flatbuffers::FlatBufferBuilder builder;

  switch (static_cast<MsgId>(msg_id)) {
    case MsgId::kLoginReq: {
      const auto username = builder.CreateString("integration_user");
      const auto password = builder.CreateString("integration_pw");
      const auto version = builder.CreateString(std::to_string(
          static_cast<uint32_t>(mir2::proto::SchemaVersion::kSchemaVersion)));
      builder.Finish(mir2::proto::CreateLoginReq(builder, username, password, version));
      break;
    }
    case MsgId::kLogout: {
      const auto token = builder.CreateString("session_token");
      builder.Finish(mir2::proto::CreateLogoutReq(builder, 1, token));
      break;
    }
    case MsgId::kCreateRoleReq: {
      const auto name = builder.CreateString("RoleA");
      builder.Finish(mir2::proto::CreateCreateRoleReq(
          builder, name, mir2::proto::Profession::WARRIOR, mir2::proto::Gender::MALE));
      break;
    }
    case MsgId::kSelectRoleReq:
      builder.Finish(mir2::proto::CreateSelectRoleReq(builder, 1));
      break;
    case MsgId::kRoleListReq: {
      const auto token = builder.CreateString("session_token");
      builder.Finish(mir2::proto::CreateRoleListReq(builder, 1, token));
      break;
    }
    case MsgId::kMoveReq:
      builder.Finish(mir2::proto::CreateMoveReq(builder, 10, 20));
      break;
    case MsgId::kAttackReq:
      builder.Finish(
          mir2::proto::CreateAttackReq(builder, 100, mir2::proto::EntityType::MONSTER));
      break;
    case MsgId::kSkillReq:
      builder.Finish(mir2::proto::CreateSkillReq(builder, 1, 100));
      break;
    case MsgId::kChatReq: {
      const auto content = builder.CreateString("hello");
      builder.Finish(
          mir2::proto::CreateChatReq(builder, mir2::proto::ChatChannel::WORLD, content, 0));
      break;
    }
    case MsgId::kUseItemReq:
      builder.Finish(mir2::proto::CreateUseItemReq(builder, 1, 1001));
      break;
    case MsgId::kDropItemReq:
      builder.Finish(mir2::proto::CreateDropItemReq(builder, 1, 1001, 1));
      break;
    case MsgId::kPickupItemReq:
      builder.Finish(mir2::proto::CreatePickupItemReq(builder, 1001));
      break;
    case MsgId::kEquipReq:
      builder.Finish(mir2::proto::CreateEquipReq(builder, 1, 1001));
      break;
    case MsgId::kUnequipReq:
      builder.Finish(mir2::proto::CreateUnequipReq(builder, 1));
      break;
    case MsgId::kNpcInteractReq:
    case MsgId::kNpcMenuSelect:
      return std::vector<uint8_t>{'{', '}'};
    case MsgId::kGuildChat: {
      const auto from_name = builder.CreateString("tester");
      const auto content = builder.CreateString("guild msg");
      builder.Finish(mir2::proto::CreateChatMessage(
          builder,
          mir2::proto::ChatChannel::GUILD,
          1,
          from_name,
          0,
          content,
          0xFFFFFFu,
          1));
      break;
    }
    case MsgId::kGuildCreateReq: {
      const auto guild_name = builder.CreateString("GuildA");
      builder.Finish(mir2::proto::CreateCreateGuildRequest(builder, guild_name));
      break;
    }
    case MsgId::kGuildJoinReq:
      builder.Finish(mir2::proto::CreateJoinGuildRequest(builder, 1));
      break;
    case MsgId::kGuildLeaveReq:
      builder.Finish(mir2::proto::CreateLeaveGuildRequest(builder));
      break;
    case MsgId::kGuildKickReq:
      builder.Finish(mir2::proto::CreateKickGuildRequest(builder, 2));
      break;
    case MsgId::kGuildDeclareWarReq:
      builder.Finish(mir2::proto::CreateDeclareWarRequest(builder, 1));
      break;
    case MsgId::kGuildCancelWarReq:
      builder.Finish(mir2::proto::CreateCancelWarRequest(builder, 1));
      break;
    case MsgId::kGuildMakeAllyReq:
      builder.Finish(mir2::proto::CreateMakeAllianceRequest(builder, 1));
      break;
    case MsgId::kGuildBreakAllyReq:
      builder.Finish(mir2::proto::CreateBreakAllianceRequest(builder, 1));
      break;
    case MsgId::kGuildUpdateNoticeReq: {
      std::vector<flatbuffers::Offset<flatbuffers::String>> notice_lines;
      notice_lines.emplace_back(builder.CreateString("notice"));
      const auto notice_vec = builder.CreateVector(notice_lines);
      builder.Finish(mir2::proto::CreateUpdateNoticeRequest(builder, notice_vec));
      break;
    }
    case MsgId::kGuildUpdateRankReq: {
      std::vector<flatbuffers::Offset<mir2::proto::RankUpdateMember>> members;
      members.emplace_back(mir2::proto::CreateRankUpdateMember(builder, 2, 99));
      const auto members_vec = builder.CreateVector(members);
      builder.Finish(mir2::proto::CreateUpdateRankRequest(builder, members_vec));
      break;
    }
    case MsgId::kTradeReq:
      builder.Finish(mir2::proto::CreateTradeReq(builder, 2));
      break;
    case MsgId::kTradeAddItemReq:
      builder.Finish(mir2::proto::CreateTradeAddItemReq(builder, 1, 1, 1001, 1));
      break;
    case MsgId::kTradeSetGoldReq:
      builder.Finish(mir2::proto::CreateTradeSetGoldReq(builder, 1, 100));
      break;
    case MsgId::kTradeConfirmReq:
      builder.Finish(mir2::proto::CreateTradeConfirmReq(builder, 1));
      break;
    case MsgId::kTradeCancelReq:
      builder.Finish(mir2::proto::CreateTradeCancelReq(builder, 1));
      break;
    case MsgId::kPartyInviteReq:
      builder.Finish(mir2::proto::CreatePartyInviteReq(builder, 2));
      break;
    case MsgId::kPartyJoinReq:
      builder.Finish(mir2::proto::CreatePartyJoinReq(builder, 1));
      break;
    case MsgId::kPartyLeaveReq:
      builder.Finish(mir2::proto::CreatePartyLeaveReq(builder));
      break;
    case MsgId::kPartyKickReq:
      builder.Finish(mir2::proto::CreatePartyKickReq(builder, 2));
      break;
    case MsgId::kRankingReq:
      builder.Finish(mir2::proto::CreateRankingReq(
          builder, mir2::proto::RankingType::LEVEL, 1, 20));
      break;
    case MsgId::kRankingMyRankReq:
      builder.Finish(mir2::proto::CreateRankingMyRankReq(
          builder, mir2::proto::RankingType::LEVEL));
      break;
    case MsgId::kMailSendReq: {
      const auto subject = builder.CreateString("mail");
      const auto content = builder.CreateString("hello");
      builder.Finish(mir2::proto::CreateMailSendReq(
          builder, 2, subject, content, 0, 0, 0));
      break;
    }
    case MsgId::kMailListReq:
      builder.Finish(mir2::proto::CreateMailListReq(builder));
      break;
    case MsgId::kMailReadReq:
      builder.Finish(mir2::proto::CreateMailReadReq(builder, 1));
      break;
    case MsgId::kMailDeleteReq:
      builder.Finish(mir2::proto::CreateMailDeleteReq(builder, 1));
      break;
    case MsgId::kMailClaimReq:
      builder.Finish(mir2::proto::CreateMailClaimReq(builder, 1));
      break;
    case MsgId::kAchievementListReq:
      builder.Finish(mir2::proto::CreateAchievementListReq(builder));
      break;
    case MsgId::kAchievementClaimReq:
      builder.Finish(mir2::proto::CreateAchievementClaimReq(builder, 1));
      break;
    case MsgId::kAuctionListReq:
      builder.Finish(mir2::proto::CreateAuctionListReq(builder, 1, 20, false));
      break;
    case MsgId::kAuctionSellReq:
      builder.Finish(
          mir2::proto::CreateAuctionSellReq(builder, 1, 1001, 1, 999, 3600));
      break;
    case MsgId::kAuctionBuyReq:
      builder.Finish(mir2::proto::CreateAuctionBuyReq(builder, 1));
      break;
    case MsgId::kAuctionCancelReq:
      builder.Finish(mir2::proto::CreateAuctionCancelReq(builder, 1));
      break;
    default:
      break;
  }

  // Legacy GuildMessageType (1..10) compatibility payloads.
  if (msg_id <= 100) {
    switch (static_cast<mir2::proto::GuildMessageType>(msg_id)) {
      case mir2::proto::GuildMessageType::CREATE: {
        const auto guild_name = builder.CreateString("GuildA");
        builder.Finish(mir2::proto::CreateCreateGuildRequest(builder, guild_name));
        break;
      }
      case mir2::proto::GuildMessageType::JOIN:
        builder.Finish(mir2::proto::CreateJoinGuildRequest(builder, 1));
        break;
      case mir2::proto::GuildMessageType::LEAVE:
        builder.Finish(mir2::proto::CreateLeaveGuildRequest(builder));
        break;
      case mir2::proto::GuildMessageType::KICK:
        builder.Finish(mir2::proto::CreateKickGuildRequest(builder, 2));
        break;
      case mir2::proto::GuildMessageType::DECLARE_WAR:
        builder.Finish(mir2::proto::CreateDeclareWarRequest(builder, 1));
        break;
      case mir2::proto::GuildMessageType::CANCEL_WAR:
        builder.Finish(mir2::proto::CreateCancelWarRequest(builder, 1));
        break;
      case mir2::proto::GuildMessageType::MAKE_ALLY:
        builder.Finish(mir2::proto::CreateMakeAllianceRequest(builder, 1));
        break;
      case mir2::proto::GuildMessageType::BREAK_ALLY:
        builder.Finish(mir2::proto::CreateBreakAllianceRequest(builder, 1));
        break;
      case mir2::proto::GuildMessageType::UPDATE_NOTICE: {
        std::vector<flatbuffers::Offset<flatbuffers::String>> notice_lines;
        notice_lines.emplace_back(builder.CreateString("notice"));
        const auto notice_vec = builder.CreateVector(notice_lines);
        builder.Finish(mir2::proto::CreateUpdateNoticeRequest(builder, notice_vec));
        break;
      }
      case mir2::proto::GuildMessageType::UPDATE_RANK: {
        std::vector<flatbuffers::Offset<mir2::proto::RankUpdateMember>> members;
        members.emplace_back(mir2::proto::CreateRankUpdateMember(builder, 2, 99));
        const auto members_vec = builder.CreateVector(members);
        builder.Finish(mir2::proto::CreateUpdateRankRequest(builder, members_vec));
        break;
      }
      default:
        break;
    }
  }

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTradeRspPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateTradeRsp(
      builder,
      /*success=*/true,
      static_cast<int>(mir2::proto::ErrorCode::ERR_OK),
      kE2ETradeId);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTradeUpdatePayload() {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> left_items;
  left_items.emplace_back(mir2::proto::CreateTradeItemInfo(builder,
                                                           kE2ETradeLeftSlot,
                                                           kE2ETradeLeftItemId,
                                                           kE2ETradeLeftItemCount));
  const auto left_items_vec = builder.CreateVector(left_items);
  const auto update = mir2::proto::CreateTradeUpdate(
      builder,
      kE2ETradeId,
      kE2ETradeLeftCharacterId,
      kE2ETradeRightCharacterId,
      left_items_vec,
      0,
      kE2ETradeLeftGold,
      kE2ETradeRightGold,
      true,
      false);
  builder.Finish(update);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTradeCompletePayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto complete = mir2::proto::CreateTradeComplete(
      builder,
      kE2ETradeId,
      /*success=*/true,
      static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  builder.Finish(complete);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPartyInviteRspPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreatePartyInviteRsp(
      builder,
      /*success=*/true,
      static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPartyUpdatePayload() {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::PartyMemberInfo>> members;
  const auto leader_name = builder.CreateString("leader");
  members.emplace_back(mir2::proto::CreatePartyMemberInfo(
      builder, kE2EPartyLeaderId, leader_name, 120, 150, 1, 10, 20, true));
  const auto member_name = builder.CreateString("member");
  members.emplace_back(mir2::proto::CreatePartyMemberInfo(
      builder, kE2EPartyMemberId, member_name, 110, 140, 1, 11, 21, true));
  const auto members_vec = builder.CreateVector(members);
  const auto update = mir2::proto::CreatePartyUpdate(
      builder, kE2EPartyId, kE2EPartyLeaderId, members_vec);
  builder.Finish(update);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildGuildCreateRspPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto guild_name = builder.CreateString(kE2EGuildName);
  const auto leader_name = builder.CreateString(kE2EGuildLeaderName);
  const auto guild_info = mir2::proto::CreateGuildInfo(
      builder,
      kE2EGuildId,
      guild_name,
      /*level=*/1,
      /*member_count=*/1,
      /*leader_id=*/kE2ETradeLeftCharacterId,
      leader_name,
      /*max_members=*/100);
  const auto rsp = mir2::proto::CreateCreateGuildResponse(
      builder,
      /*success=*/true,
      static_cast<int>(mir2::proto::ErrorCode::ERR_OK),
      guild_info);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildGuildInfoSyncPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto guild_name = builder.CreateString(kE2EGuildName);
  const auto leader_name = builder.CreateString(kE2EGuildLeaderName);
  std::vector<flatbuffers::Offset<flatbuffers::String>> notices;
  notices.emplace_back(builder.CreateString("E2E Notice"));
  const auto notice_vec = builder.CreateVector(notices);
  const auto guild_info = mir2::proto::CreateGuildInfo(
      builder,
      kE2EGuildId,
      guild_name,
      /*level=*/1,
      /*member_count=*/1,
      /*leader_id=*/kE2ETradeLeftCharacterId,
      leader_name,
      /*max_members=*/100,
      notice_vec);
  const auto sync = mir2::proto::CreateGuildInfoSync(builder, guild_info);
  builder.Finish(sync);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

struct RoutedCapture {
  void Push(uint16_t msg_id, uint64_t client_id) {
    std::lock_guard<std::mutex> lock(mutex);
    msg_ids.push_back(msg_id);
    client_ids.push_back(client_id);
    cv.notify_all();
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return msg_ids.size();
  }

  bool Contains(uint16_t msg_id) const {
    std::lock_guard<std::mutex> lock(mutex);
    return std::find(msg_ids.begin(), msg_ids.end(), msg_id) != msg_ids.end();
  }

  template <typename Container>
  bool ContainsAll(const Container& expected) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_set<uint16_t> seen(msg_ids.begin(), msg_ids.end());
    for (uint16_t msg_id : expected) {
      if (seen.count(msg_id) == 0) {
        return false;
      }
    }
    return true;
  }

  template <typename Container>
  std::vector<uint16_t> MissingFrom(const Container& expected) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_set<uint16_t> seen(msg_ids.begin(), msg_ids.end());
    std::vector<uint16_t> missing;
    for (uint16_t msg_id : expected) {
      if (seen.count(msg_id) == 0) {
        missing.push_back(msg_id);
      }
    }
    return missing;
  }

  std::vector<uint16_t> DrainMsgIds() {
    std::lock_guard<std::mutex> lock(mutex);
    return msg_ids;
  }

  std::vector<uint64_t> DrainClientIds() {
    std::lock_guard<std::mutex> lock(mutex);
    return client_ids;
  }

  mutable std::mutex mutex;
  std::condition_variable cv;
  std::vector<uint16_t> msg_ids;
  std::vector<uint64_t> client_ids;
};

std::string FormatMsgIds(const std::vector<uint16_t>& msg_ids) {
  if (msg_ids.empty()) {
    return "[]";
  }

  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < msg_ids.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << msg_ids[i];
  }
  out << "]";
  return out.str();
}

std::shared_ptr<TcpSession> CreateMockSession(asio::io_context& io_context,
                                              uint64_t connection_id) {
  auto mock_socket = std::make_unique<mir2::network::MockSocket>(io_context.get_executor());
  auto connection = std::make_shared<mir2::network::TcpConnection>(
      std::move(mock_socket), connection_id);
  return std::make_shared<TcpSession>(connection);
}

class GatewayLogicUniversalForwardTest : public ::testing::Test {
 protected:
  struct RoutedHandlerState {
    std::mutex mutex;
    std::function<void(const std::shared_ptr<TcpSession>&,
                       const mir2::common::RoutedMessageData&)>
        handler;
  };

  virtual std::string IpcTransport() const { return "tcp"; }
  virtual std::string IpcUdsPath() const { return ""; }

  void SetUp() override {
    try {
      ports_ = AllocateTestPorts();
      temp_dir_ = CreateTempDir("mir2_gateway_logic_universal");
      const std::string transport = IpcTransport();
      const std::string uds_path = IpcUdsPath();
      gateway_config_path_ = WriteTempConfig(
          temp_dir_,
          "gateway.yaml",
          BuildGatewayConfig(ports_, temp_dir_ / "gateway_logs", transport, uds_path));
      logic_config_path_ = WriteTempConfig(
          temp_dir_,
          "logic.yaml",
          BuildLogicConfig(ports_, temp_dir_ / "logic_logs", transport, uds_path));
      WriteTempConfig(
          temp_dir_,
          "combat_config.yaml",
          "combat:\n"
          "  min_variance_percent: 85\n"
          "  max_variance_percent: 115\n"
          "  minimum_damage: 1\n"
          "  base_critical_chance: 0.05\n"
          "  critical_multiplier: 1.5\n"
          "  base_miss_chance: 0.02\n"
          "  default_melee_range: 1\n"
          "  respawn:\n"
          "    hp_percent: 30\n"
          "    mp_percent: 30\n"
          "    map_id: 1\n"
          "    position:\n"
          "      x: 0\n"
          "      y: 0\n");

      gateway_ = std::make_unique<GatewayServer>();
      ASSERT_TRUE(gateway_->Initialize(gateway_config_path_));

      logic_ = std::make_unique<LogicServer>();
      ASSERT_TRUE(logic_->Initialize(logic_config_path_));
      routed_handler_state_ = std::make_shared<RoutedHandlerState>();
      InstallLogicHandler();

      logic_thread_ = std::thread([this]() { logic_->Run(); });
      gateway_->Run();

      client_ = std::make_unique<DualChannelClient>();
    } catch (const std::system_error& ex) {
      if (IsPermissionDeniedError(ex.code()) ||
          IsPermissionDeniedMessage(ex.what())) {
        GTEST_SKIP() << "Skipping GatewayLogicUniversalForwardTest due to "
                     << "socket permission restrictions in current environment: "
                     << ex.what();
      }
      throw;
    } catch (const std::exception& ex) {
      if (IsPermissionDeniedMessage(ex.what())) {
        GTEST_SKIP() << "Skipping GatewayLogicUniversalForwardTest due to "
                     << "socket permission restrictions in current environment: "
                     << ex.what();
      }
      throw;
    }
  }

  void TearDown() override {
    if (routed_handler_state_) {
      std::lock_guard<std::mutex> lock(routed_handler_state_->mutex);
      routed_handler_state_->handler = {};
    }
    routed_handler_state_.reset();

    if (client_) {
      client_->disconnect();
      client_.reset();
    }
    if (gateway_) {
      gateway_->Shutdown();
    }
    if (logic_) {
      logic_->Shutdown();
    }
    if (logic_thread_.joinable()) {
      logic_thread_.join();
    }
    gateway_.reset();
    logic_.reset();
    pending_packets_.clear();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  void InstallLogicHandler() {
    ASSERT_NE(logic_, nullptr);
    ASSERT_NE(logic_->network_, nullptr);
    ASSERT_NE(routed_handler_state_, nullptr);

    logic_->network_->RegisterHandler(
        static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
        [state_weak = std::weak_ptr<RoutedHandlerState>(routed_handler_state_)](
            const std::shared_ptr<TcpSession>& session,
            const std::vector<uint8_t>& payload) {
          mir2::common::RoutedMessageData routed;
          if (!mir2::common::ParseRoutedMessage(payload, &routed)) {
            return;
          }

          auto state = state_weak.lock();
          if (!state) {
            return;
          }

          std::function<void(const std::shared_ptr<TcpSession>&,
                             const mir2::common::RoutedMessageData&)>
              handler_copy;
          {
            std::lock_guard<std::mutex> lock(state->mutex);
            handler_copy = state->handler;
          }
          if (handler_copy) {
            handler_copy(session, routed);
          }
        });
  }

  void SetRoutedHandler(std::function<void(const mir2::common::RoutedMessageData&)> handler) {
    SetRoutedHandlerWithSession(
        [handler = std::move(handler)](const std::shared_ptr<TcpSession>&,
                                       const mir2::common::RoutedMessageData& routed) {
          if (handler) {
            handler(routed);
          }
        });
  }

  void SetRoutedHandlerWithSession(
      std::function<void(const std::shared_ptr<TcpSession>&,
                         const mir2::common::RoutedMessageData&)> handler) {
    ASSERT_NE(routed_handler_state_, nullptr);
    std::lock_guard<std::mutex> lock(routed_handler_state_->mutex);
    routed_handler_state_->handler = std::move(handler);
  }

  bool ConnectClient() {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (client_->connect(kHost, ports_.gateway_tcp)) {
        break;
      }
      std::this_thread::sleep_for(50ms);
    }

    return WaitForCondition(
        [&]() { return client_->is_connected(); },
        2s,
        10ms,
        [this]() { client_->update(); });
  }

  bool WaitForLogicConnected(std::chrono::milliseconds timeout) {
    return WaitForCondition(
        [this]() { return gateway_->IsLogicConnected(); },
        timeout,
        10ms,
        [this]() { client_->update(); });
  }

  bool WaitForGatewayForwarding(std::chrono::milliseconds timeout) {
    return WaitForCondition(
        [this]() {
          return gateway_->holder_state_ ==
                 mir2::gateway::ConnectionHolder::State::FORWARDING;
        },
        timeout,
        10ms,
        [this]() { client_->update(); });
  }

  void PumpClientFor(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpClient();
      std::this_thread::sleep_for(2ms);
    }
  }

  void PumpClient() {
    if (!client_) {
      return;
    }
    client_->update();
    while (true) {
      auto packet = client_->receive();
      if (!packet.has_value()) {
        break;
      }
      pending_packets_.push_back(std::move(*packet));
    }
  }

  std::optional<mir2::common::NetworkPacket> WaitForClientPacket(
      uint16_t msg_id, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpClient();
      auto it = std::find_if(
          pending_packets_.begin(),
          pending_packets_.end(),
          [msg_id](const mir2::common::NetworkPacket& packet) {
            return packet.msg_id == msg_id;
          });
      if (it != pending_packets_.end()) {
        auto packet = std::move(*it);
        pending_packets_.erase(it);
        return packet;
      }
      std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
  }

  TestPorts ports_{};
  std::filesystem::path temp_dir_;
  std::string gateway_config_path_;
  std::string logic_config_path_;
  std::unique_ptr<GatewayServer> gateway_;
  std::unique_ptr<LogicServer> logic_;
  std::thread logic_thread_;
  std::unique_ptr<DualChannelClient> client_;
  std::deque<mir2::common::NetworkPacket> pending_packets_;

  std::shared_ptr<RoutedHandlerState> routed_handler_state_;
};

class GatewayLogicUniversalForwardUdsTest : public GatewayLogicUniversalForwardTest {
 protected:
  std::string IpcTransport() const override { return "auto"; }
  std::string IpcUdsPath() const override {
    return (temp_dir_ / "gateway_logic_universal.sock").string();
  }
};

// 验证扩展消息矩阵都会通过Gateway转发到LogicServer。
TEST_F(GatewayLogicUniversalForwardTest, AllMessageTypesForwarded) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  // 等待 KcpReset/握手余波处理完成，减少首批消息丢失抖动。
  PumpClientFor(200ms);
  // This case validates universal-forward matrix coverage, not KCP upgrade timing.
  // Force MoveReq over TCP to keep transport deterministic.
  client_->set_route(static_cast<uint16_t>(MsgId::kMoveReq),
                     mir2::common::ChannelType::kTcp);

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto capture_locked = capture_weak.lock()) {
      capture_locked->Push(routed.msg_id, routed.client_id);
    }
  });

  const auto& msg_ids = mir2::common::protocol::kUniversalForwardMsgIds;

  auto send_messages = [&msg_ids, capture, this](bool only_missing) {
    for (auto msg_id : msg_ids) {
      if (only_missing && capture->Contains(msg_id)) {
        continue;
      }
      auto payload = BuildPayloadForMsgId(msg_id);
      ASSERT_FALSE(payload.empty());

      // Guardrail: ensure the test payload itself is protocol-valid before send.
      const auto encoded = mir2::common::EncodePacketV2(
          msg_id, payload.data(), payload.size(), /*sequence=*/0, /*flags=*/0);
      ASSERT_FALSE(encoded.empty()) << "Failed to encode msg_id=" << msg_id;
      mir2::common::NetworkPacket decoded{};
      const auto decode_status =
          mir2::common::DecodePacketV2(encoded.data(), encoded.size(), &decoded);
      ASSERT_EQ(decode_status, mir2::common::DecodeStatus::kOk)
          << "Invalid payload for msg_id=" << msg_id;

      client_->send(msg_id, payload);
      client_->update();
      // Keep below per-session ingress limiter (50 msg/s) to avoid false negatives.
      std::this_thread::sleep_for(25ms);
    }
  };

  auto wait_for_all = [&msg_ids, capture, this]() {
    return WaitForCondition(
        [capture, &msg_ids]() { return capture->ContainsAll(msg_ids); },
        6s,
        5ms,
        [this]() { client_->update(); });
  };

  send_messages(/*only_missing=*/false);
  if (!wait_for_all()) {
    // 仅补发未观测到的消息类型，提升偶发网络抖动下稳定性。
    send_messages(/*only_missing=*/true);
  }

  const bool all_forwarded = wait_for_all();
  const auto missing_msg_ids = capture->MissingFrom(msg_ids);
  ASSERT_TRUE(all_forwarded)
      << "Missing forwarded msg_ids: " << FormatMsgIds(missing_msg_ids);

  auto forwarded_ids = capture->DrainMsgIds();
  EXPECT_GE(forwarded_ids.size(), msg_ids.size());

  std::unordered_set<uint16_t> forwarded_set(forwarded_ids.begin(), forwarded_ids.end());
  for (auto msg_id : msg_ids) {
    EXPECT_TRUE(forwarded_set.count(msg_id) > 0);
  }

  auto client_ids = capture->DrainClientIds();
  ASSERT_FALSE(client_ids.empty());
  const uint64_t first_client = client_ids.front();
  EXPECT_NE(first_client, 0u);
  for (auto client_id : client_ids) {
    EXPECT_EQ(client_id, first_client);
  }

  // 给异步转发链路一个短暂排空窗口，避免带着在途任务进入 teardown。
  PumpClientFor(100ms);
}

// 验证Gateway不做认证检查，由LogicServer自行判断。
TEST_F(GatewayLogicUniversalForwardTest, NoAuthCheckAtGateway) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  // 等待 KcpReset/握手余波处理完成，减少首批消息抖动。
  PumpClientFor(200ms);

  auto authed = std::make_shared<std::atomic<bool>>(false);
  auto unauth_seen = std::make_shared<std::atomic<int>>(0);
  auto authed_seen = std::make_shared<std::atomic<int>>(0);

  SetRoutedHandler([authed, unauth_seen, authed_seen](
                       const mir2::common::RoutedMessageData& routed) {
    if (routed.msg_id == static_cast<uint16_t>(MsgId::kLoginReq)) {
      authed->store(true, std::memory_order_release);
      return;
    }
    if (!authed->load(std::memory_order_acquire)) {
      unauth_seen->fetch_add(1, std::memory_order_relaxed);
    } else {
      authed_seen->fetch_add(1, std::memory_order_relaxed);
    }
  });

  // 未登录直接发送MoveReq，Logic侧应收到并标记未认证。
  client_->send(static_cast<uint16_t>(MsgId::kMoveReq),
                BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq)));
  ASSERT_TRUE(WaitForCondition(
      [unauth_seen]() { return unauth_seen->load() > 0; }, 3s, [this]() { client_->update(); }));

  // 发送登录请求后，再发送MoveReq应被视为已认证。
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq),
                BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq)));
  ASSERT_TRUE(
      WaitForCondition([authed]() { return authed->load(); }, 3s, [this]() { client_->update(); }));

  client_->send(static_cast<uint16_t>(MsgId::kMoveReq),
                BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq)));
  ASSERT_TRUE(WaitForCondition(
      [authed_seen]() { return authed_seen->load() > 0; }, 3s, [this]() { client_->update(); }));

  EXPECT_GE(unauth_seen->load(), 1);
  EXPECT_GE(authed_seen->load(), 1);
}

TEST_F(GatewayLogicUniversalForwardTest, TradeRoundTripForwarding) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  PumpClientFor(200ms);

  auto trade_req_seen = std::make_shared<std::atomic<int>>(0);
  SetRoutedHandlerWithSession(
      [trade_req_seen](const std::shared_ptr<TcpSession>& session,
                       const mir2::common::RoutedMessageData& routed) {
        if (!session || routed.msg_id != kTradeReqMsgId) {
          return;
        }
        trade_req_seen->fetch_add(1, std::memory_order_relaxed);
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kTradeRspMsgId, BuildTradeRspPayload()));
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kTradeUpdateMsgId, BuildTradeUpdatePayload()));
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kTradeCompleteMsgId, BuildTradeCompletePayload()));
      });

  const auto login_payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq));
  ASSERT_FALSE(login_payload.empty());
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq), login_payload);
  PumpClientFor(50ms);

  const auto request_payload = BuildPayloadForMsgId(kTradeReqMsgId);
  ASSERT_FALSE(request_payload.empty());
  client_->send(kTradeReqMsgId, request_payload);
  ASSERT_TRUE(WaitForCondition(
      [trade_req_seen]() { return trade_req_seen->load(std::memory_order_relaxed) > 0; },
      3s,
      [this]() { PumpClient(); }));

  auto trade_rsp = WaitForClientPacket(kTradeRspMsgId, 3s);
  ASSERT_TRUE(trade_rsp.has_value());
  flatbuffers::Verifier rsp_verifier(trade_rsp->payload.data(), trade_rsp->payload.size());
  ASSERT_TRUE(rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* rsp = flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_rsp->payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_TRUE(rsp->success());
  EXPECT_EQ(rsp->trade_id(), kE2ETradeId);

  auto trade_update = WaitForClientPacket(kTradeUpdateMsgId, 3s);
  ASSERT_TRUE(trade_update.has_value());
  flatbuffers::Verifier update_verifier(
      trade_update->payload.data(), trade_update->payload.size());
  ASSERT_TRUE(update_verifier.VerifyBuffer<mir2::proto::TradeUpdate>(nullptr));
  const auto* update =
      flatbuffers::GetRoot<mir2::proto::TradeUpdate>(trade_update->payload.data());
  ASSERT_NE(update, nullptr);
  EXPECT_EQ(update->trade_id(), kE2ETradeId);
  EXPECT_EQ(update->left_character_id(), kE2ETradeLeftCharacterId);
  EXPECT_EQ(update->right_character_id(), kE2ETradeRightCharacterId);
  EXPECT_EQ(update->left_gold(), kE2ETradeLeftGold);
  EXPECT_EQ(update->right_gold(), kE2ETradeRightGold);
  ASSERT_NE(update->left_items(), nullptr);
  ASSERT_EQ(update->left_items()->size(), 1u);
  EXPECT_EQ(update->left_items()->Get(0)->inventory_slot(), kE2ETradeLeftSlot);
  EXPECT_EQ(update->left_items()->Get(0)->item_id(), kE2ETradeLeftItemId);
  EXPECT_EQ(update->left_items()->Get(0)->count(), kE2ETradeLeftItemCount);

  auto trade_complete = WaitForClientPacket(kTradeCompleteMsgId, 3s);
  ASSERT_TRUE(trade_complete.has_value());
  flatbuffers::Verifier complete_verifier(
      trade_complete->payload.data(), trade_complete->payload.size());
  ASSERT_TRUE(complete_verifier.VerifyBuffer<mir2::proto::TradeComplete>(nullptr));
  const auto* complete =
      flatbuffers::GetRoot<mir2::proto::TradeComplete>(trade_complete->payload.data());
  ASSERT_NE(complete, nullptr);
  EXPECT_EQ(complete->trade_id(), kE2ETradeId);
  EXPECT_TRUE(complete->success());
}

TEST_F(GatewayLogicUniversalForwardTest, PartyInviteRoundTripForwarding) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  PumpClientFor(200ms);

  auto party_invite_req_seen = std::make_shared<std::atomic<int>>(0);
  SetRoutedHandlerWithSession(
      [party_invite_req_seen](const std::shared_ptr<TcpSession>& session,
                              const mir2::common::RoutedMessageData& routed) {
        if (!session || routed.msg_id != kPartyInviteReqMsgId) {
          return;
        }
        party_invite_req_seen->fetch_add(1, std::memory_order_relaxed);
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kPartyInviteRspMsgId, BuildPartyInviteRspPayload()));
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kPartyUpdateMsgId, BuildPartyUpdatePayload()));
      });

  const auto login_payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq));
  ASSERT_FALSE(login_payload.empty());
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq), login_payload);
  PumpClientFor(50ms);

  const auto request_payload = BuildPayloadForMsgId(kPartyInviteReqMsgId);
  ASSERT_FALSE(request_payload.empty());
  client_->send(kPartyInviteReqMsgId, request_payload);
  ASSERT_TRUE(WaitForCondition(
      [party_invite_req_seen]() {
        return party_invite_req_seen->load(std::memory_order_relaxed) > 0;
      },
      3s,
      [this]() { PumpClient(); }));

  auto invite_rsp = WaitForClientPacket(kPartyInviteRspMsgId, 3s);
  ASSERT_TRUE(invite_rsp.has_value());
  flatbuffers::Verifier rsp_verifier(invite_rsp->payload.data(), invite_rsp->payload.size());
  ASSERT_TRUE(rsp_verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::PartyInviteRsp>(invite_rsp->payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_TRUE(rsp->success());

  auto party_update = WaitForClientPacket(kPartyUpdateMsgId, 3s);
  ASSERT_TRUE(party_update.has_value());
  flatbuffers::Verifier update_verifier(
      party_update->payload.data(), party_update->payload.size());
  ASSERT_TRUE(update_verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr));
  const auto* update =
      flatbuffers::GetRoot<mir2::proto::PartyUpdate>(party_update->payload.data());
  ASSERT_NE(update, nullptr);
  EXPECT_EQ(update->party_id(), kE2EPartyId);
  EXPECT_EQ(update->leader_character_id(), kE2EPartyLeaderId);
  ASSERT_NE(update->members(), nullptr);
  ASSERT_EQ(update->members()->size(), 2u);
  EXPECT_EQ(update->members()->Get(0)->character_id(), kE2EPartyLeaderId);
  EXPECT_EQ(update->members()->Get(1)->character_id(), kE2EPartyMemberId);
}

TEST_F(GatewayLogicUniversalForwardTest, GuildCreateRoundTripForwarding) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  PumpClientFor(200ms);

  auto guild_create_req_seen = std::make_shared<std::atomic<int>>(0);
  SetRoutedHandlerWithSession(
      [guild_create_req_seen](const std::shared_ptr<TcpSession>& session,
                              const mir2::common::RoutedMessageData& routed) {
        if (!session || routed.msg_id != kGuildCreateReqMsgId) {
          return;
        }
        guild_create_req_seen->fetch_add(1, std::memory_order_relaxed);
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kGuildCreateRspMsgId, BuildGuildCreateRspPayload()));
        session->Send(
            kRoutedMsgId,
            mir2::common::BuildRoutedMessage(
                routed.client_id, kGuildInfoSyncMsgId, BuildGuildInfoSyncPayload()));
      });

  const auto login_payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq));
  ASSERT_FALSE(login_payload.empty());
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq), login_payload);
  PumpClientFor(50ms);

  const auto request_payload = BuildPayloadForMsgId(kGuildCreateReqMsgId);
  ASSERT_FALSE(request_payload.empty());
  client_->send(kGuildCreateReqMsgId, request_payload);
  ASSERT_TRUE(WaitForCondition(
      [guild_create_req_seen]() {
        return guild_create_req_seen->load(std::memory_order_relaxed) > 0;
      },
      3s,
      [this]() { PumpClient(); }));

  auto create_rsp = WaitForClientPacket(kGuildCreateRspMsgId, 3s);
  ASSERT_TRUE(create_rsp.has_value());
  flatbuffers::Verifier create_rsp_verifier(
      create_rsp->payload.data(), create_rsp->payload.size());
  ASSERT_TRUE(create_rsp_verifier.VerifyBuffer<mir2::proto::CreateGuildResponse>(nullptr));
  const auto* create_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::CreateGuildResponse>(create_rsp->payload.data());
  ASSERT_NE(create_rsp_payload, nullptr);
  EXPECT_TRUE(create_rsp_payload->success());
  EXPECT_EQ(create_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  ASSERT_NE(create_rsp_payload->guild_info(), nullptr);
  EXPECT_EQ(create_rsp_payload->guild_info()->id(), kE2EGuildId);
  ASSERT_NE(create_rsp_payload->guild_info()->name(), nullptr);
  EXPECT_EQ(create_rsp_payload->guild_info()->name()->str(), kE2EGuildName);

  auto info_sync = WaitForClientPacket(kGuildInfoSyncMsgId, 3s);
  ASSERT_TRUE(info_sync.has_value());
  flatbuffers::Verifier info_sync_verifier(
      info_sync->payload.data(), info_sync->payload.size());
  ASSERT_TRUE(info_sync_verifier.VerifyBuffer<mir2::proto::GuildInfoSync>(nullptr));
  const auto* info_sync_payload =
      flatbuffers::GetRoot<mir2::proto::GuildInfoSync>(info_sync->payload.data());
  ASSERT_NE(info_sync_payload, nullptr);
  ASSERT_NE(info_sync_payload->guild_info(), nullptr);
  EXPECT_EQ(info_sync_payload->guild_info()->id(), kE2EGuildId);
  ASSERT_NE(info_sync_payload->guild_info()->name(), nullptr);
  EXPECT_EQ(info_sync_payload->guild_info()->name()->str(), kE2EGuildName);
  ASSERT_NE(info_sync_payload->guild_info()->leader_name(), nullptr);
  EXPECT_EQ(info_sync_payload->guild_info()->leader_name()->str(), kE2EGuildLeaderName);
  ASSERT_NE(info_sync_payload->guild_info()->notice_list(), nullptr);
  ASSERT_EQ(info_sync_payload->guild_info()->notice_list()->size(), 1u);
  EXPECT_EQ(info_sync_payload->guild_info()->notice_list()->Get(0)->str(), "E2E Notice");
}

TEST_F(GatewayLogicUniversalForwardTest, TradeRequestForwardingAfterLogin) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  PumpClientFor(200ms);

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto locked = capture_weak.lock()) {
      locked->Push(routed.msg_id, routed.client_id);
    }
  });

  const auto login_payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq));
  ASSERT_FALSE(login_payload.empty());
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq), login_payload);
  ASSERT_TRUE(WaitForCondition(
      [capture]() { return capture->Contains(static_cast<uint16_t>(MsgId::kLoginReq)); },
      3s,
      [this]() { PumpClient(); }));

  const auto trade_payload = BuildPayloadForMsgId(kTradeReqMsgId);
  ASSERT_FALSE(trade_payload.empty());
  client_->send(kTradeReqMsgId, trade_payload);
  ASSERT_TRUE(WaitForCondition(
      [capture]() { return capture->Contains(kTradeReqMsgId); },
      3s,
      [this]() { PumpClient(); }));
}

TEST_F(GatewayLogicUniversalForwardTest, PartyInviteForwardingAfterLogin) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  PumpClientFor(200ms);

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto locked = capture_weak.lock()) {
      locked->Push(routed.msg_id, routed.client_id);
    }
  });

  const auto login_payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq));
  ASSERT_FALSE(login_payload.empty());
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq), login_payload);
  ASSERT_TRUE(WaitForCondition(
      [capture]() { return capture->Contains(static_cast<uint16_t>(MsgId::kLoginReq)); },
      3s,
      [this]() { PumpClient(); }));

  const auto party_payload = BuildPayloadForMsgId(kPartyInviteReqMsgId);
  ASSERT_FALSE(party_payload.empty());
  client_->send(kPartyInviteReqMsgId, party_payload);
  ASSERT_TRUE(WaitForCondition(
      [capture]() { return capture->Contains(kPartyInviteReqMsgId); },
      3s,
      [this]() { PumpClient(); }));
}

TEST_F(GatewayLogicUniversalForwardTest, TradeForwardingFromClientSession) {
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto locked = capture_weak.lock()) {
      locked->Push(routed.msg_id, routed.client_id);
    }
  });

  asio::io_context io_context;
  constexpr uint64_t kMockClientId = 910001;
  auto session = CreateMockSession(io_context, kMockClientId);
  ASSERT_NE(session, nullptr);
  gateway_->RegisterConnection(kMockClientId, session);

  const auto payload = BuildPayloadForMsgId(kTradeReqMsgId);
  ASSERT_FALSE(payload.empty());
  gateway_->HandleForwardMessage(
      session, kTradeReqMsgId, mir2::common::ChannelType::kTcp, payload);

  ASSERT_TRUE(WaitForCondition(
      [capture]() { return capture->Contains(kTradeReqMsgId); }, 3s));
  const auto client_ids = capture->DrainClientIds();
  ASSERT_FALSE(client_ids.empty());
  EXPECT_EQ(client_ids.front(), kMockClientId);
}

TEST_F(GatewayLogicUniversalForwardTest, PartyInviteForwardingFromClientSession) {
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto locked = capture_weak.lock()) {
      locked->Push(routed.msg_id, routed.client_id);
    }
  });

  asio::io_context io_context;
  constexpr uint64_t kMockClientId = 910002;
  auto session = CreateMockSession(io_context, kMockClientId);
  ASSERT_NE(session, nullptr);
  gateway_->RegisterConnection(kMockClientId, session);

  const auto payload = BuildPayloadForMsgId(kPartyInviteReqMsgId);
  ASSERT_FALSE(payload.empty());
  gateway_->HandleForwardMessage(
      session, kPartyInviteReqMsgId, mir2::common::ChannelType::kTcp, payload);

  ASSERT_TRUE(WaitForCondition(
      [capture]() { return capture->Contains(kPartyInviteReqMsgId); }, 3s));
  const auto client_ids = capture->DrainClientIds();
  ASSERT_FALSE(client_ids.empty());
  EXPECT_EQ(client_ids.front(), kMockClientId);
}

TEST_F(GatewayLogicUniversalForwardUdsTest, MoveReqForwardedFromClientSessionOverUds) {
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->gateway_session_ != nullptr; },
      3s,
      [this]() { client_->update(); }));
  ASSERT_NE(logic_->gateway_session_, nullptr);
  EXPECT_TRUE(logic_->gateway_session_->GetRemoteAddress().empty());

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto locked = capture_weak.lock()) {
      locked->Push(routed.msg_id, routed.client_id);
    }
  });

  asio::io_context io_context;
  constexpr uint64_t kMockClientId = 910003;
  auto session = CreateMockSession(io_context, kMockClientId);
  ASSERT_NE(session, nullptr);
  gateway_->RegisterConnection(kMockClientId, session);

  const auto payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq));
  ASSERT_FALSE(payload.empty());
  const auto move_msg_id = static_cast<uint16_t>(MsgId::kMoveReq);
  ASSERT_TRUE(WaitForCondition(
      [this, capture, session, payload, move_msg_id]() {
        if (capture->Contains(move_msg_id)) {
          return true;
        }
        // During initial handshake/restore transition, realtime packets may be
        // dropped once; retry until routed message is observed.
        gateway_->HandleForwardMessage(
            session, move_msg_id, mir2::common::ChannelType::kTcp, payload);
        return capture->Contains(move_msg_id);
      },
      3s,
      20ms,
      [this]() { client_->update(); }));
  const auto client_ids = capture->DrainClientIds();
  ASSERT_FALSE(client_ids.empty());
  EXPECT_EQ(client_ids.front(), kMockClientId);
}

// 验证5000并发连接场景下，转发耗时仍在可接受范围内。
TEST_F(GatewayLogicUniversalForwardTest, PerformanceUnder5000Connections) {
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  constexpr int kConnectionCount = 5000;
  constexpr int kThreadCount = 8;

  auto forwarded_count = std::make_shared<std::atomic<int>>(0);
  SetRoutedHandler([forwarded_count](const mir2::common::RoutedMessageData&) {
    forwarded_count->fetch_add(1, std::memory_order_relaxed);
  });

  asio::io_context io_context;
  std::vector<std::shared_ptr<TcpSession>> sessions;
  sessions.reserve(kConnectionCount);
  for (int i = 0; i < kConnectionCount; ++i) {
    auto session = CreateMockSession(io_context, 900000 + i);
    sessions.push_back(session);
    gateway_->RegisterConnection(900000 + i, session);
  }

  const auto payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq));
  ASSERT_FALSE(payload.empty());
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  const auto start = std::chrono::steady_clock::now();
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() {
      const int chunk = kConnectionCount / kThreadCount;
      const int begin = t * chunk;
      const int end = (t == kThreadCount - 1) ? kConnectionCount : begin + chunk;
      for (int i = begin; i < end; ++i) {
        gateway_->HandleForwardMessage(
            sessions[static_cast<size_t>(i)],
            static_cast<uint16_t>(MsgId::kMoveReq),
            mir2::common::ChannelType::kTcp,
            payload);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::seconds(2));

  ASSERT_TRUE(WaitForCondition(
      [forwarded_count]() { return forwarded_count->load() >= kConnectionCount; },
      5s));
  EXPECT_EQ(forwarded_count->load(), kConnectionCount);
}

}  // namespace
