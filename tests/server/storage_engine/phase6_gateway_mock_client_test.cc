#include <gtest/gtest.h>

#include <asio.hpp>
#include <flatbuffers/flatbuffers.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "common/protocol/message_codec.h"
#include "network/packet_codec.h"
#include "game_generated.h"
#include "login_generated.h"

namespace mir2::apps {
int RunStorageEnginePhase6GatewayMockClient(int argc, char** argv);
}  // namespace mir2::apps

namespace mir2::storage_engine::phase6_gateway_mock_client_test {
namespace {

using namespace std::chrono_literals;

std::filesystem::path MakeTempDir(std::string_view suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path =
      std::filesystem::path("/tmp") /
      ("mir2_phase6_gateway_mock_client_test_" + std::string(suffix) + "_" +
       std::to_string(stamp));
  std::filesystem::create_directories(path);
  return path;
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

bool WaitForPath(const std::filesystem::path& path,
                 std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path)) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return std::filesystem::exists(path);
}

bool ReadExact(asio::ip::tcp::socket& socket, uint8_t* data, size_t size) {
  size_t offset = 0;
  std::error_code ec;
  while (offset < size) {
    const size_t read = socket.read_some(
        asio::buffer(data + offset, size - offset), ec);
    if (ec) {
      return false;
    }
    offset += read;
  }
  return true;
}

bool ReadPacket(asio::ip::tcp::socket& socket,
                mir2::common::NetworkPacket* packet) {
  std::array<uint8_t, mir2::common::PacketHeaderV2::kSize> header_bytes{};
  if (!ReadExact(socket, header_bytes.data(), header_bytes.size())) {
    return false;
  }

  mir2::common::PacketHeaderV2 header;
  if (!mir2::common::PacketHeaderV2::FromBytes(
          header_bytes.data(), header_bytes.size(), &header)) {
    return false;
  }

  std::vector<uint8_t> frame(header_bytes.begin(), header_bytes.end());
  std::vector<uint8_t> payload(header.payload_size);
  if (header.payload_size > 0 &&
      !ReadExact(socket, payload.data(), payload.size())) {
    return false;
  }
  frame.insert(frame.end(), payload.begin(), payload.end());

  return mir2::network::PacketCodec::DecodeV2(
             frame.data(), frame.size(), packet) ==
         mir2::common::DecodeStatus::kOk;
}

void WritePacket(asio::ip::tcp::socket& socket,
                 uint16_t msg_id,
                 const std::vector<uint8_t>& payload,
                 uint16_t sequence) {
  const auto encoded = mir2::network::PacketCodec::EncodeV2(
      msg_id, payload.data(), payload.size(), sequence);
  asio::write(socket, asio::buffer(encoded));
}

bool AcceptWithTimeout(asio::ip::tcp::acceptor* acceptor,
                       asio::ip::tcp::socket* socket,
                       std::chrono::milliseconds timeout) {
  if (acceptor == nullptr || socket == nullptr) {
    return false;
  }
  acceptor->non_blocking(true);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code ec;
    acceptor->accept(*socket, ec);
    if (!ec) {
      socket->non_blocking(false);
      return true;
    }
    if (ec == asio::error::would_block || ec == asio::error::try_again) {
      std::this_thread::sleep_for(10ms);
      continue;
    }
    return false;
  }
  return false;
}

std::vector<uint8_t> BuildCreateRoleRsp(uint64_t player_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateCreateRoleRsp(
      builder, mir2::proto::ErrorCode::ERR_OK, player_id);
  builder.Finish(rsp);
  const auto* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildSelectRoleRsp(uint64_t player_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateSelectRoleRsp(
      builder, mir2::proto::ErrorCode::ERR_OK, player_id);
  builder.Finish(rsp);
  const auto* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildEnterGameRsp(uint64_t player_id,
                                       int x,
                                       int y) {
  flatbuffers::FlatBufferBuilder builder;
  const auto name = builder.CreateString("phase6_mock_player");
  const auto player = mir2::proto::CreatePlayerInfo(
      builder,
      player_id,
      name,
      mir2::proto::Profession::WARRIOR,
      10,
      100,
      100,
      50,
      50,
      1,
      x,
      y,
      999);
  const auto rsp = mir2::proto::CreateEnterGameRsp(
      builder, mir2::proto::ErrorCode::ERR_OK, player);
  builder.Finish(rsp);
  const auto* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

void RunMockGatewayServer(uint16_t port, std::atomic<bool>* ready) {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(
      io_context,
      asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
  if (ready != nullptr) {
    ready->store(true, std::memory_order_release);
  }
  asio::ip::tcp::socket socket(io_context);
  if (!AcceptWithTimeout(&acceptor, &socket, 2s)) {
    return;
  }

  mir2::common::NetworkPacket packet;

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kLoginReq));

  mir2::common::LoginRequest login_request;
  ASSERT_EQ(mir2::common::DecodeLoginRequest(
                packet.msg_id, packet.payload, &login_request),
            mir2::common::MessageCodecStatus::kOk);

  mir2::common::LoginResponse login_response;
  login_response.code = mir2::proto::ErrorCode::ERR_OK;
  login_response.account_id = 7001;
  login_response.session_token = "phase6_token";
  auto login_payload =
      mir2::common::EncodeLoginResponse(login_response, nullptr);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
              login_payload,
              1);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq));
  flatbuffers::Verifier select_verifier(packet.payload.data(),
                                        packet.payload.size());
  ASSERT_TRUE(select_verifier.VerifyBuffer<mir2::proto::SelectRoleReq>(nullptr));
  const auto* select_req =
      flatbuffers::GetRoot<mir2::proto::SelectRoleReq>(packet.payload.data());
  ASSERT_NE(select_req, nullptr);

  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
              BuildSelectRoleRsp(select_req->player_id()),
              2);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
              BuildEnterGameRsp(select_req->player_id(), 10, 20),
              3);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));

  mir2::common::MoveRequest move_request;
  ASSERT_EQ(mir2::common::DecodeMoveRequest(
                packet.msg_id, packet.payload, &move_request),
            mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(move_request.target_x, 77);
  EXPECT_EQ(move_request.target_y, 66);

  mir2::common::MoveResponse move_response;
  move_response.code = mir2::proto::ErrorCode::ERR_OK;
  move_response.x = move_request.target_x;
  move_response.y = move_request.target_y;
  auto move_payload =
      mir2::common::EncodeMoveResponse(move_response, nullptr);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp),
              move_payload,
              4);
}

void RunMockGatewayServerWithCreateRole(uint16_t port, std::atomic<bool>* ready) {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(
      io_context,
      asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
  if (ready != nullptr) {
    ready->store(true, std::memory_order_release);
  }
  asio::ip::tcp::socket socket(io_context);
  if (!AcceptWithTimeout(&acceptor, &socket, 2s)) {
    return;
  }

  mir2::common::NetworkPacket packet;

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kLoginReq));
  mir2::common::LoginResponse login_response;
  login_response.code = mir2::proto::ErrorCode::ERR_OK;
  login_response.account_id = 7001;
  login_response.session_token = "phase6_token";
  auto login_payload =
      mir2::common::EncodeLoginResponse(login_response, nullptr);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
              login_payload,
              1);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq));
  flatbuffers::Verifier create_verifier(packet.payload.data(),
                                        packet.payload.size());
  ASSERT_TRUE(create_verifier.VerifyBuffer<mir2::proto::CreateRoleReq>(nullptr));

  constexpr uint64_t kCreatedPlayerId = 9101;
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
              BuildCreateRoleRsp(kCreatedPlayerId),
              2);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq));
  flatbuffers::Verifier select_verifier(packet.payload.data(),
                                        packet.payload.size());
  ASSERT_TRUE(select_verifier.VerifyBuffer<mir2::proto::SelectRoleReq>(nullptr));
  const auto* select_req =
      flatbuffers::GetRoot<mir2::proto::SelectRoleReq>(packet.payload.data());
  ASSERT_NE(select_req, nullptr);
  EXPECT_EQ(select_req->player_id(), kCreatedPlayerId);

  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
              BuildSelectRoleRsp(kCreatedPlayerId),
              3);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
              BuildEnterGameRsp(kCreatedPlayerId, 10, 20),
              4);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));
  mir2::common::MoveRequest move_request;
  ASSERT_EQ(mir2::common::DecodeMoveRequest(
                packet.msg_id, packet.payload, &move_request),
            mir2::common::MessageCodecStatus::kOk);

  mir2::common::MoveResponse move_response;
  move_response.code = mir2::proto::ErrorCode::ERR_OK;
  move_response.x = move_request.target_x;
  move_response.y = move_request.target_y;
  auto move_payload =
      mir2::common::EncodeMoveResponse(move_response, nullptr);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp),
              move_payload,
              5);
}

void RunMockGatewayServerWithCreateRoleBarrier(
    uint16_t port,
    std::atomic<bool>* ready,
    const std::filesystem::path& continue_file,
    std::atomic<bool>* move_seen_before_continue) {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(
      io_context,
      asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
  if (ready != nullptr) {
    ready->store(true, std::memory_order_release);
  }
  asio::ip::tcp::socket socket(io_context);
  if (!AcceptWithTimeout(&acceptor, &socket, 2s)) {
    return;
  }

  mir2::common::NetworkPacket packet;

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kLoginReq));
  mir2::common::LoginResponse login_response;
  login_response.code = mir2::proto::ErrorCode::ERR_OK;
  login_response.account_id = 7001;
  login_response.session_token = "phase6_token";
  auto login_payload =
      mir2::common::EncodeLoginResponse(login_response, nullptr);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
              login_payload,
              1);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq));

  constexpr uint64_t kCreatedPlayerId = 9101;
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
              BuildCreateRoleRsp(kCreatedPlayerId),
              2);

  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq));
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
              BuildSelectRoleRsp(kCreatedPlayerId),
              3);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
              BuildEnterGameRsp(kCreatedPlayerId, 10, 20),
              4);

  const auto early_deadline = std::chrono::steady_clock::now() + 300ms;
  while (std::chrono::steady_clock::now() < early_deadline &&
         !std::filesystem::exists(continue_file)) {
    std::error_code ec;
    if (socket.available(ec) > 0) {
      if (move_seen_before_continue != nullptr) {
        move_seen_before_continue->store(true, std::memory_order_release);
      }
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  ASSERT_TRUE(WaitForPath(continue_file, 2s));
  ASSERT_TRUE(ReadPacket(socket, &packet));
  ASSERT_EQ(packet.msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));

  mir2::common::MoveRequest move_request;
  ASSERT_EQ(mir2::common::DecodeMoveRequest(
                packet.msg_id, packet.payload, &move_request),
            mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(move_request.target_x, 77);
  EXPECT_EQ(move_request.target_y, 66);

  mir2::common::MoveResponse move_response;
  move_response.code = mir2::proto::ErrorCode::ERR_OK;
  move_response.x = move_request.target_x;
  move_response.y = move_request.target_y;
  auto move_payload =
      mir2::common::EncodeMoveResponse(move_response, nullptr);
  WritePacket(socket,
              static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp),
              move_payload,
              5);
}

TEST(Phase6GatewayMockClientTest,
     PerformsLoginSelectMoveRoundTripAndWritesReport) {
  const auto temp_dir = MakeTempDir("round_trip");
  const auto report_file = temp_dir / "client.report.txt";
  constexpr uint16_t kPort = 39123;
  std::atomic<bool> server_ready{false};

  std::jthread server_thread([&]() { RunMockGatewayServer(kPort, &server_ready); });
  while (!server_ready.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(10ms);
  }

  std::vector<std::string> args = {
      "mir2_storage_engine_phase6_gateway_mock_client",
      "--gateway-host",
      "127.0.0.1",
      "--gateway-port",
      std::to_string(kPort),
      "--username",
      "phase6_user",
      "--password",
      "phase6_pw",
      "--player-id",
      "8101",
      "--move-x",
      "77",
      "--move-y",
      "66",
      "--report-file",
      report_file.string(),
  };
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }

  const int exit_code = mir2::apps::RunStorageEnginePhase6GatewayMockClient(
      static_cast<int>(argv.size()), argv.data());
  EXPECT_EQ(exit_code, 0);
  const auto report = ReadTextFile(report_file);
  EXPECT_NE(report.find("phase6_gateway_mock_client_result status=ok"),
            std::string::npos);
  EXPECT_NE(report.find("login_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("create_role_rsp_code=SKIPPED"), std::string::npos);
  EXPECT_NE(report.find("select_role_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("move_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("disconnect_sent=true"), std::string::npos);
  EXPECT_NE(report.find("stage=done"), std::string::npos);
}

TEST(Phase6GatewayMockClientTest,
     PerformsLoginCreateRoleSelectMoveRoundTripWhenRequested) {
  const auto temp_dir = MakeTempDir("create_role_round_trip");
  const auto report_file = temp_dir / "client.report.txt";
  constexpr uint16_t kPort = 39124;
  std::atomic<bool> server_ready{false};

  std::jthread server_thread([&]() {
    RunMockGatewayServerWithCreateRole(kPort, &server_ready);
  });
  while (!server_ready.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(10ms);
  }

  std::vector<std::string> args = {
      "mir2_storage_engine_phase6_gateway_mock_client",
      "--gateway-host",
      "127.0.0.1",
      "--gateway-port",
      std::to_string(kPort),
      "--username",
      "phase6_user",
      "--password",
      "phase6_pw",
      "--create-role-name",
      "phase6_role",
      "--move-x",
      "77",
      "--move-y",
      "66",
      "--report-file",
      report_file.string(),
  };
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }

  const int exit_code = mir2::apps::RunStorageEnginePhase6GatewayMockClient(
      static_cast<int>(argv.size()), argv.data());
  EXPECT_EQ(exit_code, 0);
  const auto report = ReadTextFile(report_file);
  EXPECT_NE(report.find("phase6_gateway_mock_client_result status=ok"),
            std::string::npos);
  EXPECT_NE(report.find("login_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("create_role_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("select_role_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("move_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("stage=done"), std::string::npos);
}

TEST(Phase6GatewayMockClientTest,
     WritesPreMoveReadyFileAndWaitsForContinueBeforeMoveRequest) {
  const auto temp_dir = MakeTempDir("pre_move_barrier");
  const auto report_file = temp_dir / "client.report.txt";
  const auto ready_file = temp_dir / "pre_move.ready";
  const auto continue_file = temp_dir / "pre_move.continue";
  constexpr uint16_t kPort = 39125;
  std::atomic<bool> server_ready{false};
  std::atomic<bool> move_seen_before_continue{false};

  std::jthread server_thread([&]() {
    RunMockGatewayServerWithCreateRoleBarrier(
        kPort, &server_ready, continue_file, &move_seen_before_continue);
  });
  while (!server_ready.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(10ms);
  }

  std::vector<std::string> args = {
      "mir2_storage_engine_phase6_gateway_mock_client",
      "--gateway-host",
      "127.0.0.1",
      "--gateway-port",
      std::to_string(kPort),
      "--username",
      "phase6_user",
      "--password",
      "phase6_pw",
      "--create-role-name",
      "phase6_role",
      "--move-x",
      "77",
      "--move-y",
      "66",
      "--pre-move-ready-file",
      ready_file.string(),
      "--pre-move-continue-file",
      continue_file.string(),
      "--report-file",
      report_file.string(),
  };
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }

  std::promise<int> exit_code_promise;
  auto exit_code_future = exit_code_promise.get_future();
  std::thread client_thread([&]() {
    exit_code_promise.set_value(mir2::apps::RunStorageEnginePhase6GatewayMockClient(
        static_cast<int>(argv.size()), argv.data()));
  });

  ASSERT_TRUE(WaitForPath(ready_file, 2s));
  EXPECT_NE(ReadTextFile(ready_file).find("player_id=9101"), std::string::npos);
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(move_seen_before_continue.load(std::memory_order_acquire));

  std::ofstream continue_out(continue_file);
  ASSERT_TRUE(continue_out.is_open());
  continue_out << "continue\n";
  continue_out.close();

  ASSERT_EQ(exit_code_future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(exit_code_future.get(), 0);
  client_thread.join();

  const auto report = ReadTextFile(report_file);
  EXPECT_NE(report.find("phase6_gateway_mock_client_result status=ok"),
            std::string::npos);
  EXPECT_NE(report.find("create_role_rsp_code=ERR_OK"), std::string::npos);
  EXPECT_NE(report.find("player_id=9101"), std::string::npos);
  EXPECT_NE(report.find("move_rsp_code=ERR_OK"), std::string::npos);
}

}  // namespace
}  // namespace mir2::storage_engine::phase6_gateway_mock_client_test
