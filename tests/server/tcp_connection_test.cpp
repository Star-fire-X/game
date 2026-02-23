#include <gtest/gtest.h>

#include <asio/error.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <atomic>
#include <cstring>
#include <thread>

#include "common/enums.h"
#include "mocks/mock_socket.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"

namespace mir2::network {

namespace {

std::vector<uint8_t> BuildLegacyV1Packet(uint16_t msg_id) {
  std::vector<uint8_t> bytes(10, 0);
  constexpr uint32_t kLegacyV1Magic = 0x4D495232;  // "MIR2"
  constexpr uint32_t kPayloadSize = 0;
  std::memcpy(bytes.data(), &kLegacyV1Magic, sizeof(kLegacyV1Magic));
  std::memcpy(bytes.data() + sizeof(kLegacyV1Magic), &msg_id, sizeof(msg_id));
  std::memcpy(bytes.data() + sizeof(kLegacyV1Magic) + sizeof(msg_id),
              &kPayloadSize,
              sizeof(kPayloadSize));
  return bytes;
}

std::shared_ptr<TcpConnection> CreateConnection(asio::io_context& io_context,
                                                MockSocket** out_socket,
                                                size_t max_write_queue_size =
                                                    TcpConnection::kDefaultMaxWriteQueueSize) {
  auto mock_socket = std::make_unique<MockSocket>(io_context.get_executor());
  if (out_socket) {
    *out_socket = mock_socket.get();
  }
  return std::make_shared<TcpConnection>(
      std::move(mock_socket), 1, max_write_queue_size);
}

}  // namespace

TEST(TcpConnectionTest, ReadV1PacketIsRejected) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;

  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  Packet received{};
  bool called = false;
  session->SetMessageHandler([&](const std::shared_ptr<TcpSession>& active_session,
                                 const Packet& packet) {
    ASSERT_TRUE(active_session);
    EXPECT_EQ(active_session->GetSessionId(), 1u);
    received = packet;
    called = true;
  });

  session->Start();

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  auto encoded = BuildLegacyV1Packet(msg_id);
  mock_socket->PushReadData(std::move(encoded));

  io_context.run();

  EXPECT_FALSE(called);
  EXPECT_EQ(received.msg_id, 0u);
  EXPECT_NE(session->GetState(), TcpSession::SessionState::kActive);
  EXPECT_TRUE(mock_socket->IsClosed());
}

TEST(TcpConnectionTest, ReadV2PacketTriggersHandler) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;

  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  Packet received{};
  bool called = false;
  session->SetMessageHandler([&](const std::shared_ptr<TcpSession>& active_session,
                                 const Packet& packet) {
    ASSERT_TRUE(active_session);
    EXPECT_EQ(active_session->GetSessionId(), 1u);
    received = packet;
    called = true;
  });

  session->Start();

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  auto encoded = PacketCodec::EncodeV2(msg_id, nullptr, 0, 0);
  mock_socket->PushReadData(std::move(encoded));

  io_context.run();

  EXPECT_TRUE(called);
  EXPECT_EQ(received.msg_id, msg_id);
  EXPECT_TRUE(received.payload.empty());
}

TEST(TcpConnectionTest, TcpDropsKcpFlaggedPacket) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;

  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  size_t call_count = 0;
  uint16_t last_msg_id = 0;
  session->SetMessageHandler([&](const std::shared_ptr<TcpSession>& /*active_session*/,
                                 const Packet& packet) {
    ++call_count;
    last_msg_id = packet.msg_id;
  });

  session->Start();

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  auto invalid = PacketCodec::EncodeV2(
      msg_id, nullptr, 0, 1, PacketHeaderV2::kFlagChannelKcp);
  auto valid = PacketCodec::EncodeV2(
      msg_id, nullptr, 0, 2, 0);

  mock_socket->PushReadData(std::move(invalid));
  mock_socket->PushReadData(std::move(valid));

  io_context.run();

  EXPECT_EQ(call_count, 1u);
  EXPECT_EQ(last_msg_id, msg_id);
  EXPECT_EQ(session->GetState(), TcpSession::SessionState::kActive);
}

TEST(TcpConnectionTest, SendWritesEncodedPacket) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);

  session->Start();

  std::vector<uint8_t> payload{5, 6, 7};
  session->Send(100, payload);
  io_context.run();

  const auto& writes = mock_socket->GetWrites();
  ASSERT_EQ(writes.size(), 1u);
  const auto expected = PacketCodec::EncodeV2(100, payload.data(), payload.size(), 0);
  EXPECT_EQ(writes.front(), expected);
}

TEST(TcpConnectionTest, ConcurrentSessionSendKeepsWireSequenceMonotonic) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(
      io_context, &mock_socket, /*max_write_queue_size=*/8192);
  auto session = std::make_shared<TcpSession>(connection);
  session->Start();

  constexpr uint16_t kMsgId =
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  constexpr int kThreadCount = 8;
  constexpr int kMessagesPerThread = 512;
  constexpr int kTotalMessages = kThreadCount * kMessagesPerThread;
  constexpr int kIoThreadCount = 4;

  auto work_guard = asio::make_work_guard(io_context);
  std::vector<std::thread> io_threads;
  io_threads.reserve(kIoThreadCount);
  for (int i = 0; i < kIoThreadCount; ++i) {
    io_threads.emplace_back([&io_context]() {
      io_context.run();
    });
  }

  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    threads.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int i = 0; i < kMessagesPerThread; ++i) {
        session->Send(kMsgId, {});
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }

  work_guard.reset();
  for (auto& thread : io_threads) {
    thread.join();
  }

  const auto& writes = mock_socket->GetWrites();
  ASSERT_EQ(writes.size(), static_cast<size_t>(kTotalMessages));
  for (size_t i = 0; i < writes.size(); ++i) {
    Packet packet{};
    uint16_t sequence = 0;
    ASSERT_EQ(PacketCodec::DecodeV2(writes[i].data(), writes[i].size(), &packet, &sequence),
              DecodeStatus::kOk);
    EXPECT_EQ(packet.msg_id, kMsgId);
    EXPECT_EQ(sequence, static_cast<uint16_t>(i));
  }
}

TEST(TcpConnectionTest, PauseReadBlocksUntilResume) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;

  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  size_t packet_count = 0;
  session->SetMessageHandler([&](const std::shared_ptr<TcpSession>&, const Packet&) {
    ++packet_count;
  });

  session->Start();
  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  mock_socket->PushReadData(PacketCodec::EncodeV2(msg_id, nullptr, 0, 1));
  io_context.poll();
  io_context.restart();
  EXPECT_EQ(packet_count, 1u);

  connection->PauseRead();
  io_context.poll();
  io_context.restart();
  EXPECT_TRUE(connection->IsReadPaused());

  // PauseRead does not cancel an already in-flight read; only one packet may
  // still pass before reads are fully blocked.
  mock_socket->PushReadData(PacketCodec::EncodeV2(msg_id, nullptr, 0, 2));
  mock_socket->PushReadData(PacketCodec::EncodeV2(msg_id, nullptr, 0, 3));
  io_context.poll();
  io_context.restart();
  EXPECT_EQ(packet_count, 2u);

  connection->ResumeRead();
  io_context.poll();
  io_context.restart();
  EXPECT_FALSE(connection->IsReadPaused());

  io_context.run();
  EXPECT_EQ(packet_count, 3u);
}

TEST(TcpConnectionTest, ReadErrorTriggersDisconnect) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);

  bool disconnected = false;
  connection->SetDisconnectHandler([&](uint64_t id) {
    EXPECT_EQ(id, 1u);
    disconnected = true;
  });

  connection->Start();
  mock_socket->SetReadError(asio::error::connection_reset);
  io_context.run();

  EXPECT_TRUE(disconnected);
  EXPECT_TRUE(mock_socket->IsClosed());
}

TEST(TcpConnectionTest, RateLimitExceededMarksSessionLimited) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
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

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  for (int i = 0; i < 51; ++i) {
    auto encoded = PacketCodec::EncodeV2(msg_id, nullptr, 0,
                                         static_cast<uint16_t>(i));
    mock_socket->PushReadData(std::move(encoded));
  }

  io_context.run();

  EXPECT_TRUE(session->IsRateLimited());
  EXPECT_NE(session->GetState(), TcpSession::SessionState::kActive);
}

TEST(TcpConnectionTest, BypassRateLimitKeepsSessionActive) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
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

  session->SetBypassRateLimit(true);
  session->Start();

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  for (int i = 0; i < 500; ++i) {
    auto encoded = PacketCodec::EncodeV2(msg_id, nullptr, 0,
                                         static_cast<uint16_t>(i));
    mock_socket->PushReadData(std::move(encoded));
  }

  io_context.run();

  EXPECT_FALSE(session->IsRateLimited());
  EXPECT_EQ(session->GetState(), TcpSession::SessionState::kActive);
}

}  // namespace mir2::network
