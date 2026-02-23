#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "common/enums.h"
#include "mocks/mock_socket.h"
#include "network/packet_codec.h"

#define private public
#include "network/tcp_client.h"
#undef private

namespace mir2::network {
namespace {

struct ClientBundle {
  std::unique_ptr<TcpClient> client;
  MockSocket* socket = nullptr;
};

ClientBundle CreateMockClient(asio::io_context& io_context) {
  ClientBundle bundle;
  bundle.client = std::make_unique<TcpClient>(io_context);
  auto mock_socket = std::make_unique<MockSocket>(io_context.get_executor());
  bundle.socket = mock_socket.get();
  auto connection =
      std::make_shared<TcpConnection>(std::move(mock_socket), 1, /*max_write_queue_size=*/8192);
  bundle.client->connection_ = connection;
  bundle.client->connected_.store(true, std::memory_order_release);
  return bundle;
}

}  // namespace

TEST(TcpClientTest, ConcurrentSendKeepsWireSequenceMonotonic) {
  asio::io_context io_context;
  auto bundle = CreateMockClient(io_context);
  ASSERT_NE(bundle.client, nullptr);
  ASSERT_NE(bundle.socket, nullptr);

  constexpr uint16_t kMsgId =
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  constexpr int kThreadCount = 8;
  constexpr int kMessagesPerThread = 512;
  constexpr int kTotalMessages = kThreadCount * kMessagesPerThread;

  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    threads.emplace_back([&, thread_index]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      for (int i = 0; i < kMessagesPerThread; ++i) {
        const uint8_t marker =
            static_cast<uint8_t>((thread_index * kMessagesPerThread + i) & 0xFF);
        (void)marker;
        bundle.client->Send(kMsgId, {});
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }

  io_context.run();

  const auto& writes = bundle.socket->GetWrites();
  ASSERT_EQ(writes.size(), static_cast<size_t>(kTotalMessages));

  for (size_t index = 0; index < writes.size(); ++index) {
    Packet packet{};
    uint16_t sequence = 0;
    ASSERT_EQ(PacketCodec::DecodeV2(
                  writes[index].data(), writes[index].size(), &packet, &sequence),
              DecodeStatus::kOk);
    EXPECT_EQ(packet.msg_id, kMsgId);
    EXPECT_EQ(sequence, static_cast<uint16_t>(index));
  }
}

}  // namespace mir2::network
