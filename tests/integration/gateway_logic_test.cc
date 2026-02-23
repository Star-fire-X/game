#include <gtest/gtest.h>

#include <asio.hpp>
#include <entt/entt.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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
#include "integration/test_helpers.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/registry_manager.h"
#include "logic/services/session_role_store.h"
#include "auction_generated.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "guild_generated.h"
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
      << "  max_connections: 128\n"
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
      << "  max_connections: 128\n"
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
    state.position = {100, 100};

    auto& attributes =
        registry->get_or_emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.max_hp = std::max(attributes.max_hp, 200);
    attributes.hp = std::max(attributes.hp, 200);
    attributes.max_mp = std::max(attributes.max_mp, 120);
    attributes.mp = std::max(attributes.mp, 120);
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
