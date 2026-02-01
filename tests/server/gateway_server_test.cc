#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "gateway/gateway_server.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "tests/mocks/mock_socket.h"

namespace mir2::gateway {

namespace {

struct SessionBundle {
  std::shared_ptr<network::TcpSession> session;
  network::MockSocket* socket = nullptr;
};

SessionBundle CreateSession(asio::io_context& io_context, uint64_t connection_id) {
  auto mock_socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto* socket_ptr = mock_socket.get();
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), connection_id);
  auto session = std::make_shared<network::TcpSession>(connection);
  return {session, socket_ptr};
}

}  // namespace

// Connection management.
TEST(GatewayServerTest, RegisterConnectionAddsToTable) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 101);

  server.RegisterConnection(101, bundle.session);

  EXPECT_EQ(server.GetConnectionRouteCount(), 1u);
  EXPECT_EQ(server.GetConnectionSession(101), bundle.session);
}

TEST(GatewayServerTest, RegisterConnectionReplacesExisting) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle_a = CreateSession(io_context, 101);
  auto bundle_b = CreateSession(io_context, 101);

  server.RegisterConnection(101, bundle_a.session);
  server.RegisterConnection(101, bundle_b.session);

  EXPECT_EQ(server.GetConnectionRouteCount(), 1u);
  EXPECT_EQ(server.GetConnectionSession(101), bundle_b.session);
}

TEST(GatewayServerTest, UnregisterSessionRemovesFromConnectionTable) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 202);

  server.RegisterConnection(202, bundle.session);
  server.UnregisterSession(bundle.session);

  EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
  EXPECT_EQ(server.GetConnectionSession(202), nullptr);
}

TEST(GatewayServerTest, UnregisterSessionRemovesFromUserTable) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 203);

  bundle.session->SetAuthState(network::TcpSession::AuthState::kAuthed);
  server.RegisterUser(77, bundle.session);
  server.UnregisterSession(bundle.session);

  EXPECT_EQ(server.GetUserRouteCount(), 0u);
  EXPECT_EQ(server.GetUserSession(77), nullptr);
}

TEST(GatewayServerTest, UnregisterSessionHandlesNonExistent) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 204);

  server.UnregisterSession(bundle.session);

  EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
  EXPECT_EQ(server.GetUserRouteCount(), 0u);
}

TEST(GatewayServerTest, GetConnectionSessionReturnsNullForMissing) {
  GatewayServer server;
  EXPECT_EQ(server.GetConnectionSession(999), nullptr);
}

TEST(GatewayServerTest, GetUserSessionReturnsNullForMissing) {
  GatewayServer server;
  EXPECT_EQ(server.GetUserSession(999), nullptr);
}

// Route cleanup behavior.
TEST(GatewayServerTest, CleanupStaleRoutesRemovesDisconnectedSessions) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 301);

  server.RegisterConnection(301, bundle.session);
  bundle.session->SetAuthState(network::TcpSession::AuthState::kAuthed);
  server.RegisterUser(55, bundle.session);

  bundle.session->HandleDisconnect(301);
  server.CleanupStaleRoutes();

  EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
  EXPECT_EQ(server.GetUserRouteCount(), 0u);
  EXPECT_EQ(bundle.session->GetUserId(), 0u);
}

TEST(GatewayServerTest, CleanupStaleRoutesPreservesActiveSessions) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 302);

  server.RegisterConnection(302, bundle.session);
  bundle.session->SetAuthState(network::TcpSession::AuthState::kAuthed);
  server.RegisterUser(56, bundle.session);

  server.CleanupStaleRoutes();

  EXPECT_EQ(server.GetConnectionRouteCount(), 1u);
  EXPECT_EQ(server.GetUserRouteCount(), 1u);
}

// Concurrency safety checks.
TEST(GatewayServerTest, CleanupStaleRoutesThreadSafe) {
  asio::io_context io_context;
  GatewayServer server;
  std::vector<std::shared_ptr<network::TcpSession>> sessions;
  sessions.reserve(32);
  for (uint64_t i = 0; i < 32; ++i) {
    sessions.push_back(CreateSession(io_context, 400 + i).session);
    server.RegisterConnection(400 + i, sessions.back());
  }

  std::atomic<bool> stop{false};
  std::thread cleaner([&]() {
    while (!stop.load()) {
      server.CleanupStaleRoutes();
    }
  });
  std::thread reader([&]() {
    while (!stop.load()) {
      server.GetConnectionSession(400);
      server.GetConnectionRouteCount();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop.store(true);
  cleaner.join();
  reader.join();

  EXPECT_EQ(server.GetConnectionRouteCount(), 32u);
}

TEST(GatewayServerTest, ConcurrentConnectionRegistration) {
  asio::io_context io_context;
  GatewayServer server;
  constexpr int kThreadCount = 120;
  std::vector<std::shared_ptr<network::TcpSession>> sessions;
  sessions.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    sessions.push_back(CreateSession(io_context, 1000 + i).session);
  }

  std::atomic<int> ready{0};
  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&server, &sessions, &ready, &start, i]() {
      ready.fetch_add(1, std::memory_order_relaxed);
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      server.RegisterConnection(1000 + i, sessions[static_cast<size_t>(i)]);
    });
  }

  while (ready.load(std::memory_order_acquire) < kThreadCount) {
    std::this_thread::yield();
  }
  start.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(server.GetConnectionRouteCount(), static_cast<size_t>(kThreadCount));
}

TEST(GatewayServerTest, ConcurrentUserRegistration) {
  asio::io_context io_context;
  GatewayServer server;
  constexpr int kThreadCount = 64;
  std::vector<std::shared_ptr<network::TcpSession>> sessions;
  sessions.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    auto session = CreateSession(io_context, 2000 + i).session;
    session->SetAuthState(network::TcpSession::AuthState::kAuthed);
    sessions.push_back(session);
  }

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&server, &sessions, i]() {
      server.RegisterUser(3000 + i, sessions[static_cast<size_t>(i)]);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(server.GetUserRouteCount(), static_cast<size_t>(kThreadCount));
}

TEST(GatewayServerTest, ConcurrentRouteTableAccess) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 5000);
  bundle.session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  std::atomic<bool> stop{false};
  std::thread writer([&]() {
    while (!stop.load()) {
      server.RegisterConnection(5000, bundle.session);
      server.RegisterUser(6000, bundle.session);
      server.UnregisterSession(bundle.session);
    }
  });
  std::thread reader([&]() {
    while (!stop.load()) {
      server.GetConnectionSession(5000);
      server.GetUserSession(6000);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop.store(true);
  writer.join();
  reader.join();

  SUCCEED();
}

// Boundary conditions.
TEST(GatewayServerTest, RegisterConnectionWithZeroId) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 0);

  server.RegisterConnection(0, bundle.session);

  EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
}

TEST(GatewayServerTest, RegisterUserWithZeroId) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 6001);
  bundle.session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  server.RegisterUser(0, bundle.session);

  EXPECT_EQ(server.GetUserRouteCount(), 0u);
}

TEST(GatewayServerTest, UnregisterNullSession) {
  GatewayServer server;
  server.UnregisterSession(nullptr);
  EXPECT_EQ(server.GetConnectionRouteCount(), 0u);
  EXPECT_EQ(server.GetUserRouteCount(), 0u);
}

TEST(GatewayServerTest, RouteCountsAccurate) {
  asio::io_context io_context;
  GatewayServer server;
  auto session_a = CreateSession(io_context, 7001).session;
  auto session_b = CreateSession(io_context, 7002).session;

  server.RegisterConnection(7001, session_a);
  server.RegisterConnection(7002, session_b);
  session_a->SetAuthState(network::TcpSession::AuthState::kAuthed);
  session_b->SetAuthState(network::TcpSession::AuthState::kAuthed);
  server.RegisterUser(8001, session_a);
  server.RegisterUser(8002, session_b);

  EXPECT_EQ(server.GetConnectionRouteCount(), 2u);
  EXPECT_EQ(server.GetUserRouteCount(), 2u);
}

}  // namespace mir2::gateway
