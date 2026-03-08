#include <gtest/gtest.h>

#include <asio.hpp>
#include <entt/entt.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#define private public
#include "gateway/gateway_server.h"
#include "logic/logic_server.h"
#undef private

#include "client/network/dual_channel_client.h"
#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/message_codec.h"
#include "common/protocol/packet_codec.h"
#include "common/time_utils.h"
#include "integration/performance_report_generator.h"
#include "integration/test_helpers.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/registry_manager.h"
#include "logic/events/hot_event_pipeline.h"
#include "logic/handler_context.h"
#include "logic/handler_registry.h"
#include "logic/services/session_role_store.h"
#include "auction_generated.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "guild_generated.h"
#include "login_generated.h"
#include "party_generated.h"
#include "system_generated.h"
#include "trade_generated.h"

namespace {

using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::InternalMsgId;
using mir2::common::LoginRequest;
using mir2::common::LoginResponse;
using mir2::common::MessageCodecStatus;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::common::ProtocolVersion;
using mir2::gateway::GatewayServer;
using mir2::logic::LogicServer;
using mir2::network::Packet;
using mir2::network::TcpSession;
using mir2::test::integration::PerformanceMonitor;
using mir2::test::integration::WaitForCondition;

constexpr const char* kHost = "127.0.0.1";
constexpr uint16_t kHeartbeatMsgId = static_cast<uint16_t>(MsgId::kHeartbeat);
constexpr uint16_t kHeartbeatRspMsgId = static_cast<uint16_t>(MsgId::kHeartbeatRsp);
constexpr uint16_t kLoginReqMsgId = static_cast<uint16_t>(MsgId::kLoginReq);
constexpr uint16_t kLoginRspMsgId = static_cast<uint16_t>(MsgId::kLoginRsp);
constexpr uint16_t kSelectRoleRspMsgId = static_cast<uint16_t>(MsgId::kSelectRoleRsp);
constexpr uint16_t kMoveReqMsgId = static_cast<uint16_t>(MsgId::kMoveReq);
constexpr uint16_t kAttackReqMsgId = static_cast<uint16_t>(MsgId::kAttackReq);
constexpr uint16_t kSkillReqMsgId = static_cast<uint16_t>(MsgId::kSkillReq);
constexpr uint16_t kChatReqMsgId = static_cast<uint16_t>(MsgId::kChatReq);
constexpr uint16_t kBuffAddMsgId = static_cast<uint16_t>(MsgId::kBuffAdd);
constexpr uint16_t kRespawnMsgId = static_cast<uint16_t>(MsgId::kRespawn);
constexpr uint16_t kStateSyncMsgId = static_cast<uint16_t>(MsgId::kStateSync);
constexpr uint16_t kGuildCreateReqMsgId = static_cast<uint16_t>(MsgId::kGuildCreateReq);
constexpr uint16_t kGuildCreateRspMsgId = static_cast<uint16_t>(MsgId::kGuildCreateRsp);
constexpr uint16_t kGuildInfoSyncMsgId = static_cast<uint16_t>(MsgId::kGuildInfoSync);
constexpr uint16_t kTradeRspMsgId = static_cast<uint16_t>(MsgId::kTradeRsp);
constexpr uint16_t kTradeUpdateMsgId = static_cast<uint16_t>(MsgId::kTradeUpdate);
constexpr uint16_t kTradeCompleteMsgId = static_cast<uint16_t>(MsgId::kTradeComplete);
constexpr uint16_t kTradeReqMsgId = static_cast<uint16_t>(MsgId::kTradeReq);
constexpr uint16_t kPartyInviteReqMsgId = static_cast<uint16_t>(MsgId::kPartyInviteReq);
constexpr uint16_t kPartyInviteRspMsgId =
    static_cast<uint16_t>(MsgId::kPartyInviteRsp);
constexpr uint16_t kPartyUpdateMsgId = static_cast<uint16_t>(MsgId::kPartyUpdate);
constexpr uint16_t kAuctionListReqMsgId = static_cast<uint16_t>(MsgId::kAuctionListReq);
constexpr uint16_t kAuctionListRspMsgId = static_cast<uint16_t>(MsgId::kAuctionListRsp);
constexpr uint16_t kAuctionSellReqMsgId = static_cast<uint16_t>(MsgId::kAuctionSellReq);
constexpr uint16_t kAuctionSellRspMsgId = static_cast<uint16_t>(MsgId::kAuctionSellRsp);
constexpr uint16_t kAuctionBuyReqMsgId = static_cast<uint16_t>(MsgId::kAuctionBuyReq);
constexpr uint16_t kAuctionBuyRspMsgId = static_cast<uint16_t>(MsgId::kAuctionBuyRsp);
constexpr uint16_t kAuctionNotifyMsgId = static_cast<uint16_t>(MsgId::kAuctionNotify);

constexpr const char* kEmitBuffUser = "integration_emit_buff";
constexpr const char* kEmitRespawnUser = "integration_emit_respawn";
constexpr const char* kEmitStateSyncUser = "integration_emit_statesync";
constexpr const char* kEmitTradeUser = "integration_emit_trade";
constexpr const char* kEmitPartyUser = "integration_emit_party";
constexpr const char* kRealTradeUser = "integration_real_trade";
constexpr const char* kRealPartyUser = "integration_real_party";
constexpr const char* kRealGuildUser = "integration_real_guild";
constexpr const char* kRealTradePeerUser = "integration_real_trade_peer";
constexpr const char* kRealPartyPeerUser = "integration_real_party_peer";
constexpr const char* kRealAuctionUser = "integration_real_auction";
constexpr const char* kRealAuctionPeerUser = "integration_real_auction_peer";
constexpr uint32_t kRealTradeRequesterCharacterId = 8101;
constexpr uint32_t kRealTradeTargetCharacterId = 8102;
constexpr uint32_t kRealPartyLeaderCharacterId = 8201;
constexpr uint32_t kRealPartyTargetCharacterId = 8202;
constexpr uint32_t kRealAuctionSellerCharacterId = 8301;
constexpr uint32_t kRealAuctionBuyerCharacterId = 8302;
constexpr int kRealTradeRequesterGold = 200000;
constexpr int kRealTradeTargetGold = 150000;
constexpr int kRealGuildLeaderGold = 2000000;
constexpr int kRealAuctionSellerGold = 50000;
constexpr int kRealAuctionBuyerGold = 500000;
constexpr uint16_t kRealAuctionInventorySlot = 7;
constexpr uint32_t kRealAuctionItemId = 91001;
constexpr uint32_t kRealAuctionSellCount = 2;
constexpr uint32_t kRealAuctionUnitPrice = 1200;
constexpr uint32_t kRealAuctionDurationSec = 120;

constexpr uint64_t kE2EPlayerId = 4242;
constexpr uint32_t kE2EBuffId = 7007;
constexpr uint32_t kE2EBuffDurationMs = 5000;
constexpr uint16_t kE2EBuffStackCount = 2;
constexpr int32_t kE2ERespawnX = 88;
constexpr int32_t kE2ERespawnY = 99;
constexpr int32_t kE2ERespawnHp = 123;
constexpr int32_t kE2ERespawnMp = 45;
constexpr uint32_t kE2EMapId = 3;
constexpr int32_t kE2EStateSyncX = 77;
constexpr int32_t kE2EStateSyncY = 66;
constexpr int32_t kE2EStateSyncHp = 180;
constexpr int32_t kE2EStateSyncMaxHp = 200;
constexpr int32_t kE2EStateSyncMp = 80;
constexpr int32_t kE2EStateSyncMaxMp = 120;
constexpr uint16_t kE2EStateSyncLevel = 25;
constexpr uint64_t kE2ESnapshotEntityId = 9001;
constexpr uint64_t kE2ETradeId = 885566;
constexpr uint16_t kE2ETradeLeftItemSlot = 3;
constexpr uint32_t kE2ETradeLeftItemId = 10086;
constexpr uint32_t kE2ETradeLeftItemCount = 2;
constexpr uint32_t kE2ETradeLeftGold = 888;
constexpr uint32_t kE2ETradeRightGold = 666;
constexpr uint32_t kE2ETradePartnerCharacterId = 4343;
constexpr uint64_t kE2EPartyId = 7120;
constexpr uint32_t kE2EPartyMemberCharacterId = 5353;
constexpr uint16_t kE2EPartyLeaderHp = 180;
constexpr uint16_t kE2EPartyLeaderMaxHp = 200;
constexpr uint32_t kE2EPartyLeaderMapId = 3;
constexpr uint16_t kE2EPartyLeaderX = 77;
constexpr uint16_t kE2EPartyLeaderY = 66;
constexpr uint16_t kE2EPartyMemberHp = 160;
constexpr uint16_t kE2EPartyMemberMaxHp = 190;
constexpr uint32_t kE2EPartyMemberMapId = 3;
constexpr uint16_t kE2EPartyMemberX = 88;
constexpr uint16_t kE2EPartyMemberY = 55;
constexpr uint16_t kPressureMetricsPort = 9091;
constexpr int kPressureTickIntervalMs = 50;
constexpr int kPressureLoginIpRateLimitCapacity = 10000;
constexpr int kPressureLoginIpRateLimitRefillRate = 10000;
constexpr int kPressureGatewayIoThreads = 4;
constexpr int kPressureServerMaxConnections = 4096;
constexpr int kPressureServiceLinkWriteQueueSize = 262144;
constexpr double kServiceLinkDisconnectRateEpsilon = 1e-9;
constexpr const char* kPressureUserPrefix = "pressure_user_";
constexpr const char* kStage4MarkdownReport =
    "docs/STAGE4-LOGIC-BOTTLENECK-REPORT.md";
constexpr const char* kStage4CsvReport = "docs/STAGE4-LOGIC-BOTTLENECK-REPORT.csv";

std::vector<uint8_t> BuildBuffAddPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = mir2::proto::CreateBuffAdd(builder,
                                                   kE2EPlayerId,
                                                   mir2::proto::EntityType::PLAYER,
                                                   kE2EBuffId,
                                                   kE2EBuffDurationMs,
                                                   kE2EBuffStackCount);
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildRespawnPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = mir2::proto::CreateRespawn(builder,
                                                   kE2EPlayerId,
                                                   mir2::proto::EntityType::PLAYER,
                                                   kE2ERespawnX,
                                                   kE2ERespawnY,
                                                   kE2ERespawnHp,
                                                   kE2ERespawnMp);
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildStateSyncPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto player_name = builder.CreateString("e2e_player");
  const auto player = mir2::proto::CreatePlayerInfo(builder,
                                                     kE2EPlayerId,
                                                     player_name,
                                                     mir2::proto::Profession::WARRIOR,
                                                     kE2EStateSyncLevel,
                                                     kE2EStateSyncHp,
                                                     kE2EStateSyncMaxHp,
                                                     kE2EStateSyncMp,
                                                     kE2EStateSyncMaxMp,
                                                     kE2EMapId,
                                                     kE2EStateSyncX,
                                                     kE2EStateSyncY,
                                                     999);
  std::vector<flatbuffers::Offset<mir2::proto::EntitySnapshot>> entities;
  entities.push_back(mir2::proto::CreateEntitySnapshot(builder,
                                                       kE2ESnapshotEntityId,
                                                       mir2::proto::EntityType::MONSTER,
                                                       /*x=*/70,
                                                       /*y=*/71,
                                                       /*direction=*/2,
                                                       /*hp=*/90,
                                                       /*max_hp=*/100,
                                                       /*mp=*/30,
                                                       /*max_mp=*/40));
  const auto entities_offset = builder.CreateVector(entities);
  const auto payload = mir2::proto::CreateStateSync(builder, player, entities_offset);
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTradeRspPayload(bool success,
                                          mir2::proto::ErrorCode code,
                                          uint64_t trade_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto payload =
      mir2::proto::CreateTradeRsp(builder, success, static_cast<int>(code), trade_id);
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTradeUpdatePayload(uint32_t right_character_id) {
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> left_items;
  left_items.push_back(mir2::proto::CreateTradeItemInfo(builder,
                                                        kE2ETradeLeftItemSlot,
                                                        kE2ETradeLeftItemId,
                                                        kE2ETradeLeftItemCount));
  const auto left_items_vec = builder.CreateVector(left_items);

  const auto update = mir2::proto::CreateTradeUpdate(builder,
                                                      kE2ETradeId,
                                                      static_cast<uint32_t>(kE2EPlayerId),
                                                      right_character_id,
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

std::vector<uint8_t> BuildTradeCompletePayload(bool success,
                                               mir2::proto::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = mir2::proto::CreateTradeComplete(
      builder, kE2ETradeId, success, static_cast<int>(code));
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPartyInviteRspPayload(bool success,
                                                mir2::proto::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto payload =
      mir2::proto::CreatePartyInviteRsp(builder, success, static_cast<int>(code));
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPartyUpdatePayload(uint32_t member_character_id) {
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<mir2::proto::PartyMemberInfo>> members;
  const auto leader_name = builder.CreateString("e2e_leader");
  members.push_back(mir2::proto::CreatePartyMemberInfo(builder,
                                                        static_cast<uint32_t>(kE2EPlayerId),
                                                        leader_name,
                                                        kE2EPartyLeaderHp,
                                                        kE2EPartyLeaderMaxHp,
                                                        kE2EPartyLeaderMapId,
                                                        kE2EPartyLeaderX,
                                                        kE2EPartyLeaderY,
                                                        true));

  const auto member_name = builder.CreateString("e2e_member");
  members.push_back(mir2::proto::CreatePartyMemberInfo(builder,
                                                        member_character_id,
                                                        member_name,
                                                        kE2EPartyMemberHp,
                                                        kE2EPartyMemberMaxHp,
                                                        kE2EPartyMemberMapId,
                                                        kE2EPartyMemberX,
                                                        kE2EPartyMemberY,
                                                        true));

  const auto members_vec = builder.CreateVector(members);
  const auto payload = mir2::proto::CreatePartyUpdate(builder,
                                                       kE2EPartyId,
                                                       static_cast<uint32_t>(kE2EPlayerId),
                                                       members_vec);
  builder.Finish(payload);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildTradeReqPayload(uint32_t target_character_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateTradeReq(builder, target_character_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPartyInviteReqPayload(uint32_t target_character_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreatePartyInviteReq(builder, target_character_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildAuctionListReqPayload(uint32_t page,
                                                uint32_t page_size,
                                                bool seller_only) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionListReq(builder, page, page_size, seller_only);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildAuctionSellReqPayload(uint16_t inventory_slot,
                                                uint32_t item_id,
                                                uint32_t count,
                                                uint32_t unit_price,
                                                uint32_t duration_sec) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionSellReq(
      builder, inventory_slot, item_id, count, unit_price, duration_sec);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildAuctionBuyReqPayload(uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionBuyReq(builder, listing_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildGuildCreateReqPayload(const std::string& guild_name) {
  flatbuffers::FlatBufferBuilder builder;
  const auto guild_name_offset = builder.CreateString(guild_name);
  const auto req = mir2::proto::CreateCreateGuildRequest(builder, guild_name_offset);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::string BuildUniqueGuildName(uint64_t seed) {
  return "RealGuild_" + std::to_string(seed) + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::optional<uint32_t> ParsePressureCharacterId(const std::string& username) {
  if (username.rfind(kPressureUserPrefix, 0) != 0) {
    return std::nullopt;
  }
  const std::string suffix = username.substr(std::strlen(kPressureUserPrefix));
  if (suffix.empty()) {
    return std::nullopt;
  }
  try {
    const unsigned long parsed = std::stoul(suffix);
    if (parsed == 0 ||
        parsed > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
      return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
  } catch (...) {
    return std::nullopt;
  }
}

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
                               const std::string& uds_path = "",
                               uint16_t metrics_port = 0,
                               int tick_interval_ms = 20,
                               int login_ip_rate_limit_capacity = 5,
                               int login_ip_rate_limit_refill_rate = 1,
                               int io_threads = 1,
                               int max_connections = 128,
                               int service_link_write_queue_size = 8192) {
  std::ostringstream out;
  out << "server:\n"
      << "  id: 110\n"
      << "  name: \"Test-Gateway\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.gateway_tcp << "\n"
      << "  udp_port: " << ports.gateway_udp << "\n"
      << "  metrics_port: " << metrics_port << "\n"
      << "  io_threads: " << io_threads << "\n"
      << "  max_connections: " << max_connections << "\n"
      << "  tick_interval_ms: " << tick_interval_ms << "\n"
      << "  heartbeat_timeout_ms: 0\n"
      << "  service_link_write_queue_size: "
      << std::max(service_link_write_queue_size, 1) << "\n"
      << "  login_ip_rate_limit_capacity: " << login_ip_rate_limit_capacity << "\n"
      << "  login_ip_rate_limit_refill_rate: " << login_ip_rate_limit_refill_rate << "\n"
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
                             const std::string& uds_path = "",
                             uint16_t metrics_port = 0,
                             int tick_interval_ms = 20,
                             int io_threads = 1,
                             int max_connections = 128,
                             int service_link_write_queue_size = 8192) {
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
      << "  metrics_port: " << metrics_port << "\n"
      << "  io_threads: " << io_threads << "\n"
      << "  max_connections: " << max_connections << "\n"
      << "  tick_interval_ms: " << tick_interval_ms << "\n"
      << "  service_link_write_queue_size: "
      << std::max(service_link_write_queue_size, 1) << "\n"
      << "  network_session_idle_check_interval_ms: 30000\n"
      << "  network_session_idle_timeout_ms: 90000\n"
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

struct GatewayIpcProbe {
  void Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    responses.clear();
  }

  void OnPacket(const Packet& packet) {
    if (packet.msg_id != kHeartbeatRspMsgId) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    responses.push_back(std::chrono::steady_clock::now());
    cv.notify_all();
  }

  std::optional<std::chrono::steady_clock::time_point> WaitForResponse(
      std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!cv.wait_for(lock, timeout, [&]() { return !responses.empty(); })) {
      return std::nullopt;
    }
    auto time_point = responses.front();
    responses.pop_front();
    return time_point;
  }

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::chrono::steady_clock::time_point> responses;
};

class GatewayLogicIntegrationTest : public ::testing::Test {
 protected:
  virtual std::string IpcTransport() const { return "tcp"; }
  virtual std::string IpcUdsPath() const { return ""; }

  void SetUp() override {
    ports_ = AllocateTestPorts();
    temp_dir_ = CreateTempDir("mir2_gateway_logic");
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

    gateway_ = std::make_unique<GatewayServer>();
    ASSERT_TRUE(gateway_->Initialize(gateway_config_path_));

    logic_ = std::make_unique<LogicServer>();
    ASSERT_TRUE(logic_->Initialize(logic_config_path_));
    scripted_trade_login_branch_count_.store(0, std::memory_order_relaxed);
    scripted_party_login_branch_count_.store(0, std::memory_order_relaxed);
    InstallLogicHandlers();

    logic_thread_ = std::thread([this]() { logic_->Run(); });
    gateway_->Run();

    client_ = std::make_unique<DualChannelClient>();
  }

  void TearDown() override {
    if (client_) {
      client_->disconnect();
    }
    if (logic_) {
      logic_->Shutdown();
    }
    if (logic_thread_.joinable()) {
      logic_thread_.join();
    }
    if (gateway_) {
      gateway_->Shutdown();
    }
    gateway_.reset();
    logic_.reset();
    client_.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  bool SendLoginSuccess(const std::shared_ptr<TcpSession>& session,
                        uint64_t client_id) const {
    if (!session || client_id == 0) {
      return false;
    }

    LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.account_id = 42;
    response.session_token = "integration_token";

    MessageCodecStatus status = MessageCodecStatus::kOk;
    auto rsp_payload = mir2::common::EncodeLoginResponse(response, &status);
    if (status != MessageCodecStatus::kOk || rsp_payload.empty()) {
      return false;
    }

    const auto routed_rsp = mir2::common::BuildRoutedMessage(
        client_id,
        kLoginRspMsgId,
        rsp_payload);
    session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage), routed_rsp);
    return true;
  }

  bool SendSelectRoleSuccess(const std::shared_ptr<TcpSession>& session,
                             uint64_t client_id,
                             uint64_t player_id) const {
    if (!session || client_id == 0 || player_id == 0) {
      return false;
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateSelectRoleRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, player_id);
    builder.Finish(rsp);

    const uint8_t* data = builder.GetBufferPointer();
    const std::vector<uint8_t> payload(data, data + builder.GetSize());
    if (payload.empty()) {
      return false;
    }

    const auto routed_rsp = mir2::common::BuildRoutedMessage(
        client_id, kSelectRoleRspMsgId, payload);
    session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage), routed_rsp);
    return true;
  }

  entt::entity UpsertOnlineCharacter(uint32_t character_id,
                                     const std::string& name,
                                     int gold) {
    if (character_id == 0 || !logic_ || !logic_->registry_manager_) {
      return entt::null;
    }

    auto& character_manager = logic_->registry_manager_->GetCharacterManager();
    const entt::entity entity = character_manager.GetOrCreate(character_id, /*map_id=*/1);
    if (entity == entt::null) {
      return entt::null;
    }

    character_manager.OnLogin(character_id);
    auto* registry = character_manager.TryGetRegistry(character_id);
    if (!registry || !registry->valid(entity)) {
      return entt::null;
    }

    auto& identity =
        registry->get_or_emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = character_id;
    identity.account_id = static_cast<mir2::ecs::AccountId>(character_id);
    identity.name = name;

    auto& state = registry->get_or_emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.is_online = true;
    state.map_id = 1;
    const bool is_pressure_character = name.rfind(kPressureUserPrefix, 0) == 0;
    if (is_pressure_character) {
      constexpr int kPressureGridCols = 10;
      constexpr int kPressureGridSpacing = 8;
      const uint32_t pressure_index = character_id > 900000 ? (character_id - 900001) : 0;
      const int32_t col = static_cast<int32_t>(pressure_index % kPressureGridCols);
      const int32_t row = static_cast<int32_t>(pressure_index / kPressureGridCols);
      state.position = {100 + col * kPressureGridSpacing, 100 + row * kPressureGridSpacing};
    } else {
      state.position = {100, 100};
    }

    auto& attributes =
        registry->get_or_emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    constexpr int kDefaultMaxHp = 200;
    constexpr int kDefaultMaxMp = 120;
    constexpr int kPressureMaxHp = 1'000'000;
    constexpr int kPressureMaxMp = 100'000;
    attributes.max_hp =
        std::max(attributes.max_hp, is_pressure_character ? kPressureMaxHp : kDefaultMaxHp);
    attributes.hp = std::max(attributes.hp, attributes.max_hp);
    attributes.max_mp =
        std::max(attributes.max_mp, is_pressure_character ? kPressureMaxMp : kDefaultMaxMp);
    attributes.mp = std::max(attributes.mp, attributes.max_mp);
    attributes.gold = gold;

    (void)registry->get_or_emplace<mir2::ecs::ChatPreferenceComponent>(entity);
    return entity;
  }

  bool BindAuthenticatedSession(uint64_t client_id, uint32_t character_id) {
    if (client_id == 0 || character_id == 0 || !logic_ || !logic_->role_store_) {
      return false;
    }
    logic_->client_registry_.Track(client_id);
    logic_->role_store_->BindClientAccount(client_id, character_id);
    logic_->role_store_->BindClientRole(client_id, character_id);
    return true;
  }

  bool PrepareRealTradeScenario(uint64_t client_id) {
    if (!BindAuthenticatedSession(client_id, kRealTradeRequesterCharacterId)) {
      return false;
    }
    const auto requester = UpsertOnlineCharacter(kRealTradeRequesterCharacterId,
                                                 "real_trade_requester",
                                                 kRealTradeRequesterGold);
    const auto target = UpsertOnlineCharacter(kRealTradeTargetCharacterId,
                                              "real_trade_target",
                                              kRealTradeTargetGold);
    return requester != entt::null && target != entt::null;
  }

  bool PrepareRealTradePeerScenario(uint64_t client_id) {
    if (!BindAuthenticatedSession(client_id, kRealTradeTargetCharacterId)) {
      return false;
    }
    const auto requester = UpsertOnlineCharacter(kRealTradeRequesterCharacterId,
                                                 "real_trade_requester",
                                                 kRealTradeRequesterGold);
    const auto target = UpsertOnlineCharacter(kRealTradeTargetCharacterId,
                                              "real_trade_target",
                                              kRealTradeTargetGold);
    return requester != entt::null && target != entt::null;
  }

  bool PrepareRealPartyScenario(uint64_t client_id) {
    if (!BindAuthenticatedSession(client_id, kRealPartyLeaderCharacterId)) {
      return false;
    }
    const auto leader = UpsertOnlineCharacter(kRealPartyLeaderCharacterId,
                                              "real_party_leader",
                                              kRealTradeRequesterGold);
    const auto target = UpsertOnlineCharacter(kRealPartyTargetCharacterId,
                                              "real_party_target",
                                              kRealTradeTargetGold);
    return leader != entt::null && target != entt::null;
  }

  bool PrepareRealPartyPeerScenario(uint64_t client_id) {
    if (!BindAuthenticatedSession(client_id, kRealPartyTargetCharacterId)) {
      return false;
    }
    const auto leader = UpsertOnlineCharacter(kRealPartyLeaderCharacterId,
                                              "real_party_leader",
                                              kRealTradeRequesterGold);
    const auto target = UpsertOnlineCharacter(kRealPartyTargetCharacterId,
                                              "real_party_target",
                                              kRealTradeTargetGold);
    return leader != entt::null && target != entt::null;
  }

  bool SeedInventoryItem(uint32_t character_id,
                         uint16_t slot,
                         uint32_t item_id,
                         int count) {
    if (!logic_ || !logic_->registry_manager_ || character_id == 0 || item_id == 0 ||
        count <= 0) {
      return false;
    }

    auto& character_manager = logic_->registry_manager_->GetCharacterManager();
    const auto owner = character_manager.TryGet(character_id);
    if (!owner.has_value()) {
      return false;
    }

    auto* registry = character_manager.TryGetRegistry(character_id);
    if (!registry || !registry->valid(*owner)) {
      return false;
    }

    auto view = registry->view<mir2::ecs::ItemComponent, mir2::ecs::InventoryOwnerComponent>();
    for (const entt::entity entity : view) {
      const auto& owner_component = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
      if (owner_component.owner == *owner &&
          owner_component.slot_index == static_cast<int>(slot)) {
        registry->destroy(entity);
      }
    }

    const entt::entity item_entity = registry->create();
    auto& item = registry->emplace<mir2::ecs::ItemComponent>(item_entity);
    item.instance_id = static_cast<uint64_t>(entt::to_integral(item_entity));
    item.item_id = item_id;
    item.count = count;

    auto& owner_component =
        registry->emplace<mir2::ecs::InventoryOwnerComponent>(item_entity);
    owner_component.owner = *owner;
    owner_component.slot_index = static_cast<int>(slot);
    return true;
  }

  bool PrepareRealAuctionScenario(uint64_t client_id) {
    if (!BindAuthenticatedSession(client_id, kRealAuctionSellerCharacterId)) {
      return false;
    }

    const auto seller = UpsertOnlineCharacter(kRealAuctionSellerCharacterId,
                                              "real_auction_seller",
                                              kRealAuctionSellerGold);
    const auto buyer = UpsertOnlineCharacter(kRealAuctionBuyerCharacterId,
                                             "real_auction_buyer",
                                             kRealAuctionBuyerGold);
    if (seller == entt::null || buyer == entt::null) {
      return false;
    }

    return SeedInventoryItem(kRealAuctionSellerCharacterId,
                             kRealAuctionInventorySlot,
                             kRealAuctionItemId,
                             static_cast<int>(kRealAuctionSellCount + 1));
  }

  bool PrepareRealAuctionPeerScenario(uint64_t client_id) {
    if (!BindAuthenticatedSession(client_id, kRealAuctionBuyerCharacterId)) {
      return false;
    }

    const auto seller = UpsertOnlineCharacter(kRealAuctionSellerCharacterId,
                                              "real_auction_seller",
                                              kRealAuctionSellerGold);
    const auto buyer = UpsertOnlineCharacter(kRealAuctionBuyerCharacterId,
                                             "real_auction_buyer",
                                             kRealAuctionBuyerGold);
    return seller != entt::null && buyer != entt::null;
  }

  bool PrepareRealGuildScenario(uint64_t client_id) {
    if (client_id == 0 ||
        client_id > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    const uint32_t character_id = static_cast<uint32_t>(client_id);
    if (!BindAuthenticatedSession(client_id, character_id)) {
      return false;
    }
    const auto leader = UpsertOnlineCharacter(character_id,
                                              "real_guild_leader_" +
                                                  std::to_string(character_id),
                                              kRealGuildLeaderGold);
    return leader != entt::null;
  }

  void InstallLogicHandlers() {
    ASSERT_NE(logic_, nullptr);
    ASSERT_NE(logic_->network_, nullptr);

    logic_->network_->RegisterHandler(
        static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
        [this](const std::shared_ptr<TcpSession>& session,
               const std::vector<uint8_t>& payload) {
          if (!logic_) {
            return;
          }

          mir2::common::RoutedMessageData routed;
          const bool parsed = session &&
                              mir2::common::ParseRoutedMessage(payload, &routed) &&
                              routed.msg_id == kLoginReqMsgId;
          if (parsed) {
            LoginRequest request;
            const auto decode_status =
                mir2::common::DecodeLoginRequest(routed.msg_id, routed.payload, &request);
            if (decode_status == MessageCodecStatus::kOk) {
              if (const auto pressure_character_id =
                      ParsePressureCharacterId(request.username);
                  pressure_character_id.has_value()) {
                if (BindAuthenticatedSession(routed.client_id, *pressure_character_id) &&
                    UpsertOnlineCharacter(*pressure_character_id,
                                          request.username,
                                          kRealTradeRequesterGold) != entt::null) {
                  SendLoginSuccess(session, routed.client_id);
                  SendSelectRoleSuccess(session, routed.client_id, *pressure_character_id);
                }
                return;
              }
              if (request.username == kRealTradeUser) {
                if (PrepareRealTradeScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }
              if (request.username == kRealTradePeerUser) {
                if (PrepareRealTradePeerScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }
              if (request.username == kRealPartyUser) {
                if (PrepareRealPartyScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }
              if (request.username == kRealPartyPeerUser) {
                if (PrepareRealPartyPeerScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }
              if (request.username == kRealAuctionUser) {
                if (PrepareRealAuctionScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }
              if (request.username == kRealAuctionPeerUser) {
                if (PrepareRealAuctionPeerScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }
              if (request.username == kRealGuildUser) {
                if (PrepareRealGuildScenario(routed.client_id)) {
                  SendLoginSuccess(session, routed.client_id);
                }
                return;
              }

              if (request.username == kEmitTradeUser || request.username == kEmitPartyUser) {
                SendLoginSuccess(session, routed.client_id);

                if (request.username == kEmitTradeUser) {
                  scripted_trade_login_branch_count_.fetch_add(1, std::memory_order_relaxed);
                  const auto routed_trade_rsp = mir2::common::BuildRoutedMessage(
                      routed.client_id,
                      kTradeRspMsgId,
                      BuildTradeRspPayload(true, mir2::proto::ErrorCode::ERR_OK, kE2ETradeId));
                  session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
                                routed_trade_rsp);
                  const auto routed_trade_update = mir2::common::BuildRoutedMessage(
                      routed.client_id,
                      kTradeUpdateMsgId,
                      BuildTradeUpdatePayload(kE2ETradePartnerCharacterId));
                  session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
                                routed_trade_update);
                  const auto routed_trade_complete = mir2::common::BuildRoutedMessage(
                      routed.client_id,
                      kTradeCompleteMsgId,
                      BuildTradeCompletePayload(true, mir2::proto::ErrorCode::ERR_OK));
                  session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
                                routed_trade_complete);
                } else {
                  scripted_party_login_branch_count_.fetch_add(1, std::memory_order_relaxed);
                  const auto routed_party_rsp = mir2::common::BuildRoutedMessage(
                      routed.client_id,
                      kPartyInviteRspMsgId,
                      BuildPartyInviteRspPayload(true, mir2::proto::ErrorCode::ERR_OK));
                  session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
                                routed_party_rsp);
                  const auto routed_party_update = mir2::common::BuildRoutedMessage(
                      routed.client_id,
                      kPartyUpdateMsgId,
                      BuildPartyUpdatePayload(kE2EPartyMemberCharacterId));
                  session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
                                routed_party_update);
                }
                return;
              }
            }
          }

          logic_->HandleRoutedMessage(session, payload);

          if (!session) {
            return;
          }

          if (!mir2::common::ParseRoutedMessage(payload, &routed) ||
              routed.msg_id != kLoginReqMsgId) {
            return;
          }

          LoginRequest request;
          const auto decode_status =
              mir2::common::DecodeLoginRequest(routed.msg_id, routed.payload, &request);
          if (decode_status != MessageCodecStatus::kOk) {
            return;
          }

          if (!SendLoginSuccess(session, routed.client_id)) {
            return;
          }

          std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extra_payloads;
          if (request.username == kEmitBuffUser) {
            extra_payloads.emplace_back(kBuffAddMsgId, BuildBuffAddPayload());
          } else if (request.username == kEmitRespawnUser) {
            extra_payloads.emplace_back(kRespawnMsgId, BuildRespawnPayload());
          } else if (request.username == kEmitStateSyncUser) {
            extra_payloads.emplace_back(kStateSyncMsgId, BuildStateSyncPayload());
          } else if (request.username == kEmitTradeUser) {
            scripted_trade_login_branch_count_.fetch_add(1, std::memory_order_relaxed);
            extra_payloads.emplace_back(
                kTradeRspMsgId,
                BuildTradeRspPayload(true,
                                     mir2::proto::ErrorCode::ERR_OK,
                                     kE2ETradeId));
            extra_payloads.emplace_back(
                kTradeUpdateMsgId,
                BuildTradeUpdatePayload(kE2ETradePartnerCharacterId));
            extra_payloads.emplace_back(
                kTradeCompleteMsgId,
                BuildTradeCompletePayload(true, mir2::proto::ErrorCode::ERR_OK));
          } else if (request.username == kEmitPartyUser) {
            scripted_party_login_branch_count_.fetch_add(1, std::memory_order_relaxed);
            extra_payloads.emplace_back(
                kPartyInviteRspMsgId,
                BuildPartyInviteRspPayload(true, mir2::proto::ErrorCode::ERR_OK));
            extra_payloads.emplace_back(
                kPartyUpdateMsgId,
                BuildPartyUpdatePayload(kE2EPartyMemberCharacterId));
          }

          for (const auto& extra_payload : extra_payloads) {
            const auto routed_extra = mir2::common::BuildRoutedMessage(
                routed.client_id, extra_payload.first, extra_payload.second);
            session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage), routed_extra);
          }
        });

    logic_->network_->RegisterHandler(
        kHeartbeatMsgId,
        [](const std::shared_ptr<TcpSession>& session,
           const std::vector<uint8_t>& payload) {
          if (!session) {
            return;
          }
          uint32_t seq = 0;
          uint32_t timestamp = static_cast<uint32_t>(mir2::common::now_ms());
          if (!payload.empty()) {
            flatbuffers::Verifier verifier(payload.data(), payload.size());
            if (verifier.VerifyBuffer<mir2::proto::Heartbeat>(nullptr)) {
              const auto* heartbeat =
                  flatbuffers::GetRoot<mir2::proto::Heartbeat>(payload.data());
              if (heartbeat) {
                seq = heartbeat->seq();
                timestamp = heartbeat->client_time();
              }
            }
          }

          flatbuffers::FlatBufferBuilder builder;
          const auto rsp = mir2::proto::CreateHeartbeatRsp(builder, seq, timestamp);
          builder.Finish(rsp);
          const uint8_t* data = builder.GetBufferPointer();
          std::vector<uint8_t> rsp_payload(data, data + builder.GetSize());
          session->Send(kHeartbeatRspMsgId, rsp_payload);
        });
  }

  bool ConnectClient(DualChannelClient* client) {
    if (!client) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (client->connect(kHost, ports_.gateway_tcp)) {
        break;
      }
      std::this_thread::sleep_for(50ms);
    }

    return WaitForCondition(
        [&]() { return client->is_connected(); },
        2s,
        10ms,
        [&]() { client->update(); });
  }

  bool ConnectClient() {
    return ConnectClient(client_.get());
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
          return gateway_ &&
                 gateway_->holder_state_ ==
                     mir2::gateway::ConnectionHolder::State::FORWARDING;
        },
        timeout,
        10ms,
        [this]() { PumpClient(); });
  }

  std::optional<NetworkPacket> WaitForPacket(uint16_t msg_id,
                                             std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpClient();
      auto it = std::find_if(
          pending_packets_.begin(),
          pending_packets_.end(),
          [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
      if (it != pending_packets_.end()) {
        NetworkPacket packet = std::move(*it);
        pending_packets_.erase(it);
        return packet;
      }
      std::this_thread::sleep_for(2ms);
    }
    PumpClient();
    auto it = std::find_if(
        pending_packets_.begin(),
        pending_packets_.end(),
        [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
    if (it != pending_packets_.end()) {
      NetworkPacket packet = std::move(*it);
      pending_packets_.erase(it);
      return packet;
    }
    return std::nullopt;
  }

  std::optional<ProtocolVersion> WaitForSingleSessionProtocolVersion(
      std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpClient();
      if (gateway_ && gateway_->network_) {
        const auto sessions = gateway_->network_->GetAllSessions();
        if (sessions.size() == 1 && sessions.front()) {
          return sessions.front()->GetProtocolVersion();
        }
      }
      std::this_thread::sleep_for(5ms);
    }
    return std::nullopt;
  }

  void InstallGatewayIpcProbe() {
    ASSERT_NE(gateway_, nullptr);
    ASSERT_NE(gateway_->logic_client_, nullptr);
    gateway_->logic_client_->SetPacketHandler(
        [this](const Packet& packet) {
          ipc_probe_.OnPacket(packet);
          gateway_->OnLogicPacket(packet);
        });
  }

  std::vector<uint8_t> BuildLoginPayload(const std::string& username = "integration_user") const {
    LoginRequest request;
    request.username = username;
    request.password = "integration_pw";
    request.version = std::to_string(
        static_cast<uint32_t>(mir2::proto::SchemaVersion::kSchemaVersion));

    MessageCodecStatus status = MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    EXPECT_EQ(status, MessageCodecStatus::kOk);
    return payload;
  }

  bool RunOnLogicThread(std::function<void()> fn,
                        std::chrono::milliseconds timeout = 2s) {
    if (!logic_ || !logic_->io_context_ || !fn) {
      return false;
    }

    auto completed = std::make_shared<std::atomic<bool>>(false);
    asio::post(*logic_->io_context_,
               [fn = std::move(fn), completed]() mutable {
                 fn();
                 completed->store(true, std::memory_order_release);
               });

    return WaitForCondition(
        [completed]() { return completed->load(std::memory_order_acquire); },
        timeout,
        5ms,
        [this]() { PumpClient(); });
  }

  bool WaitForGatewayReadPaused(std::chrono::milliseconds timeout) {
    return WaitForCondition(
        [this]() {
          if (!gateway_ || !gateway_->network_) {
            return false;
          }
          for (const auto& session : gateway_->network_->GetAllSessions()) {
            if (session && session->IsReadPaused()) {
              return true;
            }
          }
          return false;
        },
        timeout,
        5ms,
        [this]() { PumpClient(); });
  }

  void PumpClient() {
    if (!client_) {
      return;
    }
    client_->update();
    while (true) {
      auto packet = client_->receive();
      if (!packet) {
        break;
      }
      pending_packets_.push_back(std::move(*packet));
    }
  }

  TestPorts ports_{};
  std::filesystem::path temp_dir_;
  std::string gateway_config_path_;
  std::string logic_config_path_;
  std::unique_ptr<GatewayServer> gateway_;
  std::unique_ptr<LogicServer> logic_;
  std::thread logic_thread_;
  std::unique_ptr<DualChannelClient> client_;
  std::deque<NetworkPacket> pending_packets_;
  GatewayIpcProbe ipc_probe_;
  std::atomic<uint32_t> scripted_trade_login_branch_count_{0};
  std::atomic<uint32_t> scripted_party_login_branch_count_{0};
};

class GatewayLogicUdsIntegrationTest : public GatewayLogicIntegrationTest {
 protected:
  std::string IpcTransport() const override { return "auto"; }
  std::string IpcUdsPath() const override {
    return (temp_dir_ / "gateway_logic.sock").string();
  }
};

TEST_F(GatewayLogicIntegrationTest, ForwardingAndHandshake) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      [this]() { client_->update(); }));

  const auto login_payload = BuildLoginPayload();
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());

  LoginResponse response;
  MessageCodecStatus status =
      mir2::common::DecodeLoginResponse(login_rsp->msg_id,
                                        login_rsp->payload,
                                        &response);
  EXPECT_EQ(status, MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_OK);

  auto protocol_version = WaitForSingleSessionProtocolVersion(2s);
  ASSERT_TRUE(protocol_version.has_value());
  EXPECT_EQ(*protocol_version, ProtocolVersion::kV2);
}

TEST_F(GatewayLogicIntegrationTest,
       QueueFullCriticalMessageUsesBackpressureWithoutLegacyFallback) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const auto login_payload =
      BuildLoginPayload(std::string(kPressureUserPrefix) + "990001");
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  LoginResponse login_response;
  const MessageCodecStatus login_status =
      mir2::common::DecodeLoginResponse(login_rsp->msg_id,
                                        login_rsp->payload,
                                        &login_response);
  ASSERT_EQ(login_status, MessageCodecStatus::kOk);
  ASSERT_EQ(login_response.code, mir2::proto::ErrorCode::ERR_OK);

  std::atomic<int> legacy_move_dispatch_count{0};
  ASSERT_TRUE(RunOnLogicThread([this, &legacy_move_dispatch_count]() {
    if (!logic_->hot_event_pipeline_) {
      logic_->hot_event_pipeline_ = std::make_unique<mir2::logic::events::HotEventPipeline>();
      logic_->hot_event_pipeline_->InitializeFromEnv();
    }
    logic_->hot_event_pipeline_->ForceEnable();

    // Intentionally set permissive legacy flags; queue-full path must still not use legacy dispatch.
    logic_->legacy_fallback_enabled_ = true;
    logic_->legacy_fallback_allow_auth_whitelist_ = true;
    logic_->legacy_fallback_allow_critical_msgs_ = true;
    logic_->legacy_fallback_allow_normal_msgs_ = true;
    logic_->queue_full_fallback_non_best_effort_enabled_ = true;

    logic_->handler_registry_->RegisterHandler(
        kMoveReqMsgId,
        [&legacy_move_dispatch_count](mir2::logic::HandlerContext,
                                      const uint8_t*,
                                      size_t) -> mir2::logic::Task<void> {
          legacy_move_dispatch_count.fetch_add(1, std::memory_order_relaxed);
          co_return;
        },
        "legacy_move_probe");
  }));

  std::atomic<bool> keep_filling{true};
  std::atomic<bool> queue_full_observed{false};
  std::thread fill_thread([this, &keep_filling, &queue_full_observed]() {
    mir2::logic::HandlerContext context;
    context.client_id = 0x9a110ca1ULL;
    while (keep_filling.load(std::memory_order_acquire)) {
      auto* pipeline = logic_ ? logic_->hot_event_pipeline_.get() : nullptr;
      if (!pipeline) {
        std::this_thread::sleep_for(1ms);
        continue;
      }

      const auto result = pipeline->TryEnqueue(context, kHeartbeatMsgId, nullptr, 0);
      if (result == mir2::logic::events::HotEventPipeline::EnqueueResult::kQueueFull) {
        queue_full_observed.store(true, std::memory_order_release);
        std::this_thread::sleep_for(200us);
        continue;
      }
      if (result == mir2::logic::events::HotEventPipeline::EnqueueResult::kBypass) {
        pipeline->ForceEnable();
      }
    }
  });
  struct FillThreadGuard {
    std::atomic<bool>* run = nullptr;
    std::thread* worker = nullptr;
    ~FillThreadGuard() {
      if (run) {
        run->store(false, std::memory_order_release);
      }
      if (worker && worker->joinable()) {
        worker->join();
      }
    }
  } fill_guard{&keep_filling, &fill_thread};

  ASSERT_TRUE(WaitForCondition(
      [&queue_full_observed]() {
        return queue_full_observed.load(std::memory_order_acquire);
      },
      3s,
      5ms,
      [this]() { PumpClient(); }));

  mir2::common::MoveRequest move_req{};
  move_req.target_x = 120;
  move_req.target_y = 121;
  MessageCodecStatus move_status = MessageCodecStatus::kOk;
  auto move_payload = mir2::common::EncodeMoveRequest(move_req, &move_status);
  ASSERT_EQ(move_status, MessageCodecStatus::kOk);
  ASSERT_FALSE(move_payload.empty());
  client_->send(kMoveReqMsgId, move_payload);

  EXPECT_TRUE(WaitForGatewayReadPaused(3s));
  std::this_thread::sleep_for(200ms);
  PumpClient();

  EXPECT_EQ(legacy_move_dispatch_count.load(std::memory_order_relaxed), 0);
}

TEST_F(GatewayLogicIntegrationTest, LegacyV1ToggleFallsBackToV2Protocol) {
  ASSERT_NE(client_, nullptr);
  client_->set_use_v2_protocol(false);

  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const auto login_payload = BuildLoginPayload();
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());

  LoginResponse response;
  MessageCodecStatus status =
      mir2::common::DecodeLoginResponse(login_rsp->msg_id,
                                        login_rsp->payload,
                                        &response);
  EXPECT_EQ(status, MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_OK);

  auto protocol_version = WaitForSingleSessionProtocolVersion(2s);
  ASSERT_TRUE(protocol_version.has_value());
  EXPECT_EQ(*protocol_version, ProtocolVersion::kV2);
}

TEST_F(GatewayLogicIntegrationTest, MixedLegacyToggleAndV2ClientsUseV2Protocol) {
  ASSERT_NE(client_, nullptr);
  client_->set_use_v2_protocol(true);

  auto v1_client = std::make_unique<DualChannelClient>();
  ASSERT_NE(v1_client, nullptr);
  v1_client->set_use_v2_protocol(false);

  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(ConnectClient(v1_client.get()));
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const auto login_payload = BuildLoginPayload();
  ASSERT_FALSE(login_payload.empty());

  client_->send(kLoginReqMsgId, login_payload);
  v1_client->send(kLoginReqMsgId, login_payload);

  auto wait_packet = [](DualChannelClient* client,
                        uint16_t msg_id,
                        std::chrono::milliseconds timeout)
      -> std::optional<NetworkPacket> {
    if (!client) {
      return std::nullopt;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      client->update();
      while (true) {
        auto packet = client->receive();
        if (!packet.has_value()) {
          break;
        }
        if (packet->msg_id == msg_id) {
          return packet;
        }
      }
      std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
  };

  auto v2_login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(v2_login_rsp.has_value());
  auto v1_login_rsp = wait_packet(v1_client.get(), kLoginRspMsgId, 3s);
  ASSERT_TRUE(v1_login_rsp.has_value());

  LoginResponse v2_response;
  MessageCodecStatus v2_status =
      mir2::common::DecodeLoginResponse(v2_login_rsp->msg_id,
                                        v2_login_rsp->payload,
                                        &v2_response);
  EXPECT_EQ(v2_status, MessageCodecStatus::kOk);
  EXPECT_EQ(v2_response.code, mir2::proto::ErrorCode::ERR_OK);

  LoginResponse v1_response;
  MessageCodecStatus v1_status =
      mir2::common::DecodeLoginResponse(v1_login_rsp->msg_id,
                                        v1_login_rsp->payload,
                                        &v1_response);
  EXPECT_EQ(v1_status, MessageCodecStatus::kOk);
  EXPECT_EQ(v1_response.code, mir2::proto::ErrorCode::ERR_OK);

  ASSERT_TRUE(WaitForCondition(
      [this]() {
        if (!gateway_ || !gateway_->network_) {
          return false;
        }
        const auto sessions = gateway_->network_->GetAllSessions();
        if (sessions.size() < 2) {
          return false;
        }

        size_t v2_count = 0;
        for (const auto& session : sessions) {
          if (!session) {
            continue;
          }
          if (session->GetProtocolVersion() == ProtocolVersion::kV2) {
            ++v2_count;
          }
        }
        return v2_count >= 2;
      },
      3s,
      10ms,
      [this, &v1_client]() {
        PumpClient();
        if (v1_client) {
          v1_client->update();
        }
      }));
}

TEST_F(GatewayLogicIntegrationTest, BuffAddForwardingEndToEnd) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const auto login_payload = BuildLoginPayload(kEmitBuffUser);
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  auto buff_add = WaitForPacket(kBuffAddMsgId, 3s);
  ASSERT_TRUE(buff_add.has_value());

  flatbuffers::Verifier verifier(buff_add->payload.data(), buff_add->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::BuffAdd>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::BuffAdd>(buff_add->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), kE2EPlayerId);
  EXPECT_EQ(payload->entity_type(), mir2::proto::EntityType::PLAYER);
  EXPECT_EQ(payload->buff_id(), kE2EBuffId);
  EXPECT_EQ(payload->duration_ms(), kE2EBuffDurationMs);
  EXPECT_EQ(payload->stack_count(), kE2EBuffStackCount);
}

TEST_F(GatewayLogicIntegrationTest, RespawnForwardingEndToEnd) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const auto login_payload = BuildLoginPayload(kEmitRespawnUser);
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  auto respawn = WaitForPacket(kRespawnMsgId, 3s);
  ASSERT_TRUE(respawn.has_value());

  flatbuffers::Verifier verifier(respawn->payload.data(), respawn->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::Respawn>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::Respawn>(respawn->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), kE2EPlayerId);
  EXPECT_EQ(payload->entity_type(), mir2::proto::EntityType::PLAYER);
  EXPECT_EQ(payload->x(), kE2ERespawnX);
  EXPECT_EQ(payload->y(), kE2ERespawnY);
  EXPECT_EQ(payload->hp(), kE2ERespawnHp);
  EXPECT_EQ(payload->mp(), kE2ERespawnMp);
}

TEST_F(GatewayLogicIntegrationTest, StateSyncForwardingEndToEnd) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const auto login_payload = BuildLoginPayload(kEmitStateSyncUser);
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  auto state_sync = WaitForPacket(kStateSyncMsgId, 3s);
  ASSERT_TRUE(state_sync.has_value());

  flatbuffers::Verifier verifier(state_sync->payload.data(), state_sync->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::StateSync>(state_sync->payload.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->player(), nullptr);
  EXPECT_EQ(payload->player()->id(), kE2EPlayerId);
  EXPECT_EQ(payload->player()->map_id(), kE2EMapId);
  EXPECT_EQ(payload->player()->x(), kE2EStateSyncX);
  EXPECT_EQ(payload->player()->y(), kE2EStateSyncY);
  ASSERT_NE(payload->entities(), nullptr);
  ASSERT_EQ(payload->entities()->size(), 1u);
  EXPECT_EQ(payload->entities()->Get(0)->entity_id(), kE2ESnapshotEntityId);
}

TEST_F(GatewayLogicIntegrationTest, TradeForwardingRoundTripEndToEnd) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      [this]() { PumpClient(); }));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  const auto login_payload = BuildLoginPayload(kEmitTradeUser);
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  ASSERT_TRUE(WaitForCondition(
      [this]() {
        return scripted_trade_login_branch_count_.load(std::memory_order_relaxed) >= 1;
      },
      3s,
      10ms,
      [this]() { PumpClient(); }));

  auto trade_rsp = WaitForPacket(kTradeRspMsgId, 3s);
  ASSERT_TRUE(trade_rsp.has_value());
  flatbuffers::Verifier trade_rsp_verifier(trade_rsp->payload.data(),
                                           trade_rsp->payload.size());
  ASSERT_TRUE(trade_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* trade_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_rsp->payload.data());
  ASSERT_NE(trade_rsp_payload, nullptr);
  EXPECT_TRUE(trade_rsp_payload->success());
  EXPECT_EQ(trade_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  EXPECT_EQ(trade_rsp_payload->trade_id(), kE2ETradeId);

  auto trade_update = WaitForPacket(kTradeUpdateMsgId, 3s);
  ASSERT_TRUE(trade_update.has_value());
  flatbuffers::Verifier trade_update_verifier(trade_update->payload.data(),
                                              trade_update->payload.size());
  ASSERT_TRUE(trade_update_verifier.VerifyBuffer<mir2::proto::TradeUpdate>(nullptr));
  const auto* trade_update_payload =
      flatbuffers::GetRoot<mir2::proto::TradeUpdate>(trade_update->payload.data());
  ASSERT_NE(trade_update_payload, nullptr);
  EXPECT_EQ(trade_update_payload->trade_id(), kE2ETradeId);
  EXPECT_EQ(trade_update_payload->left_character_id(),
            static_cast<uint32_t>(kE2EPlayerId));
  EXPECT_EQ(trade_update_payload->right_character_id(), kE2ETradePartnerCharacterId);
  EXPECT_EQ(trade_update_payload->left_gold(), kE2ETradeLeftGold);
  EXPECT_EQ(trade_update_payload->right_gold(), kE2ETradeRightGold);
  EXPECT_TRUE(trade_update_payload->left_confirmed());
  EXPECT_FALSE(trade_update_payload->right_confirmed());
  ASSERT_NE(trade_update_payload->left_items(), nullptr);
  ASSERT_EQ(trade_update_payload->left_items()->size(), 1u);
  EXPECT_EQ(trade_update_payload->left_items()->Get(0)->inventory_slot(),
            kE2ETradeLeftItemSlot);
  EXPECT_EQ(trade_update_payload->left_items()->Get(0)->item_id(),
            kE2ETradeLeftItemId);
  EXPECT_EQ(trade_update_payload->left_items()->Get(0)->count(),
            kE2ETradeLeftItemCount);

  auto trade_complete = WaitForPacket(kTradeCompleteMsgId, 3s);
  ASSERT_TRUE(trade_complete.has_value());
  flatbuffers::Verifier trade_complete_verifier(trade_complete->payload.data(),
                                                trade_complete->payload.size());
  ASSERT_TRUE(trade_complete_verifier.VerifyBuffer<mir2::proto::TradeComplete>(nullptr));
  const auto* trade_complete_payload =
      flatbuffers::GetRoot<mir2::proto::TradeComplete>(trade_complete->payload.data());
  ASSERT_NE(trade_complete_payload, nullptr);
  EXPECT_EQ(trade_complete_payload->trade_id(), kE2ETradeId);
  EXPECT_TRUE(trade_complete_payload->success());
  EXPECT_EQ(trade_complete_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
}

TEST_F(GatewayLogicIntegrationTest, PartyInviteForwardingRoundTripEndToEnd) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      [this]() { PumpClient(); }));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  const auto login_payload = BuildLoginPayload(kEmitPartyUser);
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  ASSERT_TRUE(WaitForCondition(
      [this]() {
        return scripted_party_login_branch_count_.load(std::memory_order_relaxed) >= 1;
      },
      3s,
      10ms,
      [this]() { PumpClient(); }));

  auto invite_rsp = WaitForPacket(kPartyInviteRspMsgId, 3s);
  ASSERT_TRUE(invite_rsp.has_value());
  flatbuffers::Verifier invite_rsp_verifier(invite_rsp->payload.data(),
                                            invite_rsp->payload.size());
  ASSERT_TRUE(invite_rsp_verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr));
  const auto* invite_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::PartyInviteRsp>(invite_rsp->payload.data());
  ASSERT_NE(invite_rsp_payload, nullptr);
  EXPECT_TRUE(invite_rsp_payload->success());
  EXPECT_EQ(invite_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));

  auto party_update = WaitForPacket(kPartyUpdateMsgId, 3s);
  ASSERT_TRUE(party_update.has_value());
  flatbuffers::Verifier party_update_verifier(party_update->payload.data(),
                                              party_update->payload.size());
  ASSERT_TRUE(party_update_verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr));
  const auto* party_update_payload =
      flatbuffers::GetRoot<mir2::proto::PartyUpdate>(party_update->payload.data());
  ASSERT_NE(party_update_payload, nullptr);
  EXPECT_EQ(party_update_payload->party_id(), kE2EPartyId);
  EXPECT_EQ(party_update_payload->leader_character_id(),
            static_cast<uint32_t>(kE2EPlayerId));
  ASSERT_NE(party_update_payload->members(), nullptr);
  ASSERT_EQ(party_update_payload->members()->size(), 2u);
  EXPECT_EQ(party_update_payload->members()->Get(0)->character_id(),
            static_cast<uint32_t>(kE2EPlayerId));
  EXPECT_EQ(party_update_payload->members()->Get(1)->character_id(),
            kE2EPartyMemberCharacterId);
}

TEST_F(GatewayLogicIntegrationTest, TradeRoundTripRealHandlerEndToEnd) {
  auto peer_client = std::make_unique<DualChannelClient>();
  ASSERT_NE(peer_client, nullptr);
  std::deque<NetworkPacket> peer_packets;
  auto pump_both = [this, &peer_client, &peer_packets]() {
    PumpClient();
    if (!peer_client) {
      return;
    }
    peer_client->update();
    while (true) {
      auto packet = peer_client->receive();
      if (!packet.has_value()) {
        break;
      }
      peer_packets.push_back(std::move(*packet));
    }
  };
  auto wait_peer_packet =
      [&peer_packets, &pump_both](uint16_t msg_id,
                                  std::chrono::milliseconds timeout)
          -> std::optional<NetworkPacket> {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      pump_both();
      auto it = std::find_if(
          peer_packets.begin(),
          peer_packets.end(),
          [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
      if (it != peer_packets.end()) {
        NetworkPacket packet = std::move(*it);
        peer_packets.erase(it);
        return packet;
      }
      std::this_thread::sleep_for(2ms);
    }

    pump_both();
    auto it = std::find_if(
        peer_packets.begin(),
        peer_packets.end(),
        [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
    if (it == peer_packets.end()) {
      return std::nullopt;
    }
    NetworkPacket packet = std::move(*it);
    peer_packets.erase(it);
    return packet;
  };

  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(ConnectClient(peer_client.get()));
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      pump_both));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  const auto login_payload = BuildLoginPayload(kRealTradeUser);
  const auto peer_login_payload = BuildLoginPayload(kRealTradePeerUser);
  ASSERT_FALSE(login_payload.empty());
  ASSERT_FALSE(peer_login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);
  peer_client->send(kLoginReqMsgId, peer_login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  auto peer_login_rsp = wait_peer_packet(kLoginRspMsgId, 3s);
  ASSERT_TRUE(peer_login_rsp.has_value());
  pending_packets_.clear();
  peer_packets.clear();

  const auto trade_req_payload = BuildTradeReqPayload(kRealTradeTargetCharacterId);
  ASSERT_FALSE(trade_req_payload.empty());
  client_->send(kTradeReqMsgId, trade_req_payload);

  auto trade_rsp = WaitForPacket(kTradeRspMsgId, 3s);
  ASSERT_TRUE(trade_rsp.has_value());
  flatbuffers::Verifier trade_rsp_verifier(trade_rsp->payload.data(),
                                           trade_rsp->payload.size());
  ASSERT_TRUE(trade_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* trade_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_rsp->payload.data());
  ASSERT_NE(trade_rsp_payload, nullptr);
  EXPECT_TRUE(trade_rsp_payload->success());
  EXPECT_EQ(trade_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  const uint64_t expected_trade_id = trade_rsp_payload->trade_id();
  EXPECT_NE(expected_trade_id, 0u);

  auto peer_trade_rsp = wait_peer_packet(kTradeRspMsgId, 3s);
  ASSERT_TRUE(peer_trade_rsp.has_value());
  flatbuffers::Verifier peer_trade_rsp_verifier(peer_trade_rsp->payload.data(),
                                                peer_trade_rsp->payload.size());
  ASSERT_TRUE(peer_trade_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* peer_trade_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(peer_trade_rsp->payload.data());
  ASSERT_NE(peer_trade_rsp_payload, nullptr);
  EXPECT_TRUE(peer_trade_rsp_payload->success());
  EXPECT_EQ(peer_trade_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  EXPECT_EQ(peer_trade_rsp_payload->trade_id(), expected_trade_id);

  const auto trade_accept_payload = BuildTradeReqPayload(kRealTradeRequesterCharacterId);
  ASSERT_FALSE(trade_accept_payload.empty());
  peer_client->send(kTradeReqMsgId, trade_accept_payload);

  auto trade_accept_rsp = WaitForPacket(kTradeRspMsgId, 3s);
  ASSERT_TRUE(trade_accept_rsp.has_value());
  flatbuffers::Verifier trade_accept_rsp_verifier(trade_accept_rsp->payload.data(),
                                                  trade_accept_rsp->payload.size());
  ASSERT_TRUE(trade_accept_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* trade_accept_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_accept_rsp->payload.data());
  ASSERT_NE(trade_accept_rsp_payload, nullptr);
  EXPECT_TRUE(trade_accept_rsp_payload->success());
  EXPECT_EQ(trade_accept_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  EXPECT_EQ(trade_accept_rsp_payload->trade_id(), expected_trade_id);

  auto peer_trade_accept_rsp = wait_peer_packet(kTradeRspMsgId, 3s);
  ASSERT_TRUE(peer_trade_accept_rsp.has_value());
  flatbuffers::Verifier peer_trade_accept_rsp_verifier(
      peer_trade_accept_rsp->payload.data(), peer_trade_accept_rsp->payload.size());
  ASSERT_TRUE(peer_trade_accept_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* peer_trade_accept_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(peer_trade_accept_rsp->payload.data());
  ASSERT_NE(peer_trade_accept_rsp_payload, nullptr);
  EXPECT_TRUE(peer_trade_accept_rsp_payload->success());
  EXPECT_EQ(peer_trade_accept_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  EXPECT_EQ(peer_trade_accept_rsp_payload->trade_id(), expected_trade_id);

  auto trade_update = WaitForPacket(kTradeUpdateMsgId, 3s);
  ASSERT_TRUE(trade_update.has_value());
  flatbuffers::Verifier trade_update_verifier(trade_update->payload.data(),
                                              trade_update->payload.size());
  ASSERT_TRUE(trade_update_verifier.VerifyBuffer<mir2::proto::TradeUpdate>(nullptr));
  const auto* trade_update_payload =
      flatbuffers::GetRoot<mir2::proto::TradeUpdate>(trade_update->payload.data());
  ASSERT_NE(trade_update_payload, nullptr);
  EXPECT_EQ(trade_update_payload->trade_id(), expected_trade_id);
  const bool requester_left =
      trade_update_payload->left_character_id() == kRealTradeRequesterCharacterId &&
      trade_update_payload->right_character_id() == kRealTradeTargetCharacterId;
  const bool requester_right =
      trade_update_payload->left_character_id() == kRealTradeTargetCharacterId &&
      trade_update_payload->right_character_id() == kRealTradeRequesterCharacterId;
  EXPECT_TRUE(requester_left || requester_right);
  EXPECT_EQ(trade_update_payload->left_gold(), 0u);
  EXPECT_EQ(trade_update_payload->right_gold(), 0u);
  EXPECT_FALSE(trade_update_payload->left_confirmed());
  EXPECT_FALSE(trade_update_payload->right_confirmed());

  auto peer_trade_update = wait_peer_packet(kTradeUpdateMsgId, 3s);
  ASSERT_TRUE(peer_trade_update.has_value());
  flatbuffers::Verifier peer_trade_update_verifier(peer_trade_update->payload.data(),
                                                   peer_trade_update->payload.size());
  ASSERT_TRUE(peer_trade_update_verifier.VerifyBuffer<mir2::proto::TradeUpdate>(nullptr));
  const auto* peer_trade_update_payload =
      flatbuffers::GetRoot<mir2::proto::TradeUpdate>(peer_trade_update->payload.data());
  ASSERT_NE(peer_trade_update_payload, nullptr);
  EXPECT_EQ(peer_trade_update_payload->trade_id(), expected_trade_id);
  const bool peer_requester_left =
      peer_trade_update_payload->left_character_id() == kRealTradeRequesterCharacterId &&
      peer_trade_update_payload->right_character_id() == kRealTradeTargetCharacterId;
  const bool peer_requester_right =
      peer_trade_update_payload->left_character_id() == kRealTradeTargetCharacterId &&
      peer_trade_update_payload->right_character_id() == kRealTradeRequesterCharacterId;
  EXPECT_TRUE(peer_requester_left || peer_requester_right);
  EXPECT_EQ(peer_trade_update_payload->left_gold(), 0u);
  EXPECT_EQ(peer_trade_update_payload->right_gold(), 0u);
  EXPECT_FALSE(peer_trade_update_payload->left_confirmed());
  EXPECT_FALSE(peer_trade_update_payload->right_confirmed());
}

TEST_F(GatewayLogicIntegrationTest, PartyInviteRoundTripRealHandlerEndToEnd) {
  auto peer_client = std::make_unique<DualChannelClient>();
  ASSERT_NE(peer_client, nullptr);
  std::deque<NetworkPacket> peer_packets;
  auto pump_both = [this, &peer_client, &peer_packets]() {
    PumpClient();
    if (!peer_client) {
      return;
    }
    peer_client->update();
    while (true) {
      auto packet = peer_client->receive();
      if (!packet.has_value()) {
        break;
      }
      peer_packets.push_back(std::move(*packet));
    }
  };
  auto wait_peer_packet =
      [&peer_packets, &pump_both](uint16_t msg_id,
                                  std::chrono::milliseconds timeout)
          -> std::optional<NetworkPacket> {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      pump_both();
      auto it = std::find_if(
          peer_packets.begin(),
          peer_packets.end(),
          [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
      if (it != peer_packets.end()) {
        NetworkPacket packet = std::move(*it);
        peer_packets.erase(it);
        return packet;
      }
      std::this_thread::sleep_for(2ms);
    }

    pump_both();
    auto it = std::find_if(
        peer_packets.begin(),
        peer_packets.end(),
        [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
    if (it == peer_packets.end()) {
      return std::nullopt;
    }
    NetworkPacket packet = std::move(*it);
    peer_packets.erase(it);
    return packet;
  };

  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(ConnectClient(peer_client.get()));
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      pump_both));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  const auto login_payload = BuildLoginPayload(kRealPartyUser);
  const auto peer_login_payload = BuildLoginPayload(kRealPartyPeerUser);
  ASSERT_FALSE(login_payload.empty());
  ASSERT_FALSE(peer_login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);
  peer_client->send(kLoginReqMsgId, peer_login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  auto peer_login_rsp = wait_peer_packet(kLoginRspMsgId, 3s);
  ASSERT_TRUE(peer_login_rsp.has_value());
  pending_packets_.clear();
  peer_packets.clear();

  const auto invite_req_payload = BuildPartyInviteReqPayload(kRealPartyTargetCharacterId);
  ASSERT_FALSE(invite_req_payload.empty());
  client_->send(kPartyInviteReqMsgId, invite_req_payload);

  auto invite_rsp = WaitForPacket(kPartyInviteRspMsgId, 3s);
  ASSERT_TRUE(invite_rsp.has_value());
  flatbuffers::Verifier invite_rsp_verifier(invite_rsp->payload.data(),
                                            invite_rsp->payload.size());
  ASSERT_TRUE(invite_rsp_verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr));
  const auto* invite_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::PartyInviteRsp>(invite_rsp->payload.data());
  ASSERT_NE(invite_rsp_payload, nullptr);
  EXPECT_TRUE(invite_rsp_payload->success());
  EXPECT_EQ(invite_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));

  auto peer_invite_rsp = wait_peer_packet(kPartyInviteRspMsgId, 3s);
  ASSERT_TRUE(peer_invite_rsp.has_value());
  flatbuffers::Verifier peer_invite_rsp_verifier(peer_invite_rsp->payload.data(),
                                                 peer_invite_rsp->payload.size());
  ASSERT_TRUE(peer_invite_rsp_verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr));
  const auto* peer_invite_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::PartyInviteRsp>(peer_invite_rsp->payload.data());
  ASSERT_NE(peer_invite_rsp_payload, nullptr);
  EXPECT_TRUE(peer_invite_rsp_payload->success());
  EXPECT_EQ(peer_invite_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));

  auto party_update = WaitForPacket(kPartyUpdateMsgId, 3s);
  ASSERT_TRUE(party_update.has_value());
  flatbuffers::Verifier party_update_verifier(party_update->payload.data(),
                                              party_update->payload.size());
  ASSERT_TRUE(party_update_verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr));
  const auto* party_update_payload =
      flatbuffers::GetRoot<mir2::proto::PartyUpdate>(party_update->payload.data());
  ASSERT_NE(party_update_payload, nullptr);
  EXPECT_GT(party_update_payload->party_id(), 0u);
  EXPECT_EQ(party_update_payload->leader_character_id(), kRealPartyLeaderCharacterId);

  ASSERT_NE(party_update_payload->members(), nullptr);
  ASSERT_EQ(party_update_payload->members()->size(), 2u);
  bool saw_leader = false;
  bool saw_target = false;
  for (const auto* member : *party_update_payload->members()) {
    if (!member) {
      continue;
    }
    if (member->character_id() == kRealPartyLeaderCharacterId) {
      saw_leader = true;
    } else if (member->character_id() == kRealPartyTargetCharacterId) {
      saw_target = true;
    }
  }
  EXPECT_TRUE(saw_leader);
  EXPECT_TRUE(saw_target);

  auto peer_party_update = wait_peer_packet(kPartyUpdateMsgId, 3s);
  ASSERT_TRUE(peer_party_update.has_value());
  flatbuffers::Verifier peer_party_update_verifier(peer_party_update->payload.data(),
                                                   peer_party_update->payload.size());
  ASSERT_TRUE(peer_party_update_verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr));
  const auto* peer_party_update_payload =
      flatbuffers::GetRoot<mir2::proto::PartyUpdate>(peer_party_update->payload.data());
  ASSERT_NE(peer_party_update_payload, nullptr);
  EXPECT_EQ(peer_party_update_payload->party_id(), party_update_payload->party_id());
  EXPECT_EQ(peer_party_update_payload->leader_character_id(),
            kRealPartyLeaderCharacterId);
  ASSERT_NE(peer_party_update_payload->members(), nullptr);
  ASSERT_EQ(peer_party_update_payload->members()->size(), 2u);
  bool peer_saw_leader = false;
  bool peer_saw_target = false;
  for (const auto* member : *peer_party_update_payload->members()) {
    if (!member) {
      continue;
    }
    if (member->character_id() == kRealPartyLeaderCharacterId) {
      peer_saw_leader = true;
    } else if (member->character_id() == kRealPartyTargetCharacterId) {
      peer_saw_target = true;
    }
  }
  EXPECT_TRUE(peer_saw_leader);
  EXPECT_TRUE(peer_saw_target);
}

TEST_F(GatewayLogicIntegrationTest, AuctionRoundTripRealHandlerEndToEnd) {
  auto peer_client = std::make_unique<DualChannelClient>();
  ASSERT_NE(peer_client, nullptr);
  std::deque<NetworkPacket> peer_packets;
  auto pump_both = [this, &peer_client, &peer_packets]() {
    PumpClient();
    if (!peer_client) {
      return;
    }
    peer_client->update();
    while (true) {
      auto packet = peer_client->receive();
      if (!packet.has_value()) {
        break;
      }
      peer_packets.push_back(std::move(*packet));
    }
  };
  auto wait_peer_packet =
      [&peer_packets, &pump_both](uint16_t msg_id,
                                  std::chrono::milliseconds timeout)
          -> std::optional<NetworkPacket> {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      pump_both();
      auto it = std::find_if(
          peer_packets.begin(),
          peer_packets.end(),
          [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
      if (it != peer_packets.end()) {
        NetworkPacket packet = std::move(*it);
        peer_packets.erase(it);
        return packet;
      }
      std::this_thread::sleep_for(2ms);
    }

    pump_both();
    auto it = std::find_if(
        peer_packets.begin(),
        peer_packets.end(),
        [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
    if (it == peer_packets.end()) {
      return std::nullopt;
    }
    NetworkPacket packet = std::move(*it);
    peer_packets.erase(it);
    return packet;
  };

  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(ConnectClient(peer_client.get()));
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      pump_both));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  const auto login_payload = BuildLoginPayload(kRealAuctionUser);
  const auto peer_login_payload = BuildLoginPayload(kRealAuctionPeerUser);
  ASSERT_FALSE(login_payload.empty());
  ASSERT_FALSE(peer_login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);
  peer_client->send(kLoginReqMsgId, peer_login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  auto peer_login_rsp = wait_peer_packet(kLoginRspMsgId, 3s);
  ASSERT_TRUE(peer_login_rsp.has_value());
  pending_packets_.clear();
  peer_packets.clear();

  const auto sell_req_payload = BuildAuctionSellReqPayload(kRealAuctionInventorySlot,
                                                           kRealAuctionItemId,
                                                           kRealAuctionSellCount,
                                                           kRealAuctionUnitPrice,
                                                           kRealAuctionDurationSec);
  ASSERT_FALSE(sell_req_payload.empty());
  client_->send(kAuctionSellReqMsgId, sell_req_payload);

  auto sell_rsp = WaitForPacket(kAuctionSellRspMsgId, 3s);
  ASSERT_TRUE(sell_rsp.has_value());
  flatbuffers::Verifier sell_rsp_verifier(sell_rsp->payload.data(),
                                          sell_rsp->payload.size());
  ASSERT_TRUE(sell_rsp_verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr));
  const auto* sell_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::AuctionSellRsp>(sell_rsp->payload.data());
  ASSERT_NE(sell_rsp_payload, nullptr);
  EXPECT_TRUE(sell_rsp_payload->success());
  EXPECT_EQ(sell_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  const uint64_t listing_id = sell_rsp_payload->listing_id();
  EXPECT_NE(listing_id, 0u);

  auto seller_listed_notify = WaitForPacket(kAuctionNotifyMsgId, 3s);
  ASSERT_TRUE(seller_listed_notify.has_value());
  flatbuffers::Verifier seller_listed_notify_verifier(
      seller_listed_notify->payload.data(), seller_listed_notify->payload.size());
  ASSERT_TRUE(
      seller_listed_notify_verifier.VerifyBuffer<mir2::proto::AuctionNotify>(nullptr));
  const auto* seller_listed_notify_payload =
      flatbuffers::GetRoot<mir2::proto::AuctionNotify>(seller_listed_notify->payload.data());
  ASSERT_NE(seller_listed_notify_payload, nullptr);
  ASSERT_NE(seller_listed_notify_payload->listing(), nullptr);
  EXPECT_EQ(seller_listed_notify_payload->notify_type(),
            mir2::proto::AuctionNotifyType::LISTED);
  EXPECT_EQ(seller_listed_notify_payload->listing()->listing_id(), listing_id);

  const auto buy_req_payload = BuildAuctionBuyReqPayload(listing_id);
  ASSERT_FALSE(buy_req_payload.empty());
  peer_client->send(kAuctionBuyReqMsgId, buy_req_payload);

  auto buyer_buy_rsp = wait_peer_packet(kAuctionBuyRspMsgId, 3s);
  ASSERT_TRUE(buyer_buy_rsp.has_value());
  flatbuffers::Verifier buyer_buy_rsp_verifier(buyer_buy_rsp->payload.data(),
                                               buyer_buy_rsp->payload.size());
  ASSERT_TRUE(buyer_buy_rsp_verifier.VerifyBuffer<mir2::proto::AuctionBuyRsp>(nullptr));
  const auto* buyer_buy_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::AuctionBuyRsp>(buyer_buy_rsp->payload.data());
  ASSERT_NE(buyer_buy_rsp_payload, nullptr);
  EXPECT_TRUE(buyer_buy_rsp_payload->success());
  EXPECT_EQ(buyer_buy_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  EXPECT_EQ(buyer_buy_rsp_payload->listing_id(), listing_id);

  auto buyer_bought_notify = wait_peer_packet(kAuctionNotifyMsgId, 3s);
  ASSERT_TRUE(buyer_bought_notify.has_value());
  flatbuffers::Verifier buyer_bought_notify_verifier(
      buyer_bought_notify->payload.data(), buyer_bought_notify->payload.size());
  ASSERT_TRUE(
      buyer_bought_notify_verifier.VerifyBuffer<mir2::proto::AuctionNotify>(nullptr));
  const auto* buyer_bought_notify_payload =
      flatbuffers::GetRoot<mir2::proto::AuctionNotify>(buyer_bought_notify->payload.data());
  ASSERT_NE(buyer_bought_notify_payload, nullptr);
  ASSERT_NE(buyer_bought_notify_payload->listing(), nullptr);
  EXPECT_EQ(buyer_bought_notify_payload->notify_type(),
            mir2::proto::AuctionNotifyType::BOUGHT);
  EXPECT_EQ(buyer_bought_notify_payload->listing()->listing_id(), listing_id);
  EXPECT_TRUE(buyer_bought_notify_payload->listing()->sold());

  auto seller_sold_notify = WaitForPacket(kAuctionNotifyMsgId, 3s);
  ASSERT_TRUE(seller_sold_notify.has_value());
  flatbuffers::Verifier seller_sold_notify_verifier(
      seller_sold_notify->payload.data(), seller_sold_notify->payload.size());
  ASSERT_TRUE(seller_sold_notify_verifier.VerifyBuffer<mir2::proto::AuctionNotify>(nullptr));
  const auto* seller_sold_notify_payload =
      flatbuffers::GetRoot<mir2::proto::AuctionNotify>(seller_sold_notify->payload.data());
  ASSERT_NE(seller_sold_notify_payload, nullptr);
  ASSERT_NE(seller_sold_notify_payload->listing(), nullptr);
  EXPECT_EQ(seller_sold_notify_payload->notify_type(),
            mir2::proto::AuctionNotifyType::SOLD);
  EXPECT_EQ(seller_sold_notify_payload->listing()->listing_id(), listing_id);
  EXPECT_TRUE(seller_sold_notify_payload->listing()->sold());

  const auto list_req_payload = BuildAuctionListReqPayload(1, 20, false);
  ASSERT_FALSE(list_req_payload.empty());
  peer_client->send(kAuctionListReqMsgId, list_req_payload);

  auto buyer_list_rsp = wait_peer_packet(kAuctionListRspMsgId, 3s);
  ASSERT_TRUE(buyer_list_rsp.has_value());
  flatbuffers::Verifier buyer_list_rsp_verifier(buyer_list_rsp->payload.data(),
                                                buyer_list_rsp->payload.size());
  ASSERT_TRUE(buyer_list_rsp_verifier.VerifyBuffer<mir2::proto::AuctionListRsp>(nullptr));
  const auto* buyer_list_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::AuctionListRsp>(buyer_list_rsp->payload.data());
  ASSERT_NE(buyer_list_rsp_payload, nullptr);
  EXPECT_TRUE(buyer_list_rsp_payload->success());
  EXPECT_EQ(buyer_list_rsp_payload->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
  EXPECT_EQ(buyer_list_rsp_payload->total_count(), 0u);
  ASSERT_NE(buyer_list_rsp_payload->listings(), nullptr);
  EXPECT_EQ(buyer_list_rsp_payload->listings()->size(), 0u);
}

TEST_F(GatewayLogicIntegrationTest, GuildCreateRoundTripRealHandlerEndToEnd) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      [this]() { PumpClient(); }));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  const auto login_payload = BuildLoginPayload(kRealGuildUser);
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
  pending_packets_.clear();

  const auto guild_name = BuildUniqueGuildName(/*seed=*/1);
  const auto create_req_payload = BuildGuildCreateReqPayload(guild_name);
  ASSERT_FALSE(create_req_payload.empty());
  client_->send(kGuildCreateReqMsgId, create_req_payload);

  auto create_rsp = WaitForPacket(kGuildCreateRspMsgId, 3s);
  ASSERT_TRUE(create_rsp.has_value());
  flatbuffers::Verifier create_rsp_verifier(create_rsp->payload.data(),
                                            create_rsp->payload.size());
  ASSERT_TRUE(create_rsp_verifier.VerifyBuffer<mir2::proto::CreateGuildResponse>(nullptr));
  const auto* create_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::CreateGuildResponse>(create_rsp->payload.data());
  ASSERT_NE(create_rsp_payload, nullptr);
  EXPECT_TRUE(create_rsp_payload->success());
  const auto* create_guild_info = create_rsp_payload->guild_info();
  ASSERT_NE(create_guild_info, nullptr);
  const uint64_t guild_id = create_guild_info->id();
  EXPECT_GT(guild_id, 0u);

  auto sync_rsp = WaitForPacket(kGuildInfoSyncMsgId, 3s);
  ASSERT_TRUE(sync_rsp.has_value());
  flatbuffers::Verifier sync_rsp_verifier(sync_rsp->payload.data(),
                                          sync_rsp->payload.size());
  ASSERT_TRUE(sync_rsp_verifier.VerifyBuffer<mir2::proto::GuildInfoSync>(nullptr));
  const auto* sync_rsp_payload =
      flatbuffers::GetRoot<mir2::proto::GuildInfoSync>(sync_rsp->payload.data());
  ASSERT_NE(sync_rsp_payload, nullptr);
  ASSERT_NE(sync_rsp_payload->guild_info(), nullptr);
  ASSERT_NE(sync_rsp_payload->guild_info()->name(), nullptr);
  EXPECT_EQ(sync_rsp_payload->guild_info()->id(), guild_id);
  EXPECT_EQ(sync_rsp_payload->guild_info()->name()->str(), guild_name);
}

TEST_F(GatewayLogicIntegrationTest, PerformanceTargets) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  InstallGatewayIpcProbe();

  PerformanceMonitor ipc_monitor;
  ipc_monitor.Reset();
  const size_t ipc_samples = 30;

  for (size_t i = 0; i < ipc_samples; ++i) {
    flatbuffers::FlatBufferBuilder builder;
    const auto hb = mir2::proto::CreateHeartbeat(
        builder, static_cast<uint32_t>(i), static_cast<uint32_t>(mir2::common::now_ms()));
    builder.Finish(hb);
    const uint8_t* data = builder.GetBufferPointer();
    std::vector<uint8_t> payload(data, data + builder.GetSize());

    ipc_probe_.Reset();
    const auto start = std::chrono::steady_clock::now();
    gateway_->logic_client_->Send(kHeartbeatMsgId, payload);

    auto response_time = ipc_probe_.WaitForResponse(500ms);
    ASSERT_TRUE(response_time.has_value());

    ipc_monitor.RecordLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(
        *response_time - start));
    std::this_thread::sleep_for(2ms);
  }

  const auto ipc_stats = ipc_monitor.GetLatencyStats();
  // IPC p50 can jitter slightly above 1ms on shared CI/WSL environments.
  EXPECT_LT(ipc_stats.p50_ms, 1.5);
  EXPECT_LT(ipc_stats.p95_ms, 5.0);

  PerformanceMonitor rtt_monitor;
  rtt_monitor.Reset();
  const size_t rtt_samples = 20;
  const auto login_payload = BuildLoginPayload();
  ASSERT_FALSE(login_payload.empty());

  for (size_t i = 0; i < rtt_samples; ++i) {
    const auto start = std::chrono::steady_clock::now();
    client_->send(kLoginReqMsgId, login_payload);
    auto login_rsp = WaitForPacket(kLoginRspMsgId, 2s);
    ASSERT_TRUE(login_rsp.has_value());
    rtt_monitor.RecordLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start));
    std::this_thread::sleep_for(2ms);
  }

  const auto rtt_stats = rtt_monitor.GetLatencyStats();
  EXPECT_LT(rtt_stats.p50_ms, 30.0);
}

enum class PressureMessageType {
  kHeartbeat = 0,
  kMove,
  kAttack,
  kSkill,
  kChat,
};

enum class BottleneckVerdict {
  kProved = 0,
  kFalsified,
  kInconclusive,
};

const char* ToVerdictString(BottleneckVerdict verdict) {
  switch (verdict) {
    case BottleneckVerdict::kProved:
      return "PROVED";
    case BottleneckVerdict::kFalsified:
      return "FALSIFIED";
    case BottleneckVerdict::kInconclusive:
    default:
      return "INCONCLUSIVE";
  }
}

struct PressureStepResult {
  int step_index = 0;
  double target_offered_qps = 0.0;
  double offered_qps = 0.0;
  double effective_qps = 0.0;
  std::string effective_qps_source;
  double elasticity = std::numeric_limits<double>::quiet_NaN();
  double tick_p99_ms = 0.0;
  double overrun_rate = 0.0;
  double drain_budget_hit_rate = 0.0;
  double mailbox_util_avg = 0.0;
  double queue_slope = 0.0;
  double global_overflow_rate = 0.0;
  double hard_backpressure_rate = 0.0;
  double gateway_forward_rate = 0.0;
  double disconnect_overflow_rate = 0.0;
  double gateway_service_disconnect_rate = 0.0;
  double logic_service_disconnect_rate = 0.0;
  bool healthy = false;
  bool metrics_available = true;
  bool service_link_reset_observed = false;
  bool logic_overload_window = false;
  std::vector<std::string> threshold_hits;
};

struct WorkloadVerdict {
  std::string workload_name;
  BottleneckVerdict verdict = BottleneckVerdict::kInconclusive;
  double ceiling_qps = 0.0;
  std::vector<PressureStepResult> steps;
  std::vector<std::string> reasons;
};

struct PressureExperimentResult {
  WorkloadVerdict control;
  WorkloadVerdict mixed;
  WorkloadVerdict write_heavy;
  BottleneckVerdict final_verdict = BottleneckVerdict::kInconclusive;
  bool prometheus_available = false;
  int tick_interval_ms = kPressureTickIntervalMs;
  int warmup_seconds = 60;
  int sample_seconds = 180;
  int cooldown_seconds = 30;
  int repeats = 3;
  size_t swarm_clients = 128;
  double qps_scale = 1.0;
  std::vector<std::string> notes;
};

struct WorkloadDefinition {
  std::string name;
  bool control_group = false;
  std::vector<std::pair<PressureMessageType, int>> weights;
};

struct MessageTemplate {
  uint16_t msg_id = 0;
  std::vector<uint8_t> payload;
};

struct PressureRuntimeConfig {
  int warmup_seconds = 60;
  int sample_seconds = 180;
  int cooldown_seconds = 30;
  int repeats = 3;
  size_t swarm_clients = 128;
  int tick_interval_ms = kPressureTickIntervalMs;
  double qps_scale = 1.0;
  std::vector<double> control_steps_qps;
  std::vector<double> business_multipliers;
};

struct PressureExperimentCache {
  std::mutex mutex;
  std::optional<PressureExperimentResult> result;
};

PressureExperimentCache& GetPressureExperimentCache() {
  static PressureExperimentCache cache;
  return cache;
}

int EnvInt(const char* key, int default_value, int min_value) {
  const char* raw = std::getenv(key);
  if (!raw || raw[0] == '\0') {
    return default_value;
  }
  try {
    const int parsed = std::stoi(raw);
    return std::max(parsed, min_value);
  } catch (...) {
    return default_value;
  }
}

double EnvDouble(const char* key, double default_value, double min_value) {
  const char* raw = std::getenv(key);
  if (!raw || raw[0] == '\0') {
    return default_value;
  }
  try {
    const double parsed = std::stod(raw);
    if (!std::isfinite(parsed)) {
      return default_value;
    }
    return std::max(parsed, min_value);
  } catch (...) {
    return default_value;
  }
}

double Median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const size_t mid = values.size() / 2;
  if ((values.size() % 2) == 0) {
    return (values[mid - 1] + values[mid]) * 0.5;
  }
  return values[mid];
}

double Average(const std::vector<double>& values) {
  if (values.empty()) {
    return 0.0;
  }
  const double sum = std::accumulate(values.begin(), values.end(), 0.0);
  return sum / static_cast<double>(values.size());
}

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double p = std::clamp(percentile, 0.0, 100.0) / 100.0;
  const size_t index = static_cast<size_t>(
      std::clamp(std::ceil(p * static_cast<double>(values.size())) - 1.0,
                 0.0,
                 static_cast<double>(values.size() - 1)));
  return values[index];
}

std::filesystem::path ResolveRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "CMakeLists.txt")) {
      return current;
    }
    current = current.parent_path();
  }
  return std::filesystem::current_path();
}

bool EnsureParentPath(const std::filesystem::path& path) {
  std::error_code ec;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
  }
  return !ec;
}

std::string Join(const std::vector<std::string>& items, const std::string& delimiter) {
  if (items.empty()) {
    return "";
  }
  std::ostringstream out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      out << delimiter;
    }
    out << items[i];
  }
  return out.str();
}

std::string FormatDouble(double value, int precision = 3) {
  if (std::isnan(value)) {
    return "nan";
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string FormatNowLocal() {
  const auto now = std::chrono::system_clock::now();
  std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_result{};
#ifdef _WIN32
  localtime_s(&tm_result, &time);
#else
  localtime_r(&time, &tm_result);
#endif
  std::ostringstream out;
  out << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

double ReadMetric(const std::unordered_map<std::string, double>& metrics,
                  const char* key,
                  bool* found = nullptr) {
  const auto it = metrics.find(key);
  if (it == metrics.end()) {
    if (found) {
      *found = false;
    }
    return 0.0;
  }
  if (found) {
    *found = true;
  }
  return it->second;
}

bool IsLoginSuccessResponsePacket(const NetworkPacket& packet) {
  if (packet.msg_id != kLoginRspMsgId || packet.payload.empty()) {
    return false;
  }

  LoginResponse response;
  const MessageCodecStatus status =
      mir2::common::DecodeLoginResponse(packet.msg_id, packet.payload, &response);
  if (status != MessageCodecStatus::kOk) {
    return false;
  }
  return response.code == mir2::proto::ErrorCode::ERR_OK;
}

bool IsSelectRoleSuccessResponsePacket(const NetworkPacket& packet) {
  if (packet.msg_id != kSelectRoleRspMsgId || packet.payload.empty()) {
    return false;
  }
  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr)) {
    return false;
  }
  const auto* rsp = flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(packet.payload.data());
  return rsp && rsp->code() == mir2::proto::ErrorCode::ERR_OK && rsp->player_id() != 0;
}

double ComputeEffectiveQpsFromMetrics(
    const std::unordered_map<std::string, double>& start,
    const std::unordered_map<std::string, double>& end,
    double sample_seconds,
    std::string* source) {
  if (sample_seconds <= 0.0) {
    if (source) {
      *source = "invalid_window";
    }
    return 0.0;
  }

  const auto compute_delta_qps = [&](const char* metric_name,
                                     bool* available) -> double {
    bool found_start = false;
    bool found_end = false;
    const double start_value = ReadMetric(start, metric_name, &found_start);
    const double end_value = ReadMetric(end, metric_name, &found_end);
    *available = found_start && found_end && end_value >= start_value;
    if (!*available) {
      return 0.0;
    }
    return (end_value - start_value) / sample_seconds;
  };

  bool processed_available = false;
  const double processed_qps = compute_delta_qps("logic_routed_processed_total",
                                                 &processed_available);
  if (processed_available) {
    if (source) {
      *source = "logic_routed_processed_total";
    }
    return processed_qps;
  }

  bool drain_available = false;
  const double drain_qps = compute_delta_qps("logic_hot_event_drain_total",
                                             &drain_available);
  if (drain_available) {
    if (source) {
      *source = "logic_hot_event_drain_total";
    }
    return drain_qps;
  }

  bool enqueue_available = false;
  const double enqueue_qps = compute_delta_qps("logic_hot_event_enqueue_total",
                                               &enqueue_available);
  if (enqueue_available) {
    if (source) {
      *source = "logic_hot_event_enqueue_total";
    }
    return enqueue_qps;
  }

  if (source) {
    *source = "unavailable";
  }
  return 0.0;
}

#if defined(LEGEND2_ENABLE_PROMETHEUS)
std::optional<std::string> FetchPrometheusMetricsText() {
  try {
    asio::io_context io_context;
    asio::ip::tcp::resolver resolver(io_context);
    asio::ip::tcp::socket socket(io_context);
    auto endpoints = resolver.resolve(kHost, std::to_string(kPressureMetricsPort));
    asio::connect(socket, endpoints);

    static constexpr char kRequest[] =
        "GET /metrics HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n\r\n";
    asio::write(socket, asio::buffer(kRequest, std::strlen(kRequest)));

    std::string response;
    std::array<char, 4096> chunk{};
    asio::error_code ec;
    for (;;) {
      const size_t bytes = socket.read_some(asio::buffer(chunk), ec);
      if (bytes > 0) {
        response.append(chunk.data(), bytes);
      }
      if (ec == asio::error::eof) {
        break;
      }
      if (ec) {
        return std::nullopt;
      }
    }
    return response;
  } catch (...) {
    return std::nullopt;
  }
}
#endif

std::optional<std::unordered_map<std::string, double>> FetchPrometheusMetrics() {
#if !defined(LEGEND2_ENABLE_PROMETHEUS)
  return std::nullopt;
#else
  const auto response = FetchPrometheusMetricsText();
  if (!response.has_value()) {
    return std::nullopt;
  }

  const size_t body_pos = response->find("\r\n\r\n");
  const std::string body =
      (body_pos == std::string::npos) ? *response : response->substr(body_pos + 4);

  std::unordered_map<std::string, double> metrics;
  std::istringstream stream(body);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const size_t name_end = line.find(' ');
    if (name_end == std::string::npos) {
      continue;
    }
    std::string name = line.substr(0, name_end);
    const size_t brace_pos = name.find('{');
    if (brace_pos != std::string::npos) {
      name = name.substr(0, brace_pos);
    }
    std::string value_str = line.substr(name_end + 1);
    const size_t tail_pos = value_str.find(' ');
    if (tail_pos != std::string::npos) {
      value_str = value_str.substr(0, tail_pos);
    }
    try {
      metrics[name] = std::stod(value_str);
    } catch (...) {
      continue;
    }
  }
  return metrics;
#endif
}

void EvaluateStepHealth(PressureStepResult* step, int tick_interval_ms) {
  if (!step) {
    return;
  }

  step->threshold_hits.clear();
  if (!step->metrics_available) {
    step->healthy = false;
    step->threshold_hits.push_back("metrics_unavailable");
    return;
  }
  if (step->service_link_reset_observed) {
    step->healthy = false;
    step->threshold_hits.push_back("service_link_reset_observed");
    return;
  }

  const bool tick_ok = step->tick_p99_ms < (0.8 * static_cast<double>(tick_interval_ms));
  const bool overrun_ok = step->overrun_rate < 0.05;
  const bool mailbox_ok = step->mailbox_util_avg < 0.70;
  const bool queue_ok = step->queue_slope <= 0.0;
  step->healthy = tick_ok && overrun_ok && mailbox_ok && queue_ok;

  if (!tick_ok) {
    step->threshold_hits.push_back("health:p99_tick>=0.8*tick");
  }
  if (!overrun_ok) {
    step->threshold_hits.push_back("health:overrun_rate>=0.05");
  }
  if (!mailbox_ok) {
    step->threshold_hits.push_back("health:mailbox_util>=0.70");
  }
  if (!queue_ok) {
    step->threshold_hits.push_back("health:queue_slope>0");
  }
}

bool StepCrossesLogicOverload(const PressureStepResult& step, int tick_interval_ms) {
  if (!step.metrics_available) {
    return false;
  }
  const bool c2 = step.tick_p99_ms >= static_cast<double>(tick_interval_ms) ||
                  step.overrun_rate >= 0.20;
  const bool c3 = step.drain_budget_hit_rate >= 0.30;
  const bool c4 = step.mailbox_util_avg >= 0.90 && step.queue_slope > 0.0;
  const bool c5 = step.global_overflow_rate >= 1.0 || step.hard_backpressure_rate >= 5.0;
  return c2 && c3 && c4 && c5;
}

double ComputeCeilingQps(const std::vector<PressureStepResult>& steps) {
  double ceiling = 0.0;
  for (const auto& step : steps) {
    if (step.healthy) {
      ceiling = std::max(ceiling, step.target_offered_qps);
    }
  }
  return ceiling;
}

bool ControlHealthyAtLoad(const std::vector<PressureStepResult>& control_steps,
                          double target_qps) {
  if (control_steps.empty() || target_qps <= 0.0) {
    return false;
  }
  const PressureStepResult* best = nullptr;
  double best_distance = std::numeric_limits<double>::max();
  for (const auto& step : control_steps) {
    const double distance = std::abs(step.target_offered_qps - target_qps);
    if (distance < best_distance) {
      best = &step;
      best_distance = distance;
    }
  }
  if (!best) {
    return false;
  }
  const double tolerance = std::max(target_qps * 0.10, 1.0);
  return best->healthy && best_distance <= tolerance;
}

enum class DegradationMode {
  kNone = 0,
  kLogic,
  kNonLogic,
};

DegradationMode DetectDegradationMode(const std::vector<PressureStepResult>& steps,
                                      int tick_interval_ms) {
  for (const auto& step : steps) {
    if (step.healthy) {
      continue;
    }
    const bool logic_related = step.tick_p99_ms >= static_cast<double>(tick_interval_ms) ||
                               step.overrun_rate >= 0.20 ||
                               step.drain_budget_hit_rate >= 0.30 ||
                               step.mailbox_util_avg >= 0.90;
    return logic_related ? DegradationMode::kLogic : DegradationMode::kNonLogic;
  }
  return DegradationMode::kNone;
}

template <typename Getter>
double MedianBy(const std::vector<PressureStepResult>& runs, Getter getter) {
  std::vector<double> values;
  values.reserve(runs.size());
  for (const auto& run : runs) {
    values.push_back(getter(run));
  }
  return Median(values);
}

std::string AggregateEffectiveQpsSource(const std::vector<PressureStepResult>& runs,
                                        double effective_qps) {
  if (runs.empty()) {
    return {};
  }

  const PressureStepResult* best = nullptr;
  double best_distance = std::numeric_limits<double>::max();
  for (const auto& run : runs) {
    const double distance = std::abs(run.effective_qps - effective_qps);
    if (!best || distance < best_distance ||
        (std::abs(distance - best_distance) < 1e-9 &&
         best->effective_qps_source.empty() && !run.effective_qps_source.empty())) {
      best = &run;
      best_distance = distance;
    }
  }

  if (best && !best->effective_qps_source.empty()) {
    return best->effective_qps_source;
  }

  for (const auto& run : runs) {
    if (!run.effective_qps_source.empty()) {
      return run.effective_qps_source;
    }
  }

  return {};
}

PressureStepResult AggregateStepRuns(const std::vector<PressureStepResult>& runs,
                                     int step_index,
                                     double target_qps,
                                     int tick_interval_ms) {
  PressureStepResult result;
  result.step_index = step_index;
  result.target_offered_qps = target_qps;
  if (runs.empty()) {
    result.metrics_available = false;
    EvaluateStepHealth(&result, tick_interval_ms);
    return result;
  }

  result.offered_qps = MedianBy(runs, [](const auto& item) { return item.offered_qps; });
  result.effective_qps = MedianBy(runs, [](const auto& item) { return item.effective_qps; });
  result.effective_qps_source = AggregateEffectiveQpsSource(runs, result.effective_qps);
  result.tick_p99_ms = MedianBy(runs, [](const auto& item) { return item.tick_p99_ms; });
  result.overrun_rate = MedianBy(runs, [](const auto& item) { return item.overrun_rate; });
  result.drain_budget_hit_rate =
      MedianBy(runs, [](const auto& item) { return item.drain_budget_hit_rate; });
  result.mailbox_util_avg = MedianBy(runs, [](const auto& item) { return item.mailbox_util_avg; });
  result.queue_slope = MedianBy(runs, [](const auto& item) { return item.queue_slope; });
  result.global_overflow_rate =
      MedianBy(runs, [](const auto& item) { return item.global_overflow_rate; });
  result.hard_backpressure_rate =
      MedianBy(runs, [](const auto& item) { return item.hard_backpressure_rate; });
  result.gateway_forward_rate =
      MedianBy(runs, [](const auto& item) { return item.gateway_forward_rate; });
  result.disconnect_overflow_rate =
      MedianBy(runs, [](const auto& item) { return item.disconnect_overflow_rate; });
  result.gateway_service_disconnect_rate =
      MedianBy(runs, [](const auto& item) { return item.gateway_service_disconnect_rate; });
  result.logic_service_disconnect_rate =
      MedianBy(runs, [](const auto& item) { return item.logic_service_disconnect_rate; });
  result.service_link_reset_observed = std::any_of(
      runs.begin(), runs.end(), [](const auto& run) { return run.service_link_reset_observed; });

  result.metrics_available = std::all_of(runs.begin(), runs.end(), [](const auto& run) {
    return run.metrics_available;
  });
  EvaluateStepHealth(&result, tick_interval_ms);
  return result;
}

WorkloadVerdict FinalizeWorkloadVerdict(const WorkloadDefinition& workload,
                                        std::vector<PressureStepResult> steps,
                                        int tick_interval_ms,
                                        const std::vector<PressureStepResult>* control_steps,
                                        double control_ceiling_qps) {
  WorkloadVerdict verdict;
  verdict.workload_name = workload.name;
  verdict.steps = std::move(steps);

  if (!verdict.steps.empty()) {
    verdict.steps.front().elasticity = 1.0;
    for (size_t i = 1; i < verdict.steps.size(); ++i) {
      const double denominator =
          verdict.steps[i].offered_qps - verdict.steps[i - 1].offered_qps;
      if (std::abs(denominator) < 1e-9) {
        verdict.steps[i].elasticity = 0.0;
      } else {
        verdict.steps[i].elasticity =
            (verdict.steps[i].effective_qps - verdict.steps[i - 1].effective_qps) /
            denominator;
      }
    }
  }

  for (auto& step : verdict.steps) {
    step.logic_overload_window = StepCrossesLogicOverload(step, tick_interval_ms);
  }

  verdict.ceiling_qps = ComputeCeilingQps(verdict.steps);
  const bool metrics_available = std::all_of(verdict.steps.begin(),
                                             verdict.steps.end(),
                                             [](const auto& step) {
                                               return step.metrics_available;
                                             });
  if (!metrics_available) {
    verdict.verdict = BottleneckVerdict::kInconclusive;
    verdict.reasons.push_back("Prometheus metrics unavailable during workload run.");
    return verdict;
  }
  const bool service_link_reset = std::any_of(
      verdict.steps.begin(), verdict.steps.end(), [](const auto& step) {
        return step.service_link_reset_observed;
      });
  if (service_link_reset) {
    verdict.verdict = BottleneckVerdict::kInconclusive;
    verdict.reasons.push_back(
        "Gateway-Logic service link reset observed in sample window.");
    return verdict;
  }

  if (workload.control_group || !control_steps) {
    verdict.verdict = BottleneckVerdict::kInconclusive;
    verdict.reasons.push_back("Control workload ceiling computed.");
    return verdict;
  }

  bool cond1 = false;
  std::vector<size_t> low_elasticity_pairs;
  for (size_t i = 1; i < verdict.steps.size(); ++i) {
    if (verdict.steps[i - 1].elasticity < 0.10 && verdict.steps[i].elasticity < 0.10) {
      cond1 = true;
      low_elasticity_pairs.push_back(i);
    }
  }

  bool cond2_to_5 = false;
  double overload_load = 0.0;
  for (const size_t right : low_elasticity_pairs) {
    const auto& left_step = verdict.steps[right - 1];
    const auto& right_step = verdict.steps[right];
    if (left_step.logic_overload_window || right_step.logic_overload_window) {
      cond2_to_5 = true;
      overload_load = std::max(left_step.target_offered_qps, right_step.target_offered_qps);
      break;
    }
  }

  const bool cond6_ratio = verdict.ceiling_qps > 0.0 && control_ceiling_qps > 0.0 &&
                           (control_ceiling_qps / verdict.ceiling_qps) >= 1.8;
  const bool cond6_control_same_load =
      cond2_to_5 && ControlHealthyAtLoad(*control_steps, overload_load);

  const bool proved =
      cond1 && cond2_to_5 && cond6_ratio && cond6_control_same_load;

  bool falsified_1 = false;
  if (!verdict.steps.empty()) {
    const auto& highest = verdict.steps.back();
    falsified_1 = highest.elasticity >= 0.30 && highest.healthy;
  }

  const bool close_ceiling = verdict.ceiling_qps > 0.0 && control_ceiling_qps > 0.0 &&
                             (std::abs(control_ceiling_qps - verdict.ceiling_qps) /
                              control_ceiling_qps) < 0.15;
  const DegradationMode control_mode = DetectDegradationMode(*control_steps, tick_interval_ms);
  const DegradationMode workload_mode = DetectDegradationMode(verdict.steps, tick_interval_ms);
  const bool falsified_2 = close_ceiling && control_mode != DegradationMode::kNone &&
                           control_mode == workload_mode;

  bool falsified_3 = false;
  for (size_t i = 1; i < verdict.steps.size(); ++i) {
    const auto& step = verdict.steps[i];
    if (step.elasticity < 0.10 &&
        step.tick_p99_ms < (0.8 * static_cast<double>(tick_interval_ms)) &&
        step.overrun_rate < 0.05 && step.mailbox_util_avg < 0.70) {
      falsified_3 = true;
      break;
    }
  }

  if (proved) {
    verdict.verdict = BottleneckVerdict::kProved;
    verdict.reasons.push_back("PROVED conditions met for main-thread hard bottleneck.");
  } else if (falsified_1 || falsified_2 || falsified_3) {
    verdict.verdict = BottleneckVerdict::kFalsified;
    if (falsified_1) {
      verdict.reasons.push_back("FALSIFIED-1: highest step remains elastic and healthy.");
    }
    if (falsified_2) {
      verdict.reasons.push_back(
          "FALSIFIED-2: control and business ceilings are close with same degradation mode.");
    }
    if (falsified_3) {
      verdict.reasons.push_back(
          "FALSIFIED-3: degradation occurs while logic-thread indicators stay healthy.");
    }
  } else {
    verdict.verdict = BottleneckVerdict::kInconclusive;
    verdict.reasons.push_back("Neither PROVED nor FALSIFIED conditions fully matched.");
  }

  return verdict;
}

PressureRuntimeConfig LoadPressureRuntimeConfig() {
  PressureRuntimeConfig config;
  config.warmup_seconds = EnvInt("LEGEND2_PRESSURE_WARMUP_SEC", 60, 0);
  config.sample_seconds = EnvInt("LEGEND2_PRESSURE_SAMPLE_SEC", 180, 1);
  config.cooldown_seconds = EnvInt("LEGEND2_PRESSURE_COOLDOWN_SEC", 30, 0);
  config.repeats = EnvInt("LEGEND2_PRESSURE_REPEATS", 3, 1);
  config.swarm_clients =
      static_cast<size_t>(EnvInt("LEGEND2_PRESSURE_CLIENTS", 128, 1));
  config.qps_scale = EnvDouble("LEGEND2_PRESSURE_QPS_SCALE", 1.0, 0.01);

  const std::vector<double> base_control_steps = {
      2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000};
  config.control_steps_qps.reserve(base_control_steps.size());
  for (const double step : base_control_steps) {
    config.control_steps_qps.push_back(step * config.qps_scale);
  }
  config.business_multipliers = {0.50, 0.65, 0.80, 0.95, 1.10, 1.25};
  return config;
}

std::vector<WorkloadDefinition> BuildWorkloadDefinitions() {
  return {
      WorkloadDefinition{
          .name = "W0-Control",
          .control_group = true,
          .weights = {{PressureMessageType::kHeartbeat, 100}},
      },
      WorkloadDefinition{
          .name = "W1-MixedGameplay",
          .control_group = false,
          .weights = {{PressureMessageType::kMove, 50},
                      {PressureMessageType::kAttack, 20},
                      {PressureMessageType::kSkill, 10},
                      {PressureMessageType::kChat, 10},
                      {PressureMessageType::kHeartbeat, 10}},
      },
      WorkloadDefinition{
          .name = "W2-WriteHeavy",
          .control_group = false,
          .weights = {{PressureMessageType::kMove, 60},
                      {PressureMessageType::kAttack, 30},
                      {PressureMessageType::kSkill, 10}},
      },
  };
}

std::vector<PressureMessageType> BuildMessageSchedule(const WorkloadDefinition& workload) {
  std::vector<PressureMessageType> schedule;
  for (const auto& [type, weight] : workload.weights) {
    const int normalized_weight = std::max(weight, 0);
    for (int i = 0; i < normalized_weight; ++i) {
      schedule.push_back(type);
    }
  }
  if (schedule.empty()) {
    schedule.push_back(PressureMessageType::kHeartbeat);
  }
  return schedule;
}

std::vector<uint8_t> BuildPressureHeartbeatPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto heartbeat = mir2::proto::CreateHeartbeat(
      builder, 0, static_cast<uint32_t>(mir2::common::now_ms()));
  builder.Finish(heartbeat);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPressureMovePayload(int32_t target_x, int32_t target_y) {
  mir2::common::MessageCodecStatus status = MessageCodecStatus::kOk;
  mir2::common::MoveRequest move_req{};
  move_req.target_x = target_x;
  move_req.target_y = target_y;
  auto payload = mir2::common::EncodeMoveRequest(move_req, &status);
  if (status != MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::vector<uint8_t> BuildPressureAttackPayload(uint64_t target_id,
                                                mir2::proto::EntityType target_type) {
  mir2::common::MessageCodecStatus status = MessageCodecStatus::kOk;
  mir2::common::AttackRequest attack_req{};
  attack_req.target_id = target_id;
  attack_req.target_type = target_type;
  auto payload = mir2::common::EncodeAttackRequest(attack_req, &status);
  if (status != MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::vector<uint8_t> BuildPressureSkillPayload(uint32_t skill_id, uint64_t target_id) {
  mir2::common::MessageCodecStatus status = MessageCodecStatus::kOk;
  mir2::common::SkillRequest skill_req{};
  skill_req.skill_id = skill_id;
  skill_req.target_id = target_id;
  auto payload = mir2::common::EncodeSkillRequest(skill_req, &status);
  if (status != MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::vector<uint8_t> BuildPressureChatPayload(mir2::proto::ChatChannel channel,
                                              uint64_t target_id,
                                              const std::string& content) {
  mir2::common::MessageCodecStatus status = MessageCodecStatus::kOk;
  mir2::common::ChatRequest chat_req{};
  chat_req.channel = channel;
  chat_req.target_id = target_id;
  chat_req.content = content;
  auto payload = mir2::common::EncodeChatRequest(chat_req, &status);
  if (status != MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::unordered_map<PressureMessageType, MessageTemplate> BuildPressureMessageTemplates() {
  std::unordered_map<PressureMessageType, MessageTemplate> templates;
  templates[PressureMessageType::kHeartbeat] = MessageTemplate{
      .msg_id = kHeartbeatMsgId, .payload = BuildPressureHeartbeatPayload()};

  auto move_payload = BuildPressureMovePayload(/*target_x=*/120, /*target_y=*/121);
  if (!move_payload.empty()) {
    templates[PressureMessageType::kMove] = MessageTemplate{
        .msg_id = kMoveReqMsgId, .payload = std::move(move_payload)};
  }

  auto attack_payload =
      BuildPressureAttackPayload(/*target_id=*/1, mir2::proto::EntityType::PLAYER);
  if (!attack_payload.empty()) {
    templates[PressureMessageType::kAttack] = MessageTemplate{
        .msg_id = kAttackReqMsgId, .payload = std::move(attack_payload)};
  }

  auto skill_payload = BuildPressureSkillPayload(/*skill_id=*/1001, /*target_id=*/1);
  if (!skill_payload.empty()) {
    templates[PressureMessageType::kSkill] = MessageTemplate{
        .msg_id = kSkillReqMsgId, .payload = std::move(skill_payload)};
  }

  auto chat_payload = BuildPressureChatPayload(
      mir2::proto::ChatChannel::PRIVATE,
      1,
      "stage4 pressure chat");
  if (!chat_payload.empty()) {
    templates[PressureMessageType::kChat] = MessageTemplate{
        .msg_id = kChatReqMsgId, .payload = std::move(chat_payload)};
  }

  return templates;
}

void WriteStage4Reports(const PressureExperimentResult& result) {
  const auto repo_root = ResolveRepoRoot();
  const auto report_md = repo_root / kStage4MarkdownReport;
  const auto report_csv = repo_root / kStage4CsvReport;
  if (!EnsureParentPath(report_md) || !EnsureParentPath(report_csv)) {
    return;
  }

  {
    std::ofstream csv(report_csv, std::ios::out | std::ios::trunc);
    if (csv.is_open()) {
      csv << "workload,step,target_offered_qps,offered_qps,effective_qps,elasticity,"
             "effective_qps_source,tick_p99_ms,overrun_rate,drain_budget_hit_rate,"
             "mailbox_util_avg,queue_slope,"
             "global_overflow_rate,hard_backpressure_rate,gateway_forward_rate,"
             "disconnect_overflow_rate,gateway_service_disconnect_rate,"
             "logic_service_disconnect_rate,service_link_reset_observed,"
             "healthy,logic_overload_window,threshold_hits,verdict\n";

      auto write_rows = [&csv](const WorkloadVerdict& workload) {
        for (const auto& step : workload.steps) {
          csv << workload.workload_name << ','
              << step.step_index << ','
              << FormatDouble(step.target_offered_qps) << ','
              << FormatDouble(step.offered_qps) << ','
              << FormatDouble(step.effective_qps) << ','
              << FormatDouble(step.elasticity) << ','
              << step.effective_qps_source << ','
              << FormatDouble(step.tick_p99_ms) << ','
              << FormatDouble(step.overrun_rate) << ','
              << FormatDouble(step.drain_budget_hit_rate) << ','
              << FormatDouble(step.mailbox_util_avg) << ','
              << FormatDouble(step.queue_slope) << ','
              << FormatDouble(step.global_overflow_rate) << ','
              << FormatDouble(step.hard_backpressure_rate) << ','
              << FormatDouble(step.gateway_forward_rate) << ','
              << FormatDouble(step.disconnect_overflow_rate) << ','
              << FormatDouble(step.gateway_service_disconnect_rate) << ','
              << FormatDouble(step.logic_service_disconnect_rate) << ','
              << (step.service_link_reset_observed ? "true" : "false") << ','
              << (step.healthy ? "true" : "false") << ','
              << (step.logic_overload_window ? "true" : "false") << ",\""
              << Join(step.threshold_hits, ";") << "\","
              << ToVerdictString(workload.verdict) << '\n';
        }
      };
      write_rows(result.control);
      write_rows(result.mixed);
      write_rows(result.write_heavy);
    }
  }

  {
    std::ofstream md(report_md, std::ios::out | std::ios::trunc);
    if (md.is_open()) {
      md << "# Stage 4 Logic Main Thread Bottleneck Report\n\n";
      md << "Generated: " << FormatNowLocal() << "\n\n";
      md << "Final Verdict: **" << ToVerdictString(result.final_verdict) << "**\n\n";

      md << "## Environment\n";
      md << "- tick_interval_ms: " << result.tick_interval_ms << "\n";
      md << "- warmup/sample/cooldown (s): "
         << result.warmup_seconds << "/" << result.sample_seconds << "/"
         << result.cooldown_seconds << "\n";
      md << "- repeats: " << result.repeats << "\n";
      md << "- swarm_clients: " << result.swarm_clients << "\n";
      md << "- qps_scale: " << FormatDouble(result.qps_scale) << "\n";
      md << "- prometheus_available: " << (result.prometheus_available ? "true" : "false")
         << "\n\n";

      md << "## Workload Verdicts\n";
      md << "| Workload | Ceiling QPS | Verdict |\n";
      md << "| --- | ---: | --- |\n";
      md << "| " << result.control.workload_name << " | "
         << FormatDouble(result.control.ceiling_qps, 2) << " | "
         << ToVerdictString(result.control.verdict) << " |\n";
      md << "| " << result.mixed.workload_name << " | "
         << FormatDouble(result.mixed.ceiling_qps, 2) << " | "
         << ToVerdictString(result.mixed.verdict) << " |\n";
      md << "| " << result.write_heavy.workload_name << " | "
         << FormatDouble(result.write_heavy.ceiling_qps, 2) << " | "
         << ToVerdictString(result.write_heavy.verdict) << " |\n\n";

      auto write_step_table = [&md](const WorkloadVerdict& workload) {
        md << "### " << workload.workload_name << "\n";
        md << "| Step | Target | Offered | Effective | Elasticity | Tick p99 | "
              "Source | Overrun | Util | Queue Slope | Overflow/s | HardBP/s | "
              "SvcDisc(g/l)/s | Healthy |\n";
        md << "| ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: "
              "| ---: | --- | --- |\n";
        for (const auto& step : workload.steps) {
          md << "| " << step.step_index
             << " | " << FormatDouble(step.target_offered_qps, 2)
             << " | " << FormatDouble(step.offered_qps, 2)
             << " | " << FormatDouble(step.effective_qps, 2)
             << " | " << FormatDouble(step.elasticity, 3)
             << " | " << FormatDouble(step.tick_p99_ms, 3)
             << " | " << step.effective_qps_source
             << " | " << FormatDouble(step.overrun_rate, 3)
             << " | " << FormatDouble(step.mailbox_util_avg, 3)
             << " | " << FormatDouble(step.queue_slope, 3)
             << " | " << FormatDouble(step.global_overflow_rate, 3)
             << " | " << FormatDouble(step.hard_backpressure_rate, 3)
             << " | " << FormatDouble(step.gateway_service_disconnect_rate, 3)
             << "/" << FormatDouble(step.logic_service_disconnect_rate, 3)
             << " | " << (step.healthy ? "PASS" : "FAIL") << " |\n";
        }
        md << '\n';

        if (!workload.reasons.empty()) {
          md << "Reasons: " << Join(workload.reasons, " | ") << "\n\n";
        }
      };

      write_step_table(result.control);
      write_step_table(result.mixed);
      write_step_table(result.write_heavy);

      md << "## Threshold Hits\n";
      auto write_threshold_hits = [&md](const WorkloadVerdict& workload) {
        for (const auto& step : workload.steps) {
          if (step.threshold_hits.empty()) {
            continue;
          }
          md << "- " << workload.workload_name << " step " << step.step_index << ": "
             << Join(step.threshold_hits, ", ") << "\n";
        }
      };
      write_threshold_hits(result.control);
      write_threshold_hits(result.mixed);
      write_threshold_hits(result.write_heavy);
      md << '\n';

      md << "## Conclusion\n";
      md << "Final classification: **" << ToVerdictString(result.final_verdict) << "**.\n";
      if (!result.notes.empty()) {
        md << "Notes: " << Join(result.notes, " | ") << "\n";
      }
    }
  }
}

TEST(GatewayLogicPressureMetricsTest, EffectiveQpsPrefersProcessedCounter) {
  std::unordered_map<std::string, double> start{
      {"logic_routed_processed_total", 100.0},
      {"logic_hot_event_drain_total", 500.0},
      {"logic_hot_event_enqueue_total", 900.0},
  };
  std::unordered_map<std::string, double> end{
      {"logic_routed_processed_total", 190.0},
      {"logic_hot_event_drain_total", 900.0},
      {"logic_hot_event_enqueue_total", 1500.0},
  };

  std::string source;
  const double qps = ComputeEffectiveQpsFromMetrics(start, end, 10.0, &source);
  EXPECT_DOUBLE_EQ(qps, 9.0);
  EXPECT_EQ(source, "logic_routed_processed_total");
}

TEST(GatewayLogicPressureMetricsTest, EffectiveQpsFallsBackToDrainCounter) {
  std::unordered_map<std::string, double> start{
      {"logic_hot_event_drain_total", 100.0},
      {"logic_hot_event_enqueue_total", 200.0},
  };
  std::unordered_map<std::string, double> end{
      {"logic_hot_event_drain_total", 180.0},
      {"logic_hot_event_enqueue_total", 320.0},
  };

  std::string source;
  const double qps = ComputeEffectiveQpsFromMetrics(start, end, 20.0, &source);
  EXPECT_DOUBLE_EQ(qps, 4.0);
  EXPECT_EQ(source, "logic_hot_event_drain_total");
}

TEST(GatewayLogicPressureMetricsTest, AggregateStepRunsPropagatesMedianSource) {
  PressureStepResult run0;
  run0.offered_qps = 100.0;
  run0.effective_qps = 50.0;
  run0.effective_qps_source = "logic_hot_event_enqueue_total";
  run0.metrics_available = true;

  PressureStepResult run1;
  run1.offered_qps = 100.0;
  run1.effective_qps = 80.0;
  run1.effective_qps_source = "logic_hot_event_drain_total";
  run1.metrics_available = true;

  PressureStepResult run2;
  run2.offered_qps = 100.0;
  run2.effective_qps = 120.0;
  run2.effective_qps_source = "logic_routed_processed_total";
  run2.metrics_available = true;

  const PressureStepResult aggregated = AggregateStepRuns(
      {run0, run1, run2}, /*step_index=*/1, /*target_qps=*/100.0, /*tick_interval_ms=*/50);
  EXPECT_DOUBLE_EQ(aggregated.effective_qps, 80.0);
  EXPECT_EQ(aggregated.effective_qps_source, "logic_hot_event_drain_total");
}

TEST(GatewayLogicPressureMetricsTest, AggregateStepRunsFallsBackToFirstNonEmptySource) {
  PressureStepResult run0;
  run0.offered_qps = 100.0;
  run0.effective_qps = 60.0;
  run0.effective_qps_source = "";
  run0.metrics_available = true;

  PressureStepResult run1;
  run1.offered_qps = 100.0;
  run1.effective_qps = 90.0;
  run1.effective_qps_source = "";
  run1.metrics_available = true;

  PressureStepResult run2;
  run2.offered_qps = 100.0;
  run2.effective_qps = 140.0;
  run2.effective_qps_source = "logic_hot_event_drain_total";
  run2.metrics_available = true;

  const PressureStepResult aggregated = AggregateStepRuns(
      {run0, run1, run2}, /*step_index=*/2, /*target_qps=*/100.0, /*tick_interval_ms=*/50);
  EXPECT_DOUBLE_EQ(aggregated.effective_qps, 90.0);
  EXPECT_EQ(aggregated.effective_qps_source, "logic_hot_event_drain_total");
}

TEST(GatewayLogicPressureAuthTest, AcceptsSuccessfulLoginResponsePacket) {
  LoginResponse response;
  response.code = mir2::proto::ErrorCode::ERR_OK;
  response.account_id = 42;
  response.session_token = "pressure_token";

  MessageCodecStatus status = MessageCodecStatus::kOk;
  const auto payload = mir2::common::EncodeLoginResponse(response, &status);
  ASSERT_EQ(status, MessageCodecStatus::kOk);
  ASSERT_FALSE(payload.empty());

  const NetworkPacket packet{kLoginRspMsgId, payload};
  EXPECT_TRUE(IsLoginSuccessResponsePacket(packet));
}

TEST(GatewayLogicPressureAuthTest, RejectsFailedLoginResponsePacket) {
  LoginResponse response;
  response.code = mir2::proto::ErrorCode::ERR_UNKNOWN;
  response.account_id = 0;
  response.session_token.clear();

  MessageCodecStatus status = MessageCodecStatus::kOk;
  const auto payload = mir2::common::EncodeLoginResponse(response, &status);
  ASSERT_EQ(status, MessageCodecStatus::kOk);
  ASSERT_FALSE(payload.empty());

  const NetworkPacket packet{kLoginRspMsgId, payload};
  EXPECT_FALSE(IsLoginSuccessResponsePacket(packet));
}

TEST(GatewayLogicPressureVerdictTest, ServiceLinkResetForcesInconclusive) {
  WorkloadDefinition workload{
      .name = "W1-MixedGameplay",
      .control_group = false,
      .weights = {{PressureMessageType::kMove, 100}},
  };

  PressureStepResult control_step;
  control_step.step_index = 0;
  control_step.target_offered_qps = 1000.0;
  control_step.offered_qps = 1000.0;
  control_step.effective_qps = 900.0;
  control_step.metrics_available = true;
  control_step.healthy = true;
  std::vector<PressureStepResult> control_steps{control_step};

  PressureStepResult business_step;
  business_step.step_index = 0;
  business_step.target_offered_qps = 1000.0;
  business_step.offered_qps = 900.0;
  business_step.effective_qps = 600.0;
  business_step.metrics_available = true;
  business_step.service_link_reset_observed = true;

  const WorkloadVerdict verdict = FinalizeWorkloadVerdict(
      workload,
      {business_step},
      /*tick_interval_ms=*/50,
      &control_steps,
      /*control_ceiling_qps=*/1000.0);
  EXPECT_EQ(verdict.verdict, BottleneckVerdict::kInconclusive);
  EXPECT_FALSE(verdict.reasons.empty());
  EXPECT_NE(verdict.reasons.front().find("service link"), std::string::npos);
}

class GatewayLogicPressureTest : public GatewayLogicIntegrationTest {
 protected:
  struct SwarmClient {
    std::unique_ptr<DualChannelClient> client;
    uint32_t character_id = 0;
  };

  void SetUp() override {
    if (!mir2::test::integration::BenchmarkOnlyEnabled()) {
      GTEST_SKIP() << "Set LEGEND2_BENCHMARK_ONLY=1 to run pressure benchmarks.";
    }

    ports_ = AllocateTestPorts();
    temp_dir_ = CreateTempDir("mir2_gateway_logic_pressure");
    const std::string transport = IpcTransport();
    const std::string uds_path = IpcUdsPath();

    gateway_config_path_ = WriteTempConfig(
        temp_dir_,
        "gateway.yaml",
        BuildGatewayConfig(ports_,
                           temp_dir_ / "gateway_logs",
                           transport,
                           uds_path,
                           /*metrics_port=*/0,
                           /*tick_interval_ms=*/20,
                           kPressureLoginIpRateLimitCapacity,
                           kPressureLoginIpRateLimitRefillRate,
                           kPressureGatewayIoThreads,
                           kPressureServerMaxConnections,
                           kPressureServiceLinkWriteQueueSize));
    logic_config_path_ = WriteTempConfig(
        temp_dir_,
        "logic.yaml",
        BuildLogicConfig(ports_,
                         temp_dir_ / "logic_logs",
                         transport,
                         uds_path,
                         kPressureMetricsPort,
                         kPressureTickIntervalMs,
                         /*io_threads=*/1,
                         kPressureServerMaxConnections,
                         kPressureServiceLinkWriteQueueSize));

    gateway_ = std::make_unique<GatewayServer>();
    ASSERT_TRUE(gateway_->Initialize(gateway_config_path_));

    logic_ = std::make_unique<LogicServer>();
    ASSERT_TRUE(logic_->Initialize(logic_config_path_));
    scripted_trade_login_branch_count_.store(0, std::memory_order_relaxed);
    scripted_party_login_branch_count_.store(0, std::memory_order_relaxed);
    InstallLogicHandlers();

    logic_thread_ = std::thread([this]() { logic_->Run(); });
    gateway_->Run();
    client_ = std::make_unique<DualChannelClient>();
  }

  bool WaitForLogicConnectedNoClient(std::chrono::milliseconds timeout) {
    return WaitForCondition(
        [this]() { return gateway_ && gateway_->IsLogicConnected(); },
        timeout,
        10ms,
        []() {});
  }

  std::optional<NetworkPacket> WaitForPacketFromClient(DualChannelClient* client,
                                                       uint16_t msg_id,
                                                       std::chrono::milliseconds timeout) {
    if (!client) {
      return std::nullopt;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      client->update();
      while (true) {
        auto packet = client->receive();
        if (!packet.has_value()) {
          break;
        }
        if (packet->msg_id == msg_id) {
          return packet;
        }
      }
      std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
  }

  void DrainClient(DualChannelClient* client) {
    if (!client) {
      return;
    }
    client->update();
    while (client->receive().has_value()) {
    }
  }

  void PumpSwarm(std::vector<SwarmClient>* clients) {
    if (!clients) {
      return;
    }
    for (auto& client : *clients) {
      DrainClient(client.client.get());
    }
  }

  bool ConnectAndLoginSwarmClient(SwarmClient* swarm_client, uint32_t character_id) {
    if (!swarm_client || character_id == 0) {
      return false;
    }
    swarm_client->client = std::make_unique<DualChannelClient>();
    if (!ConnectClient(swarm_client->client.get())) {
      return false;
    }
    if (!WaitForLogicConnectedNoClient(3s)) {
      return false;
    }
    const std::string username =
        std::string(kPressureUserPrefix) + std::to_string(character_id);
    const auto payload = BuildLoginPayload(username);
    if (payload.empty()) {
      return false;
    }
    swarm_client->client->send(kLoginReqMsgId, payload);
    auto login_rsp = WaitForPacketFromClient(swarm_client->client.get(), kLoginRspMsgId, 3s);
    if (!login_rsp.has_value()) {
      return false;
    }
    if (!IsLoginSuccessResponsePacket(*login_rsp)) {
      return false;
    }
    auto select_role_rsp =
        WaitForPacketFromClient(swarm_client->client.get(), kSelectRoleRspMsgId, 3s);
    if (!select_role_rsp.has_value()) {
      return false;
    }
    if (!IsSelectRoleSuccessResponsePacket(*select_role_rsp)) {
      return false;
    }
    swarm_client->character_id = character_id;
    DrainClient(swarm_client->client.get());
    return true;
  }

  bool PrepareSwarm(std::vector<SwarmClient>* clients, size_t count) {
    if (!clients || count == 0) {
      return false;
    }
    clients->clear();
    clients->reserve(count);
    constexpr uint32_t kBaseCharacterId = 900000;
    for (size_t i = 0; i < count; ++i) {
      SwarmClient entry;
      const uint32_t character_id = kBaseCharacterId + static_cast<uint32_t>(i + 1);
      if (!ConnectAndLoginSwarmClient(&entry, character_id)) {
        return false;
      }
      clients->push_back(std::move(entry));
    }
    return true;
  }

  void RunCooldown(std::vector<SwarmClient>* clients, int seconds) {
    if (seconds <= 0) {
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      PumpSwarm(clients);
      std::this_thread::sleep_for(1ms);
    }
  }

  PressureStepResult RunSingleStep(const WorkloadDefinition& /*workload*/,
                                   int step_index,
                                   double target_qps,
                                   const PressureRuntimeConfig& runtime,
                                   const std::vector<PressureMessageType>& schedule,
                                   const std::unordered_map<PressureMessageType, MessageTemplate>&
                                       templates,
                                   std::vector<SwarmClient>* clients) {
    PressureStepResult result;
    result.step_index = step_index;
    result.target_offered_qps = target_qps;

    if (!clients || clients->empty()) {
      result.metrics_available = false;
      EvaluateStepHealth(&result, runtime.tick_interval_ms);
      return result;
    }

    std::vector<MessageTemplate> per_client_move_templates(clients->size());
    std::vector<bool> per_client_move_template_ready(clients->size(), false);
    std::vector<MessageTemplate> per_client_attack_templates(clients->size());
    std::vector<bool> per_client_attack_template_ready(clients->size(), false);
    std::vector<MessageTemplate> per_client_skill_templates(clients->size());
    std::vector<bool> per_client_skill_template_ready(clients->size(), false);
    std::vector<MessageTemplate> per_client_chat_templates(clients->size());
    std::vector<bool> per_client_chat_template_ready(clients->size(), false);
    for (size_t i = 0; i < clients->size(); ++i) {
      constexpr int kPressureGridCols = 10;
      constexpr int kPressureGridSpacing = 8;
      const int32_t col = static_cast<int32_t>(i % kPressureGridCols);
      const int32_t row = static_cast<int32_t>(i / kPressureGridCols);
      const int32_t move_target_x = 100 + col * kPressureGridSpacing;
      const int32_t move_target_y = 100 + row * kPressureGridSpacing;
      auto move_payload = BuildPressureMovePayload(move_target_x, move_target_y);
      if (!move_payload.empty()) {
        per_client_move_templates[i] = MessageTemplate{
            .msg_id = kMoveReqMsgId, .payload = std::move(move_payload)};
        per_client_move_template_ready[i] = true;
      }

      const size_t target_index = (i + 1) % clients->size();
      const uint64_t target_character_id = (*clients)[target_index].character_id;

      auto attack_payload =
          BuildPressureAttackPayload(target_character_id, mir2::proto::EntityType::PLAYER);
      if (!attack_payload.empty()) {
        per_client_attack_templates[i] = MessageTemplate{
            .msg_id = kAttackReqMsgId, .payload = std::move(attack_payload)};
        per_client_attack_template_ready[i] = true;
      }

      auto skill_payload = BuildPressureSkillPayload(/*skill_id=*/1001, target_character_id);
      if (!skill_payload.empty()) {
        per_client_skill_templates[i] = MessageTemplate{
            .msg_id = kSkillReqMsgId, .payload = std::move(skill_payload)};
        per_client_skill_template_ready[i] = true;
      }

      const size_t chat_target_index = (i + 1) % clients->size();
      const uint64_t chat_target_character_id = (*clients)[chat_target_index].character_id;
      auto chat_payload = BuildPressureChatPayload(
          mir2::proto::ChatChannel::PRIVATE,
          chat_target_character_id,
          "stage4 pressure chat");
      if (!chat_payload.empty()) {
        per_client_chat_templates[i] = MessageTemplate{
            .msg_id = kChatReqMsgId, .payload = std::move(chat_payload)};
        per_client_chat_template_ready[i] = true;
      }
    }

    auto run_phase = [&](int seconds,
                         bool collect_sample,
                         std::vector<double>* tick_samples,
                         std::vector<double>* mailbox_util_samples,
                         std::vector<double>* queue_depth_samples,
                         size_t* offered_messages,
                         std::optional<std::unordered_map<std::string, double>>* metrics_start,
                         std::optional<std::unordered_map<std::string, double>>* metrics_end,
                         bool* metrics_ok) {
      if (seconds <= 0) {
        return;
      }

      if (collect_sample && metrics_start) {
        *metrics_start = FetchPrometheusMetrics();
        if (!metrics_start->has_value() && metrics_ok) {
          *metrics_ok = false;
        }
      }

      const auto phase_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(seconds);
      auto last_tick = std::chrono::steady_clock::now();
      auto next_sample_tick = std::chrono::steady_clock::now() + 1s;
      double tokens = 0.0;
      size_t schedule_index = 0;
      size_t client_index = 0;

      while (std::chrono::steady_clock::now() < phase_deadline) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed_seconds =
            std::chrono::duration<double>(now - last_tick).count();
        last_tick = now;
        tokens += target_qps * elapsed_seconds;

        size_t to_send = 0;
        if (tokens >= 1.0) {
          to_send = static_cast<size_t>(tokens);
          const size_t max_burst = static_cast<size_t>(std::max(target_qps * 0.25, 64.0));
          to_send = std::min(to_send, max_burst);
          tokens -= static_cast<double>(to_send);
        }

        for (size_t n = 0; n < to_send; ++n) {
          const auto message_type = schedule[schedule_index % schedule.size()];
          ++schedule_index;
          const auto message_it = templates.find(message_type);
          if (message_it == templates.end()) {
            continue;
          }
          const size_t selected_client_index = client_index % clients->size();
          auto& target_client = (*clients)[selected_client_index];
          ++client_index;

          const MessageTemplate* selected_template = &message_it->second;
          if (message_type == PressureMessageType::kMove &&
              per_client_move_template_ready[selected_client_index]) {
            selected_template = &per_client_move_templates[selected_client_index];
          } else if (message_type == PressureMessageType::kAttack &&
              per_client_attack_template_ready[selected_client_index]) {
            selected_template = &per_client_attack_templates[selected_client_index];
          } else if (message_type == PressureMessageType::kSkill &&
                     per_client_skill_template_ready[selected_client_index]) {
            selected_template = &per_client_skill_templates[selected_client_index];
          } else if (message_type == PressureMessageType::kChat &&
                     per_client_chat_template_ready[selected_client_index]) {
            selected_template = &per_client_chat_templates[selected_client_index];
          }

          target_client.client->send(selected_template->msg_id, selected_template->payload);
          if (collect_sample && offered_messages) {
            ++(*offered_messages);
          }
        }

        PumpSwarm(clients);

        if (collect_sample && std::chrono::steady_clock::now() >= next_sample_tick) {
          const auto metrics_snapshot = FetchPrometheusMetrics();
          if (!metrics_snapshot.has_value()) {
            if (metrics_ok) {
              *metrics_ok = false;
            }
          } else {
            bool found_tick = false;
            bool found_mailbox = false;
            bool found_queue = false;
            const double tick = ReadMetric(*metrics_snapshot, "logic_tick_duration_ms", &found_tick);
            const double mailbox = ReadMetric(*metrics_snapshot,
                                              "logic_mailbox_global_pending_utilization",
                                              &found_mailbox);
            const double queue = ReadMetric(*metrics_snapshot, "logic_hot_event_queue_depth",
                                            &found_queue);
            if (tick_samples && found_tick) {
              tick_samples->push_back(tick);
            }
            if (mailbox_util_samples && found_mailbox) {
              mailbox_util_samples->push_back(mailbox);
            }
            if (queue_depth_samples && found_queue) {
              queue_depth_samples->push_back(queue);
            }
          }
          next_sample_tick += 1s;
        }

        std::this_thread::sleep_for(1ms);
      }

      if (collect_sample && metrics_end) {
        *metrics_end = FetchPrometheusMetrics();
        if (!metrics_end->has_value() && metrics_ok) {
          *metrics_ok = false;
        }
      }
    };

    std::vector<double> tick_samples;
    std::vector<double> mailbox_samples;
    std::vector<double> queue_samples;
    size_t offered_messages = 0;
    bool metrics_ok = true;
    std::optional<std::unordered_map<std::string, double>> metrics_start;
    std::optional<std::unordered_map<std::string, double>> metrics_end;

    run_phase(runtime.warmup_seconds,
              false,
              nullptr,
              nullptr,
              nullptr,
              nullptr,
              nullptr,
              nullptr,
              nullptr);

    run_phase(runtime.sample_seconds,
              true,
              &tick_samples,
              &mailbox_samples,
              &queue_samples,
              &offered_messages,
              &metrics_start,
              &metrics_end,
              &metrics_ok);

    RunCooldown(clients, runtime.cooldown_seconds);

    result.offered_qps =
        static_cast<double>(offered_messages) / static_cast<double>(runtime.sample_seconds);
    result.metrics_available =
        metrics_ok && metrics_start.has_value() && metrics_end.has_value();
    result.tick_p99_ms = Percentile(tick_samples, 99.0);
    result.mailbox_util_avg = Average(mailbox_samples);

    if (result.metrics_available) {
      const auto& start = *metrics_start;
      const auto& end = *metrics_end;
      const double sample_seconds = static_cast<double>(runtime.sample_seconds);
      const double tick_window = std::max(
          1.0, (sample_seconds * 1000.0) / static_cast<double>(runtime.tick_interval_ms));

      result.effective_qps = ComputeEffectiveQpsFromMetrics(
          start, end, sample_seconds, &result.effective_qps_source);

      const double overrun_start = ReadMetric(start, "logic_tick_overrun_total");
      const double overrun_end = ReadMetric(end, "logic_tick_overrun_total");
      result.overrun_rate = (overrun_end - overrun_start) / tick_window;

      const double budget_hit_start =
          ReadMetric(start, "logic_hot_event_drain_budget_hit_total");
      const double budget_hit_end =
          ReadMetric(end, "logic_hot_event_drain_budget_hit_total");
      result.drain_budget_hit_rate = (budget_hit_end - budget_hit_start) / tick_window;

      const double queue_start = ReadMetric(start, "logic_hot_event_queue_depth");
      const double queue_end = ReadMetric(end, "logic_hot_event_queue_depth");
      result.queue_slope = (queue_end - queue_start) / sample_seconds;

      const double global_overflow_start =
          ReadMetric(start, "logic_mailbox_global_overflow_total");
      const double global_overflow_end =
          ReadMetric(end, "logic_mailbox_global_overflow_total");
      result.global_overflow_rate =
          (global_overflow_end - global_overflow_start) / sample_seconds;

      const double hard_bp_start =
          ReadMetric(start, "logic_mailbox_hard_backpressure_total");
      const double hard_bp_end =
          ReadMetric(end, "logic_mailbox_hard_backpressure_total");
      result.hard_backpressure_rate = (hard_bp_end - hard_bp_start) / sample_seconds;

      const double gateway_forward_start = ReadMetric(start, "gateway_forward_total");
      const double gateway_forward_end = ReadMetric(end, "gateway_forward_total");
      result.gateway_forward_rate =
          (gateway_forward_end - gateway_forward_start) / sample_seconds;

      const double disconnect_drop_start =
          ReadMetric(start, "gateway_disconnect_queue_dropped_overflow_total");
      const double disconnect_drop_end =
          ReadMetric(end, "gateway_disconnect_queue_dropped_overflow_total");
      result.disconnect_overflow_rate =
          (disconnect_drop_end - disconnect_drop_start) / sample_seconds;

      bool found_gateway_service_disc_start = false;
      bool found_gateway_service_disc_end = false;
      const double gateway_service_disc_start = ReadMetric(
          start, "gateway_service_disconnected_logic", &found_gateway_service_disc_start);
      const double gateway_service_disc_end = ReadMetric(
          end, "gateway_service_disconnected_logic", &found_gateway_service_disc_end);
      if (found_gateway_service_disc_start && found_gateway_service_disc_end &&
          gateway_service_disc_end >= gateway_service_disc_start) {
        result.gateway_service_disconnect_rate =
            (gateway_service_disc_end - gateway_service_disc_start) / sample_seconds;
      }

      bool found_logic_service_disc_start = false;
      bool found_logic_service_disc_end = false;
      double logic_service_disc_start = ReadMetric(
          start,
          "logic_service_disconnected_gateway_total",
          &found_logic_service_disc_start);
      double logic_service_disc_end = ReadMetric(
          end,
          "logic_service_disconnected_gateway_total",
          &found_logic_service_disc_end);
      if (!found_logic_service_disc_start || !found_logic_service_disc_end) {
        logic_service_disc_start = ReadMetric(
            start, "logic_service_disconnected_gateway", &found_logic_service_disc_start);
        logic_service_disc_end = ReadMetric(
            end, "logic_service_disconnected_gateway", &found_logic_service_disc_end);
      }
      if (found_logic_service_disc_start && found_logic_service_disc_end &&
          logic_service_disc_end >= logic_service_disc_start) {
        result.logic_service_disconnect_rate =
            (logic_service_disc_end - logic_service_disc_start) / sample_seconds;
      }
      result.service_link_reset_observed =
          result.gateway_service_disconnect_rate > kServiceLinkDisconnectRateEpsilon ||
          result.logic_service_disconnect_rate > kServiceLinkDisconnectRateEpsilon;
    } else {
      result.effective_qps_source = "metrics_unavailable";
    }

    EvaluateStepHealth(&result, runtime.tick_interval_ms);
    result.logic_overload_window = StepCrossesLogicOverload(result, runtime.tick_interval_ms);
    return result;
  }

  WorkloadVerdict RunWorkload(const WorkloadDefinition& workload,
                              const std::vector<double>& steps_qps,
                              const PressureRuntimeConfig& runtime,
                              std::vector<SwarmClient>* clients,
                              const std::vector<PressureStepResult>* control_steps,
                              double control_ceiling_qps) {
    const auto schedule = BuildMessageSchedule(workload);
    const auto templates = BuildPressureMessageTemplates();
    std::vector<PressureStepResult> aggregated_steps;
    aggregated_steps.reserve(steps_qps.size());

    for (size_t step_index = 0; step_index < steps_qps.size(); ++step_index) {
      const double target_qps = steps_qps[step_index];
      std::vector<PressureStepResult> repeat_runs;
      repeat_runs.reserve(static_cast<size_t>(runtime.repeats));

      for (int repeat = 0; repeat < runtime.repeats; ++repeat) {
        repeat_runs.push_back(RunSingleStep(workload,
                                            static_cast<int>(step_index),
                                            target_qps,
                                            runtime,
                                            schedule,
                                            templates,
                                            clients));
      }

      aggregated_steps.push_back(AggregateStepRuns(repeat_runs,
                                                   static_cast<int>(step_index),
                                                   target_qps,
                                                   runtime.tick_interval_ms));
    }

    return FinalizeWorkloadVerdict(
        workload, std::move(aggregated_steps), runtime.tick_interval_ms, control_steps,
        control_ceiling_qps);
  }

  PressureExperimentResult ExecutePressureExperiment() {
    PressureExperimentResult result;
    const PressureRuntimeConfig runtime = LoadPressureRuntimeConfig();
    result.tick_interval_ms = runtime.tick_interval_ms;
    result.warmup_seconds = runtime.warmup_seconds;
    result.sample_seconds = runtime.sample_seconds;
    result.cooldown_seconds = runtime.cooldown_seconds;
    result.repeats = runtime.repeats;
    result.swarm_clients = runtime.swarm_clients;
    result.qps_scale = runtime.qps_scale;

#if !defined(LEGEND2_ENABLE_PROMETHEUS)
    result.prometheus_available = false;
    result.control.workload_name = "W0-Control";
    result.mixed.workload_name = "W1-MixedGameplay";
    result.write_heavy.workload_name = "W2-WriteHeavy";
    result.control.verdict = BottleneckVerdict::kInconclusive;
    result.mixed.verdict = BottleneckVerdict::kInconclusive;
    result.write_heavy.verdict = BottleneckVerdict::kInconclusive;
    result.final_verdict = BottleneckVerdict::kInconclusive;
    result.notes.push_back("Prometheus is disabled at build time.");
    WriteStage4Reports(result);
    return result;
#else
    const auto metrics_probe = FetchPrometheusMetrics();
    result.prometheus_available = metrics_probe.has_value();
    if (!result.prometheus_available) {
      result.control.workload_name = "W0-Control";
      result.mixed.workload_name = "W1-MixedGameplay";
      result.write_heavy.workload_name = "W2-WriteHeavy";
      result.control.verdict = BottleneckVerdict::kInconclusive;
      result.mixed.verdict = BottleneckVerdict::kInconclusive;
      result.write_heavy.verdict = BottleneckVerdict::kInconclusive;
      result.final_verdict = BottleneckVerdict::kInconclusive;
      result.notes.push_back("Prometheus endpoint unavailable on 127.0.0.1:9091.");
      WriteStage4Reports(result);
      return result;
    }

    std::vector<SwarmClient> clients;
    if (!PrepareSwarm(&clients, runtime.swarm_clients)) {
      result.control.workload_name = "W0-Control";
      result.mixed.workload_name = "W1-MixedGameplay";
      result.write_heavy.workload_name = "W2-WriteHeavy";
      result.control.verdict = BottleneckVerdict::kInconclusive;
      result.mixed.verdict = BottleneckVerdict::kInconclusive;
      result.write_heavy.verdict = BottleneckVerdict::kInconclusive;
      result.final_verdict = BottleneckVerdict::kInconclusive;
      result.notes.push_back("Failed to prepare pressure client swarm.");
      WriteStage4Reports(result);
      return result;
    }

    const auto workloads = BuildWorkloadDefinitions();
    result.control = RunWorkload(
        workloads[0], runtime.control_steps_qps, runtime, &clients, nullptr, 0.0);

    double control_ceiling = result.control.ceiling_qps;
    if (control_ceiling <= 0.0 && !runtime.control_steps_qps.empty()) {
      control_ceiling = runtime.control_steps_qps.front();
      result.notes.push_back(
          "No healthy control step found; fallback to first control step.");
    }

    std::vector<double> business_steps;
    business_steps.reserve(runtime.business_multipliers.size());
    for (const double multiplier : runtime.business_multipliers) {
      business_steps.push_back(control_ceiling * multiplier);
    }

    result.mixed = RunWorkload(
        workloads[1], business_steps, runtime, &clients, &result.control.steps, control_ceiling);
    result.write_heavy = RunWorkload(
        workloads[2], business_steps, runtime, &clients, &result.control.steps, control_ceiling);

    if (result.mixed.verdict == BottleneckVerdict::kProved ||
        result.write_heavy.verdict == BottleneckVerdict::kProved) {
      result.final_verdict = BottleneckVerdict::kProved;
    } else if (result.mixed.verdict == BottleneckVerdict::kFalsified ||
               result.write_heavy.verdict == BottleneckVerdict::kFalsified) {
      result.final_verdict = BottleneckVerdict::kFalsified;
    } else {
      result.final_verdict = BottleneckVerdict::kInconclusive;
    }

    WriteStage4Reports(result);
    return result;
#endif
  }

  const PressureExperimentResult& EnsurePressureExperiment() {
    auto& cache = GetPressureExperimentCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (!cache.result.has_value()) {
      cache.result = ExecutePressureExperiment();
    }
    return *cache.result;
  }
};

TEST_F(GatewayLogicPressureTest, ControlCeilingHeartbeat) {
  const auto& result = EnsurePressureExperiment();
  EXPECT_FALSE(result.control.workload_name.empty());
  EXPECT_TRUE(result.control.workload_name == "W0-Control");
  EXPECT_TRUE(result.control.ceiling_qps >= 0.0);
}

TEST_F(GatewayLogicPressureTest, MixedGameplayVerdict) {
  const auto& result = EnsurePressureExperiment();
  EXPECT_EQ(result.mixed.workload_name, "W1-MixedGameplay");
  const auto verdict = result.mixed.verdict;
  EXPECT_TRUE(verdict == BottleneckVerdict::kProved ||
              verdict == BottleneckVerdict::kFalsified ||
              verdict == BottleneckVerdict::kInconclusive);
}

TEST_F(GatewayLogicPressureTest, FinalClassification) {
  const auto& result = EnsurePressureExperiment();
  EXPECT_TRUE(result.final_verdict == BottleneckVerdict::kProved ||
              result.final_verdict == BottleneckVerdict::kFalsified ||
              result.final_verdict == BottleneckVerdict::kInconclusive);

  const auto repo_root = ResolveRepoRoot();
  EXPECT_TRUE(std::filesystem::exists(repo_root / kStage4MarkdownReport));
  EXPECT_TRUE(std::filesystem::exists(repo_root / kStage4CsvReport));
}

TEST_F(GatewayLogicUdsIntegrationTest, ForwardingAndHandshakeOverUds) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  ASSERT_TRUE(WaitForCondition(
      [this]() { return logic_->last_context_request_id_.load() > 0; },
      3s,
      10ms,
      [this]() { client_->update(); }));

  ASSERT_TRUE(WaitForCondition(
      [this]() {
        return logic_->gateway_session_ != nullptr;
      },
      3s,
      10ms,
      [this]() { client_->update(); }));
  ASSERT_NE(logic_->gateway_session_, nullptr);
  EXPECT_TRUE(logic_->gateway_session_->GetRemoteAddress().empty());

  const auto login_payload = BuildLoginPayload();
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 3s);
  ASSERT_TRUE(login_rsp.has_value());
}

}  // namespace
