#include "apps/storage_engine_phase6_gateway_mock_client.h"

#include <asio.hpp>
#include <flatbuffers/flatbuffers.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "common/protocol/message_codec.h"
#include "network/packet_codec.h"
#include "game_generated.h"
#include "login_generated.h"

namespace mir2::apps {
namespace {

using asio::ip::tcp;

struct Options {
  std::string gateway_host = "127.0.0.1";
  uint16_t gateway_port = 7000;
  std::string username;
  std::string password;
  std::string create_role_name;
  uint64_t player_id = 0;
  int move_x = 0;
  int move_y = 0;
  std::string pre_move_ready_file;
  std::string pre_move_continue_file;
  std::string report_file;
  uint32_t timeout_ms = 5000;
};

struct ResultSummary {
  std::string status = "fail";
  std::string login_rsp_code = "UNKNOWN";
  std::string create_role_rsp_code = "SKIPPED";
  std::string select_role_rsp_code = "UNKNOWN";
  std::string move_rsp_code = "UNKNOWN";
  std::string stage = "init";
  uint16_t last_rx_msg_id = 0;
  uint32_t last_rx_payload_size = 0;
  bool disconnect_sent = false;
  uint64_t player_id = 0;
  int move_target_x = 0;
  int move_target_y = 0;
};

std::string BuildUsage(const char* argv0) {
  const std::string program =
      (argv0 != nullptr && std::string(argv0).size() > 0)
          ? std::string(argv0)
          : "mir2_storage_engine_phase6_gateway_mock_client";
  return "Usage: " + program +
         " --gateway-host <host> --gateway-port <port> --username <name>"
         " --password <password> [--create-role-name <name>] [--player-id <id>]"
         " --move-x <x> --move-y <y>"
         " [--pre-move-ready-file <path>] [--pre-move-continue-file <path>]"
         " --report-file <path> [--timeout-ms <n>]\n";
}

std::optional<uint16_t> ParseUint16(const std::string& value) {
  try {
    return static_cast<uint16_t>(std::stoul(value));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<uint32_t> ParseUint32(const std::string& value) {
  try {
    return static_cast<uint32_t>(std::stoul(value));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<uint64_t> ParseUint64(const std::string& value) {
  try {
    return std::stoull(value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<int> ParseInt(const std::string& value) {
  try {
    return std::stoi(value);
  } catch (...) {
    return std::nullopt;
  }
}

bool ParseArgs(int argc,
               char** argv,
               Options* options,
               std::string* error) {
  if (options == nullptr || error == nullptr) {
    return false;
  }
  *error = "";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return false;
    }

    auto require_value = [&](const char* flag) -> std::optional<std::string> {
      if (i + 1 >= argc) {
        *error = std::string("missing value for ") + flag;
        return std::nullopt;
      }
      ++i;
      return std::string(argv[i]);
    };

    if (arg == "--gateway-host") {
      auto value = require_value("--gateway-host");
      if (!value.has_value()) {
        return false;
      }
      options->gateway_host = *value;
      continue;
    }
    if (arg == "--gateway-port") {
      auto value = require_value("--gateway-port");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint16(*value);
      if (!parsed.has_value()) {
        *error = "invalid --gateway-port";
        return false;
      }
      options->gateway_port = *parsed;
      continue;
    }
    if (arg == "--username") {
      auto value = require_value("--username");
      if (!value.has_value()) {
        return false;
      }
      options->username = *value;
      continue;
    }
    if (arg == "--password") {
      auto value = require_value("--password");
      if (!value.has_value()) {
        return false;
      }
      options->password = *value;
      continue;
    }
    if (arg == "--player-id") {
      auto value = require_value("--player-id");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value()) {
        *error = "invalid --player-id";
        return false;
      }
      options->player_id = *parsed;
      continue;
    }
    if (arg == "--create-role-name") {
      auto value = require_value("--create-role-name");
      if (!value.has_value()) {
        return false;
      }
      options->create_role_name = *value;
      continue;
    }
    if (arg == "--move-x") {
      auto value = require_value("--move-x");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseInt(*value);
      if (!parsed.has_value()) {
        *error = "invalid --move-x";
        return false;
      }
      options->move_x = *parsed;
      continue;
    }
    if (arg == "--move-y") {
      auto value = require_value("--move-y");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseInt(*value);
      if (!parsed.has_value()) {
        *error = "invalid --move-y";
        return false;
      }
      options->move_y = *parsed;
      continue;
    }
    if (arg == "--report-file") {
      auto value = require_value("--report-file");
      if (!value.has_value()) {
        return false;
      }
      options->report_file = *value;
      continue;
    }
    if (arg == "--pre-move-ready-file") {
      auto value = require_value("--pre-move-ready-file");
      if (!value.has_value()) {
        return false;
      }
      options->pre_move_ready_file = *value;
      continue;
    }
    if (arg == "--pre-move-continue-file") {
      auto value = require_value("--pre-move-continue-file");
      if (!value.has_value()) {
        return false;
      }
      options->pre_move_continue_file = *value;
      continue;
    }
    if (arg == "--timeout-ms") {
      auto value = require_value("--timeout-ms");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint32(*value);
      if (!parsed.has_value()) {
        *error = "invalid --timeout-ms";
        return false;
      }
      options->timeout_ms = *parsed;
      continue;
    }

    *error = "unknown argument: " + arg;
    return false;
  }

  if (options->username.empty() || options->password.empty() ||
      (options->player_id == 0 && options->create_role_name.empty()) ||
      options->report_file.empty()) {
    *error =
        "--username, --password, (--player-id or --create-role-name) and --report-file are required";
    return false;
  }
  return true;
}

std::string ErrorCodeName(mir2::proto::ErrorCode code) {
  const char* name = mir2::proto::EnumNameErrorCode(code);
  if (name != nullptr) {
    return name;
  }
  return "UNKNOWN";
}

void WriteReport(const std::string& report_file, const ResultSummary& summary) {
  const auto parent = std::filesystem::path(report_file).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(report_file, std::ios::trunc);
  out << "phase6_gateway_mock_client_result"
      << " status=" << summary.status
      << " login_rsp_code=" << summary.login_rsp_code
      << " create_role_rsp_code=" << summary.create_role_rsp_code
      << " select_role_rsp_code=" << summary.select_role_rsp_code
      << " move_rsp_code=" << summary.move_rsp_code
      << " stage=" << summary.stage
      << " last_rx_msg_id=" << summary.last_rx_msg_id
      << " last_rx_payload_size=" << summary.last_rx_payload_size
      << " disconnect_sent=" << (summary.disconnect_sent ? "true" : "false")
      << " player_id=" << summary.player_id
      << " move_target_x=" << summary.move_target_x
      << " move_target_y=" << summary.move_target_y
      << "\n";
}

void WritePreMoveReadyFile(const std::string& ready_file,
                           uint64_t player_id,
                           int move_x,
                           int move_y) {
  if (ready_file.empty()) {
    return;
  }
  const auto parent = std::filesystem::path(ready_file).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(ready_file, std::ios::trunc);
  out << "phase6_gateway_mock_client_pre_move_ready"
      << " player_id=" << player_id
      << " move_target_x=" << move_x
      << " move_target_y=" << move_y
      << "\n";
}

bool WaitForFile(const std::string& path, std::chrono::milliseconds timeout) {
  if (path.empty()) {
    return true;
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return std::filesystem::exists(path);
}

bool WriteAll(tcp::socket* socket,
              const uint8_t* data,
              size_t size,
              std::chrono::milliseconds timeout) {
  if (socket == nullptr) {
    return false;
  }
  size_t offset = 0;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (offset < size) {
    std::error_code ec;
    const size_t written = socket->write_some(
        asio::buffer(data + offset, size - offset), ec);
    if (!ec) {
      offset += written;
      continue;
    }
    if (ec == asio::error::would_block || ec == asio::error::try_again) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    return false;
  }
  return true;
}

bool ReadExact(tcp::socket* socket,
               uint8_t* data,
               size_t size,
               std::chrono::milliseconds timeout) {
  if (socket == nullptr) {
    return false;
  }
  size_t offset = 0;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (offset < size) {
    std::error_code ec;
    const size_t read =
        socket->read_some(asio::buffer(data + offset, size - offset), ec);
    if (!ec) {
      offset += read;
      continue;
    }
    if (ec == asio::error::would_block || ec == asio::error::try_again) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    return false;
  }
  return true;
}

bool SendPacket(tcp::socket* socket,
                uint16_t msg_id,
                const std::vector<uint8_t>& payload,
                uint16_t* sequence,
                std::chrono::milliseconds timeout) {
  if (socket == nullptr || sequence == nullptr) {
    return false;
  }
  const auto encoded = mir2::network::PacketCodec::EncodeV2(
      msg_id, payload.data(), payload.size(), *sequence);
  *sequence = static_cast<uint16_t>(*sequence + 1);
  return WriteAll(socket, encoded.data(), encoded.size(), timeout);
}

bool ReceivePacket(tcp::socket* socket,
                   mir2::common::NetworkPacket* packet,
                   std::chrono::milliseconds timeout) {
  if (socket == nullptr || packet == nullptr) {
    return false;
  }

  std::array<uint8_t, mir2::common::PacketHeaderV2::kSize> header_bytes{};
  if (!ReadExact(socket, header_bytes.data(), header_bytes.size(), timeout)) {
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
      !ReadExact(socket, payload.data(), payload.size(), timeout)) {
    return false;
  }
  frame.insert(frame.end(), payload.begin(), payload.end());

  return mir2::network::PacketCodec::DecodeV2(
             frame.data(), frame.size(), packet) ==
         mir2::common::DecodeStatus::kOk;
}

std::vector<uint8_t> BuildSelectRoleRequest(uint64_t player_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateSelectRoleReq(builder, player_id);
  builder.Finish(req);
  const auto* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildCreateRoleRequest(const std::string& role_name) {
  mir2::common::CreateCharacterRequest request;
  request.name = role_name;
  request.profession = mir2::proto::Profession::WARRIOR;
  request.gender = mir2::proto::Gender::MALE;
  return mir2::common::EncodeCreateCharacterRequest(request, nullptr);
}

}  // namespace

int RunStorageEnginePhase6GatewayMockClient(int argc, char** argv) {
  Options options;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &error)) {
    if (!error.empty()) {
      if (!options.report_file.empty()) {
        ResultSummary summary;
        WriteReport(options.report_file, summary);
      }
      return 1;
    }
    return 0;
  }

  ResultSummary summary;
  const auto timeout = std::chrono::milliseconds(options.timeout_ms);
  summary.move_target_x = options.move_x;
  summary.move_target_y = options.move_y;

  try {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    const auto endpoints = resolver.resolve(
        options.gateway_host, std::to_string(options.gateway_port));
    asio::connect(socket, endpoints);
    socket.non_blocking(true);

    uint16_t sequence = 0;

    mir2::common::LoginRequest login_request;
    login_request.username = options.username;
    login_request.password = options.password;
    login_request.version = std::to_string(
        static_cast<uint32_t>(mir2::proto::SchemaVersion::kSchemaVersion));
    auto login_payload =
        mir2::common::EncodeLoginRequest(login_request, nullptr);
    if (!SendPacket(&socket,
                    static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                    login_payload,
                    &sequence,
                    timeout)) {
      WriteReport(options.report_file, summary);
      return 1;
    }
    summary.stage = "wait_login_rsp";

    mir2::common::NetworkPacket packet;
    if (!ReceivePacket(&socket, &packet, timeout) ||
        packet.msg_id !=
            static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp)) {
      WriteReport(options.report_file, summary);
      return 1;
    }
    summary.last_rx_msg_id = packet.msg_id;
    summary.last_rx_payload_size = static_cast<uint32_t>(packet.payload.size());

    mir2::common::LoginResponse login_response;
    if (mir2::common::DecodeLoginResponse(
            packet.msg_id, packet.payload, &login_response) !=
        mir2::common::MessageCodecStatus::kOk) {
      WriteReport(options.report_file, summary);
      return 1;
    }
    summary.login_rsp_code = ErrorCodeName(login_response.code);
    if (login_response.code != mir2::proto::ErrorCode::ERR_OK) {
      WriteReport(options.report_file, summary);
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint64_t selected_player_id = options.player_id;
    if (!options.create_role_name.empty()) {
      if (!SendPacket(&socket,
                      static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq),
                      BuildCreateRoleRequest(options.create_role_name),
                      &sequence,
                      timeout)) {
        WriteReport(options.report_file, summary);
        return 1;
      }
      summary.stage = "wait_create_role_rsp";

      bool create_role_ok = false;
      const auto create_deadline = std::chrono::steady_clock::now() + timeout;
      while (std::chrono::steady_clock::now() < create_deadline) {
        if (!ReceivePacket(&socket, &packet, timeout)) {
          break;
        }
        summary.last_rx_msg_id = packet.msg_id;
        summary.last_rx_payload_size = static_cast<uint32_t>(packet.payload.size());
        if (packet.msg_id !=
            static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp)) {
          continue;
        }

        flatbuffers::Verifier create_verifier(packet.payload.data(),
                                              packet.payload.size());
        if (!create_verifier.VerifyBuffer<mir2::proto::CreateRoleRsp>(nullptr)) {
          break;
        }
        const auto* create_rsp =
            flatbuffers::GetRoot<mir2::proto::CreateRoleRsp>(packet.payload.data());
        summary.create_role_rsp_code =
            create_rsp != nullptr ? ErrorCodeName(create_rsp->code()) : "UNKNOWN";
        if (create_rsp == nullptr || create_rsp->code() != mir2::proto::ErrorCode::ERR_OK ||
            create_rsp->player_id() == 0) {
          WriteReport(options.report_file, summary);
          return 1;
        }
        selected_player_id = create_rsp->player_id();
        create_role_ok = true;
        break;
      }
      if (!create_role_ok) {
        WriteReport(options.report_file, summary);
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!SendPacket(&socket,
                    static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
                    BuildSelectRoleRequest(selected_player_id),
                    &sequence,
                    timeout)) {
      WriteReport(options.report_file, summary);
      return 1;
    }
    summary.stage = "wait_select_role_rsp";

    bool select_role_ok = false;
    const auto select_deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < select_deadline) {
      if (!ReceivePacket(&socket, &packet, timeout)) {
        break;
      }
      summary.last_rx_msg_id = packet.msg_id;
      summary.last_rx_payload_size = static_cast<uint32_t>(packet.payload.size());

      if (packet.msg_id ==
          static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp)) {
        flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
        if (!verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr)) {
          break;
        }
        const auto* rsp =
            flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(packet.payload.data());
        summary.select_role_rsp_code = ErrorCodeName(rsp->code());
        select_role_ok = rsp->code() == mir2::proto::ErrorCode::ERR_OK;
        if (!select_role_ok) {
          WriteReport(options.report_file, summary);
          return 1;
        }
        continue;
      }

      if (packet.msg_id ==
          static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp)) {
        flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
        if (!verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr)) {
          break;
        }
        const auto* rsp =
            flatbuffers::GetRoot<mir2::proto::EnterGameRsp>(packet.payload.data());
        if (rsp == nullptr || rsp->code() != mir2::proto::ErrorCode::ERR_OK) {
          WriteReport(options.report_file, summary);
          return 1;
        }
        if (select_role_ok) {
          break;
        }
      }
    }

    if (!select_role_ok) {
      WriteReport(options.report_file, summary);
      return 1;
    }

    summary.player_id = selected_player_id;
    WritePreMoveReadyFile(options.pre_move_ready_file,
                          selected_player_id,
                          options.move_x,
                          options.move_y);
    if (!options.pre_move_continue_file.empty()) {
      summary.stage = "wait_pre_move_continue";
      if (!WaitForFile(options.pre_move_continue_file, timeout)) {
        WriteReport(options.report_file, summary);
        return 1;
      }
    }

    mir2::common::MoveRequest move_request;
    move_request.target_x = options.move_x;
    move_request.target_y = options.move_y;
    auto move_payload = mir2::common::EncodeMoveRequest(move_request, nullptr);
    if (!SendPacket(&socket,
                    static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
                    move_payload,
                    &sequence,
                    timeout)) {
      WriteReport(options.report_file, summary);
      return 1;
    }
    summary.stage = "wait_move_rsp";

    bool move_ok = false;
    const auto move_deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < move_deadline) {
      if (!ReceivePacket(&socket, &packet, timeout)) {
        break;
      }
      summary.last_rx_msg_id = packet.msg_id;
      summary.last_rx_payload_size = static_cast<uint32_t>(packet.payload.size());
      if (packet.msg_id !=
          static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp)) {
        continue;
      }

      mir2::common::MoveResponse move_response;
      if (mir2::common::DecodeMoveResponse(
              packet.msg_id, packet.payload, &move_response) !=
          mir2::common::MessageCodecStatus::kOk) {
        break;
      }
      summary.move_rsp_code = ErrorCodeName(move_response.code);
      move_ok = move_response.code == mir2::proto::ErrorCode::ERR_OK;
      break;
    }

    if (!move_ok) {
      WriteReport(options.report_file, summary);
      return 1;
    }

    std::error_code close_ec;
    socket.close(close_ec);
    summary.disconnect_sent = !close_ec;
    summary.status = summary.disconnect_sent ? "ok" : "fail";
    summary.stage = "done";
    WriteReport(options.report_file, summary);
    return summary.disconnect_sent ? 0 : 1;
  } catch (...) {
    WriteReport(options.report_file, summary);
    return 1;
  }
}

}  // namespace mir2::apps
