#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "server/common/error_codes.h"
#include "config/config_manager.h"
#include "gateway/gateway_server.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "system_generated.h"

namespace mir2::gateway {

namespace {

struct SessionPair {
    std::shared_ptr<network::TcpSession> session;
    asio::ip::tcp::socket client_socket;
};

std::string WriteTempConfig(const std::string& contents) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("gateway_integration_test_" + std::to_string(timestamp) + ".yaml");
    std::ofstream output(path, std::ios::binary);
    output << contents;
    output.close();
    return path.string();
}

SessionPair CreateSessionPair(asio::io_context& io_context, uint64_t connection_id) {
    asio::ip::tcp::acceptor acceptor(
        io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    const uint16_t port = acceptor.local_endpoint().port();

    asio::ip::tcp::socket client_socket(io_context);
    client_socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port));

    asio::ip::tcp::socket server_socket = acceptor.accept();

    auto connection = std::make_shared<network::TcpConnection>(
        std::make_unique<network::AsioSocketAdapter>(std::move(server_socket)),
        connection_id);
    auto session = std::make_shared<network::TcpSession>(connection);
    std::weak_ptr<network::TcpSession> weak_session = session;
    connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
        if (auto locked = weak_session.lock()) {
            locked->HandleBytes(data, size);
        }
    });
    connection->SetDisconnectHandler([weak_session](uint64_t id) {
        if (auto locked = weak_session.lock()) {
            locked->HandleDisconnect(id);
        }
    });
    session->Start();

    return {session, std::move(client_socket)};
}

void ReadKickMessage(asio::ip::tcp::socket& socket,
                     common::ErrorCode expected_reason,
                     const std::string& expected_text) {
    std::array<uint8_t, network::PacketHeader::kSize> header_bytes{};
    asio::error_code ec;
    asio::read(socket, asio::buffer(header_bytes), ec);
    ASSERT_FALSE(ec);

    network::PacketHeader header{};
    ASSERT_TRUE(network::PacketHeader::FromBytes(header_bytes.data(), header_bytes.size(), &header));
    EXPECT_EQ(header.msg_id, static_cast<uint16_t>(common::MsgId::kKick));
    ASSERT_GT(header.payload_size, 0u);

    std::vector<uint8_t> payload(header.payload_size);
    asio::read(socket, asio::buffer(payload), ec);
    ASSERT_FALSE(ec);

    flatbuffers::Verifier verifier(payload.data(), payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::Kick>(nullptr));

    const auto* kick = flatbuffers::GetRoot<mir2::proto::Kick>(payload.data());
    ASSERT_NE(kick, nullptr);
    EXPECT_EQ(kick->reason(),
              static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(expected_reason)));
    ASSERT_TRUE(kick->message());
    EXPECT_EQ(kick->message()->str(), expected_text);
    ASSERT_TRUE(kick->reason_text());
    EXPECT_EQ(kick->reason_text()->str(), common::ToString(expected_reason));
}

class TestGatewayServer : public GatewayServer {
 public:
    using GatewayServer::CheckHeartbeatTimeouts;
};

}  // namespace

TEST(gateway_integration_test, HeartbeatTimeoutKickCleansRoutes) {
    const int timeout_ms = 1000;
    const std::string path = WriteTempConfig("server:\n  heartbeat_timeout_ms: 1000\n");
    ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
    std::filesystem::remove(path);

    asio::io_context io_context;
    auto session_pair = CreateSessionPair(io_context, 101);
    TestGatewayServer server;

    server.RegisterConnection(101, session_pair.session);
    session_pair.session->SetAuthState(network::TcpSession::AuthState::kAuthed);
    server.RegisterUser(42, session_pair.session);

    EXPECT_EQ(server.GetConnectionRouteCount(), 1u);
    EXPECT_EQ(server.GetUserRouteCount(), 1u);

    auto work_guard = asio::make_work_guard(io_context);
    std::thread io_thread([&io_context]() { io_context.run(); });

    session_pair.session->MarkHeartbeat();
    const int64_t now_ms = session_pair.session->GetLastHeartbeatMs() + timeout_ms;
    server.CheckHeartbeatTimeouts({session_pair.session}, now_ms);

    ReadKickMessage(session_pair.client_socket,
                    common::ErrorCode::kKickHeartbeatTimeout,
                    "Heartbeat timeout");

    EXPECT_NE(session_pair.session->GetState(), network::TcpSession::SessionState::kActive);
    EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
    EXPECT_EQ(server.GetUserRouteCount(), 0u);
    EXPECT_EQ(server.GetConnectionSession(101), nullptr);
    EXPECT_EQ(server.GetUserSession(42), nullptr);
    EXPECT_EQ(session_pair.session->GetUserId(), 0u);

    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
}

TEST(gateway_integration_test, MultipleTimeoutsKickAndCleanup) {
    const int timeout_ms = 500;
    const std::string path = WriteTempConfig("server:\n  heartbeat_timeout_ms: 500\n");
    ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
    std::filesystem::remove(path);

    asio::io_context io_context;
    auto pair_a = CreateSessionPair(io_context, 201);
    auto pair_b = CreateSessionPair(io_context, 202);
    TestGatewayServer server;

    server.RegisterConnection(201, pair_a.session);
    server.RegisterConnection(202, pair_b.session);
    pair_a.session->SetAuthState(network::TcpSession::AuthState::kAuthed);
    pair_b.session->SetAuthState(network::TcpSession::AuthState::kAuthed);
    server.RegisterUser(71, pair_a.session);
    server.RegisterUser(72, pair_b.session);

    EXPECT_EQ(server.GetConnectionRouteCount(), 2u);
    EXPECT_EQ(server.GetUserRouteCount(), 2u);

    auto work_guard = asio::make_work_guard(io_context);
    std::thread io_thread([&io_context]() { io_context.run(); });

    pair_a.session->MarkHeartbeat();
    pair_b.session->MarkHeartbeat();
    const int64_t latest_heartbeat = std::max(pair_a.session->GetLastHeartbeatMs(),
                                              pair_b.session->GetLastHeartbeatMs());
    const int64_t now_ms = latest_heartbeat + timeout_ms;

    server.CheckHeartbeatTimeouts({pair_a.session, pair_b.session}, now_ms);

    ReadKickMessage(pair_a.client_socket,
                    common::ErrorCode::kKickHeartbeatTimeout,
                    "Heartbeat timeout");
    ReadKickMessage(pair_b.client_socket,
                    common::ErrorCode::kKickHeartbeatTimeout,
                    "Heartbeat timeout");

    EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
    EXPECT_EQ(server.GetUserRouteCount(), 0u);
    EXPECT_EQ(server.GetConnectionSession(201), nullptr);
    EXPECT_EQ(server.GetConnectionSession(202), nullptr);
    EXPECT_EQ(server.GetUserSession(71), nullptr);
    EXPECT_EQ(server.GetUserSession(72), nullptr);

    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
}

}  // namespace mir2::gateway
