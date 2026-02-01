#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <chrono>
#include <vector>

#include "common/enums.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "tests/mocks/mock_socket.h"

#define private public
#include "gateway/gateway_server.h"
#include "network/tcp_client.h"
#undef private

namespace mir2::gateway {

namespace {

struct SessionBundle {
  std::shared_ptr<network::TcpSession> session;
};

SessionBundle CreateSession(asio::io_context& io_context, uint64_t connection_id) {
  auto mock_socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), connection_id);
  auto session = std::make_shared<network::TcpSession>(connection);
  return {session};
}

struct ClientBundle {
  std::unique_ptr<network::TcpClient> client;
};

ClientBundle CreateMockClient(asio::io_context& io_context) {
  auto client = std::make_unique<network::TcpClient>(io_context);
  auto mock_socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), 1);
  client->connection_ = connection;
  client->connected_.store(true);
  return {std::move(client)};
}

void DrainIoContext(asio::io_context& io_context) {
  while (io_context.poll_one() > 0) {
  }
  io_context.restart();
}

class TestGatewayServer : public GatewayServer {
 public:
  using GatewayServer::CheckHeartbeatTimeouts;
};

}  // namespace

// Micro-benchmarks with generous thresholds to avoid flakiness.
TEST(GatewayPerformanceTest, RouteTableLookup_Latency) {
  asio::io_context io_context;
  GatewayServer server;
  constexpr int kSessionCount = 5000;
  std::vector<std::shared_ptr<network::TcpSession>> sessions;
  sessions.reserve(kSessionCount);
  for (int i = 0; i < kSessionCount; ++i) {
    auto session = CreateSession(io_context, 10000 + i).session;
    sessions.push_back(session);
    server.RegisterConnection(10000 + i, session);
  }

  constexpr int kLookups = 100000;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < kLookups; ++i) {
    server.GetConnectionSession(10000 + (i % kSessionCount));
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto avg_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / kLookups;

  EXPECT_LT(avg_ns, 2000000) << "Average lookup latency exceeds 2ms";
}

TEST(GatewayPerformanceTest, MessageForwarding_ThroughputBaseline) {
  asio::io_context io_context;
  GatewayServer server;

  auto game_client = CreateMockClient(io_context);
  server.game_client_ = std::move(game_client.client);

  constexpr int kMessages = 10000;
  const std::vector<uint8_t> payload{1, 2, 3, 4};

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < kMessages; ++i) {
    server.ForwardToService(common::ServiceType::kGame,
                            static_cast<uint64_t>(i + 1),
                            static_cast<uint16_t>(common::MsgId::kMoveReq),
                            payload);
  }
  DrainIoContext(io_context);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
  const auto throughput = seconds > 0.0 ? kMessages / seconds : 0.0;

  EXPECT_GT(throughput, 1000.0) << "Throughput too low for baseline";
}

TEST(GatewayPerformanceTest, MemoryUsage_10kConnections) {
  GTEST_SKIP() << "Memory usage tracking is not available in unit tests.";
}

TEST(GatewayPerformanceTest, HeartbeatTimeout_CheckCost) {
  TestGatewayServer server;
  std::vector<std::shared_ptr<network::TcpSession>> sessions;
  asio::io_context io_context;

  constexpr int kSessionCount = 1000;
  sessions.reserve(kSessionCount);
  for (int i = 0; i < kSessionCount; ++i) {
    sessions.push_back(CreateSession(io_context, 20000 + i).session);
  }

  const int64_t now_ms = network::TcpSession::NowMs();
  const auto start = std::chrono::steady_clock::now();
  server.CheckHeartbeatTimeouts(sessions, now_ms);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 50) << "Heartbeat check exceeds 50ms";
}

}  // namespace mir2::gateway
