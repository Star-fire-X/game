#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#define private public
#include "client/network/kcp_channel.h"
#undef private

extern "C" {
#include "common/3rd_party/ikcp.h"
}

#include "common/protocol/packet_codec.h"
#include "common/enums.h"
#include "system_generated.h"

namespace mir2::client {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class MockUdpTransport : public IUdpTransport {
 public:
  using ReceiveHandler = IUdpTransport::ReceiveHandler;

  MOCK_METHOD(bool, Bind, (uint16_t port), (override));
  MOCK_METHOD(void, Close, (), (override));
  MOCK_METHOD(bool, IsOpen, (), (const, override));
  MOCK_METHOD(void, StartReceive, (ReceiveHandler handler), (override));
  MOCK_METHOD(void,
              SendTo,
              (const asio::ip::udp::endpoint& endpoint, const uint8_t* data, size_t size),
              (override));
};

struct ChannelWithMock {
  std::unique_ptr<asio::io_context> io_context;
  std::unique_ptr<KcpChannel> channel;
  MockUdpTransport* transport = nullptr;
};

ChannelWithMock MakeChannel() {
  auto io_context = std::make_unique<asio::io_context>();
  auto transport = std::make_unique<NiceMock<MockUdpTransport>>();
  auto* transport_ptr = transport.get();
  auto channel = std::make_unique<KcpChannel>(*io_context,
                                              mir2::common::KcpConfig{},
                                              std::move(transport));
  return {std::move(io_context), std::move(channel), transport_ptr};
}

struct KcpSegmentCollector {
  std::vector<std::vector<uint8_t>> segments;

  static int Output(const char* buf, int len, IKCPCB* /*kcp*/, void* user) {
    if (!user || !buf || len <= 0) {
      return -1;
    }
    auto* self = static_cast<KcpSegmentCollector*>(user);
    self->segments.emplace_back(buf, buf + len);
    return 0;
  }
};

std::vector<uint8_t> BuildKcpSegment(uint32_t conv, const std::vector<uint8_t>& payload) {
  KcpSegmentCollector collector;
  IKCPCB* sender = ikcp_create(conv, &collector);
  if (!sender) {
    return {};
  }
  ikcp_setoutput(sender, &KcpSegmentCollector::Output);
  if (ikcp_send(sender, reinterpret_cast<const char*>(payload.data()),
                static_cast<int>(payload.size())) >= 0) {
    // ikcp_flush is a no-op until the control block has been updated at least once.
    ikcp_update(sender, 1);
    ikcp_flush(sender);
  }
  ikcp_release(sender);
  if (collector.segments.empty()) {
    return {};
  }
  return collector.segments.front();
}

std::vector<uint8_t> BuildUdpPacket(
    uint32_t conv,
    const std::array<uint8_t, KcpChannel::kTokenSize>& token,
    const std::vector<uint8_t>& kcp_payload) {
  std::vector<uint8_t> packet(sizeof(conv) + token.size() + kcp_payload.size());
  size_t offset = 0;
  std::memcpy(packet.data() + offset, &conv, sizeof(conv));
  offset += sizeof(conv);
  std::memcpy(packet.data() + offset, token.data(), token.size());
  offset += token.size();
  if (!kcp_payload.empty()) {
    std::memcpy(packet.data() + offset, kcp_payload.data(), kcp_payload.size());
  }
  return packet;
}

std::vector<uint8_t> BuildHeartbeatPayload(uint32_t seq = 1, uint32_t client_time = 2) {
  flatbuffers::FlatBufferBuilder builder;
  const auto hb = mir2::proto::CreateHeartbeat(builder, seq, client_time);
  builder.Finish(hb);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

TEST(KcpChannelTest, ConnectDisconnectDoesNotStopExternalIoContext) {
  asio::io_context io_context;
  io_context.stop();
  EXPECT_TRUE(io_context.stopped());
  auto transport = std::make_unique<NiceMock<MockUdpTransport>>();
  auto* transport_ptr = transport.get();
  KcpChannel channel(io_context, mir2::common::KcpConfig{}, std::move(transport));

  EXPECT_CALL(*transport_ptr, Bind(0)).WillOnce(Return(true));
  EXPECT_CALL(*transport_ptr, StartReceive(_)).Times(1);
  EXPECT_CALL(*transport_ptr, Close()).Times(1);

  EXPECT_TRUE(channel.Connect("127.0.0.1", 12345));
  EXPECT_TRUE(channel.IsConnected());
  EXPECT_FALSE(io_context.stopped());

  channel.Disconnect();

  EXPECT_FALSE(channel.IsConnected());
  EXPECT_FALSE(io_context.stopped());
}

TEST(KcpChannelTest, KcpOutputFramesTokenAndConv) {
  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;
  auto* transport = channel_pack.transport;

  constexpr uint32_t kConv = 0x11223344;
  const std::array<uint8_t, KcpChannel::kTokenSize> token = {'t', 'o', 'k', 'e',
                                                             'n', '0', '1', '2'};
  channel.SetConvId(kConv);
  channel.SetSessionToken(token);
  channel.remote_endpoint_ = asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 6000);

  const std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
  EXPECT_CALL(*transport, IsOpen()).WillOnce(Return(true));
  EXPECT_CALL(*transport, SendTo(_, _, _))
      .WillOnce(Invoke([&](const asio::ip::udp::endpoint& /*endpoint*/,
                           const uint8_t* data,
                           size_t size) {
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(size, sizeof(kConv) + token.size() + payload.size());

        uint32_t conv = 0;
        std::memcpy(&conv, data, sizeof(conv));
        EXPECT_EQ(conv, kConv);

        const uint8_t* token_data = data + sizeof(conv);
        EXPECT_EQ(0, std::memcmp(token_data, token.data(), token.size()));

        const uint8_t* payload_data = token_data + token.size();
        EXPECT_EQ(0, std::memcmp(payload_data, payload.data(), payload.size()));
      }));

  const int result = KcpChannel::KcpOutput(
      reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()),
      nullptr, &channel);
  EXPECT_EQ(result, 0);
}

TEST(KcpChannelTest, ReceiveDeliversPacketWithValidToken) {
  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;

  constexpr uint32_t kConv = 42;
  const std::array<uint8_t, KcpChannel::kTokenSize> token = {'a', 'b', 'c', 'd',
                                                             'e', 'f', 'g', 'h'};
  channel.SetConvId(kConv);
  channel.SetSessionToken(token);
  channel.state_.store(mir2::common::ChannelState::Connected);

  channel.kcp_ = ikcp_create(kConv, &channel);
  ASSERT_NE(channel.kcp_, nullptr);

  const std::vector<uint8_t> payload = BuildHeartbeatPayload();
  const auto encoded = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat),
      payload.data(),
      payload.size(),
      9,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  ASSERT_FALSE(encoded.empty());

  const auto kcp_segment = BuildKcpSegment(kConv, encoded);
  ASSERT_FALSE(kcp_segment.empty());

  const auto udp_packet = BuildUdpPacket(kConv, token, kcp_segment);

  mir2::common::NetworkPacket received{};
  bool got_message = false;
  channel.SetOnMessage([&](const mir2::common::NetworkPacket& packet) {
    received = packet;
    got_message = true;
  });

  asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 10001);
  channel.HandleUdpPacket(endpoint, udp_packet.data(), udp_packet.size());
  channel.Update();

  EXPECT_TRUE(got_message);
  EXPECT_EQ(received.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat));
  EXPECT_EQ(received.payload, payload);

  channel.Disconnect();
}

TEST(KcpChannelTest, ReceiveRejectsMismatchedToken) {
  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;

  constexpr uint32_t kConv = 7;
  const std::array<uint8_t, KcpChannel::kTokenSize> token = {'1', '2', '3', '4',
                                                             '5', '6', '7', '8'};
  channel.SetConvId(kConv);
  channel.SetSessionToken(token);
  channel.state_.store(mir2::common::ChannelState::Connected);

  channel.kcp_ = ikcp_create(kConv, &channel);
  ASSERT_NE(channel.kcp_, nullptr);

  const std::vector<uint8_t> payload = BuildHeartbeatPayload(3, 4);
  const auto encoded = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat),
      payload.data(),
      payload.size(),
      1,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  ASSERT_FALSE(encoded.empty());

  const auto kcp_segment = BuildKcpSegment(kConv, encoded);
  ASSERT_FALSE(kcp_segment.empty());

  const std::array<uint8_t, KcpChannel::kTokenSize> wrong_token = {'x', 'y', 'z', '0',
                                                                   '0', '0', '0', '0'};
  const auto udp_packet = BuildUdpPacket(kConv, wrong_token, kcp_segment);

  bool got_message = false;
  channel.SetOnMessage([&](const mir2::common::NetworkPacket& /*packet*/) {
    got_message = true;
  });

  asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 10002);
  channel.HandleUdpPacket(endpoint, udp_packet.data(), udp_packet.size());
  channel.Update();

  EXPECT_FALSE(got_message);

  channel.Disconnect();
}

TEST(KcpChannelTest, ReceiveRejectsTcpFlaggedPacket) {
  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;

  constexpr uint32_t kConv = 17;
  const std::array<uint8_t, KcpChannel::kTokenSize> token = {'k', 'c', 'p', 'f',
                                                             'l', 'a', 'g', '!'};
  channel.SetConvId(kConv);
  channel.SetSessionToken(token);
  channel.state_.store(mir2::common::ChannelState::Connected);

  channel.kcp_ = ikcp_create(kConv, &channel);
  ASSERT_NE(channel.kcp_, nullptr);

  const std::vector<uint8_t> payload = BuildHeartbeatPayload(11, 22);
  const auto encoded = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat),
      payload.data(),
      payload.size(),
      5,
      /*flags=*/0);
  ASSERT_FALSE(encoded.empty());

  const auto kcp_segment = BuildKcpSegment(kConv, encoded);
  ASSERT_FALSE(kcp_segment.empty());
  const auto udp_packet = BuildUdpPacket(kConv, token, kcp_segment);

  bool got_message = false;
  channel.SetOnMessage([&](const mir2::common::NetworkPacket& /*packet*/) {
    got_message = true;
  });

  asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 10005);
  channel.HandleUdpPacket(endpoint, udp_packet.data(), udp_packet.size());
  channel.Update();

  EXPECT_FALSE(got_message);
  {
    std::lock_guard<std::mutex> lock(channel.receive_mutex_);
    EXPECT_TRUE(channel.receive_queue_.empty());
  }

  channel.Disconnect();
}

TEST(KcpChannelTest, ReceiveQueueCapsAtMaxSize) {
  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;

  constexpr uint32_t kConv = 99;
  const std::array<uint8_t, KcpChannel::kTokenSize> token = {'q', 'w', 'e', 'r',
                                                             't', 'y', 'u', 'i'};
  channel.SetConvId(kConv);
  channel.SetSessionToken(token);
  channel.state_.store(mir2::common::ChannelState::Connected);

  channel.kcp_ = ikcp_create(kConv, &channel);
  ASSERT_NE(channel.kcp_, nullptr);

  {
    std::lock_guard<std::mutex> lock(channel.receive_mutex_);
    for (size_t i = 0; i < KcpChannel::kMaxReceiveQueueSize; ++i) {
      channel.receive_queue_.push(mir2::common::NetworkPacket{});
    }
  }

  const std::vector<uint8_t> payload = BuildHeartbeatPayload(5, 6);
  const auto encoded = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat),
      payload.data(),
      payload.size(),
      2,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  ASSERT_FALSE(encoded.empty());
  const auto kcp_segment = BuildKcpSegment(kConv, encoded);
  ASSERT_FALSE(kcp_segment.empty());
  const auto udp_packet = BuildUdpPacket(kConv, token, kcp_segment);

  asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 10003);
  channel.HandleUdpPacket(endpoint, udp_packet.data(), udp_packet.size());

  {
    std::lock_guard<std::mutex> lock(channel.receive_mutex_);
    EXPECT_EQ(channel.receive_queue_.size(), KcpChannel::kMaxReceiveQueueSize);
  }

  channel.Disconnect();
}

TEST(KcpChannelTest, SendBlocksWhileKcpMutexHeld) {
  using namespace std::chrono_literals;

  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;

  constexpr uint32_t kConv = 123;
  channel.SetConvId(kConv);
  channel.state_.store(mir2::common::ChannelState::Connected);
  channel.kcp_ = ikcp_create(kConv, &channel);
  ASSERT_NE(channel.kcp_, nullptr);

  std::unique_lock<std::mutex> lock(channel.kcp_mutex_);

  std::atomic<bool> started{false};
  std::atomic<bool> completed{false};
  std::thread worker([&]() {
    started.store(true);
    channel.Send(1, {1, 2, 3});
    completed.store(true);
  });

  while (!started.load()) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(completed.load());

  lock.unlock();
  worker.join();
  EXPECT_TRUE(completed.load());

  channel.Disconnect();
}

TEST(KcpChannelTest, HandleUdpPacketBlocksWhileKcpMutexHeld) {
  using namespace std::chrono_literals;

  auto channel_pack = MakeChannel();
  auto& channel = *channel_pack.channel;

  constexpr uint32_t kConv = 321;
  channel.SetConvId(kConv);
  channel.state_.store(mir2::common::ChannelState::Connected);
  channel.kcp_ = ikcp_create(kConv, &channel);
  ASSERT_NE(channel.kcp_, nullptr);

  const std::array<uint8_t, KcpChannel::kTokenSize> token = {'l', 'o', 'c', 'k',
                                                             't', 'e', 's', 't'};
  channel.SetSessionToken(token);

  const std::vector<uint8_t> payload = BuildHeartbeatPayload(7, 8);
  const auto encoded = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat),
      payload.data(),
      payload.size(),
      0,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  ASSERT_FALSE(encoded.empty());
  const auto kcp_segment = BuildKcpSegment(kConv, encoded);
  ASSERT_FALSE(kcp_segment.empty());
  const auto udp_packet = BuildUdpPacket(kConv, token, kcp_segment);

  std::unique_lock<std::mutex> lock(channel.kcp_mutex_);
  std::atomic<bool> started{false};
  std::atomic<bool> completed{false};
  std::thread worker([&]() {
    started.store(true);
    asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 10004);
    channel.HandleUdpPacket(endpoint, udp_packet.data(), udp_packet.size());
    completed.store(true);
  });

  while (!started.load()) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(completed.load());

  lock.unlock();
  worker.join();
  EXPECT_TRUE(completed.load());

  channel.Disconnect();
}

}  // namespace mir2::client
