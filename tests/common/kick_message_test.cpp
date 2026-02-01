#include <gtest/gtest.h>

#include <array>
#include <thread>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "server/common/error_codes.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "system_generated.h"

namespace mir2::network {

namespace {

struct SessionPair {
    std::shared_ptr<TcpSession> session;
    asio::ip::tcp::socket client_socket;
};

SessionPair CreateSessionPair(asio::io_context& io_context) {
    asio::ip::tcp::acceptor acceptor(
        io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    const uint16_t port = acceptor.local_endpoint().port();

    asio::ip::tcp::socket client_socket(io_context);
    client_socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port));

    asio::ip::tcp::socket server_socket = acceptor.accept();

    auto connection = std::make_shared<TcpConnection>(
        std::make_unique<AsioSocketAdapter>(std::move(server_socket)),
        1);
    auto session = std::make_shared<TcpSession>(connection);
    std::weak_ptr<TcpSession> weak_session = session;
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

}  // namespace

TEST(KickMessageTest, KickSendsMessageAndCloses) {
    asio::io_context io_context;
    auto session_pair = CreateSessionPair(io_context);

    auto work_guard = asio::make_work_guard(io_context);
    std::thread io_thread([&io_context]() { io_context.run(); });

    session_pair.session->Kick(mir2::common::ErrorCode::kKickHeartbeatTimeout,
                               "Heartbeat timeout");
    EXPECT_NE(session_pair.session->GetState(), TcpSession::SessionState::kActive);

    std::array<uint8_t, PacketHeader::kSize> header_bytes{};
    asio::error_code ec;
    asio::read(session_pair.client_socket, asio::buffer(header_bytes), ec);
    ASSERT_FALSE(ec);

    PacketHeader header{};
    ASSERT_TRUE(PacketHeader::FromBytes(header_bytes.data(), header_bytes.size(), &header));
    EXPECT_EQ(header.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kKick));
    ASSERT_GT(header.payload_size, 0u);

    std::vector<uint8_t> payload(header.payload_size);
    asio::read(session_pair.client_socket, asio::buffer(payload), ec);
    ASSERT_FALSE(ec);

    flatbuffers::Verifier verifier(payload.data(), payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::Kick>(nullptr));

    const auto* kick = flatbuffers::GetRoot<mir2::proto::Kick>(payload.data());
    ASSERT_NE(kick, nullptr);
    EXPECT_EQ(kick->reason(),
              static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(
                  mir2::common::ErrorCode::kKickHeartbeatTimeout)));
    ASSERT_TRUE(kick->message());
    EXPECT_EQ(kick->message()->str(), "Heartbeat timeout");
    ASSERT_TRUE(kick->reason_text());
    EXPECT_EQ(kick->reason_text()->str(), "Heartbeat timeout");

    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
}

}  // namespace mir2::network
