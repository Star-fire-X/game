#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "chat_generated.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "integration/kcp_integration_test_base.h"
#include "integration/test_helpers.h"

namespace {

using mir2::common::ChannelType;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::test::integration::KcpIntegrationTestBase;
using mir2::test::integration::MessageCapture;
using mir2::test::integration::PerformanceMonitor;
using KcpUpgradeState = mir2::client::DualChannelClient::KcpUpgradeState;

constexpr double kKcpRttLimitMs = 50.0;
constexpr double kTcpRttLimitMs = 100.0;
constexpr std::chrono::milliseconds kHandshakeTimeout(3000);
constexpr std::chrono::milliseconds kMessageTimeout(300);
constexpr std::chrono::milliseconds kMixedTimeout(2000);

constexpr uint16_t kMoveReqId = static_cast<uint16_t>(MsgId::kMoveReq);

constexpr uint16_t kLoginReqId = static_cast<uint16_t>(MsgId::kLoginReq);
constexpr uint16_t kChatReqId = static_cast<uint16_t>(MsgId::kChatReq);
// Placeholder for "ShopBuy" until a dedicated MsgId exists in the protocol.
constexpr uint16_t kShopBuyId = static_cast<uint16_t>(MsgId::kNpcMenuSelect);

std::vector<uint8_t> BuildPayload(uint16_t msg_id, uint8_t seed) {
  if (msg_id == kMoveReqId) {
    mir2::common::MoveRequest move_req;
    move_req.target_x = static_cast<int32_t>(100 + seed);
    move_req.target_y = static_cast<int32_t>(200 + seed);
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeMoveRequest(move_req, &status);
    if (status == mir2::common::MessageCodecStatus::kOk) {
      return payload;
    }
    return {};
  }

  if (msg_id == kLoginReqId) {
    mir2::common::LoginRequest request;
    request.username = "route_user_" + std::to_string(seed);
    request.password = "route_pass";
    request.version = "route_test";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    if (status == mir2::common::MessageCodecStatus::kOk) {
      return payload;
    }
    return {};
  }

  if (msg_id == kChatReqId) {
    flatbuffers::FlatBufferBuilder builder;
    const auto content = builder.CreateString("route_chat_" + std::to_string(seed));
    const auto req = mir2::proto::CreateChatReq(
        builder, mir2::proto::ChatChannel::WORLD, content, 0);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
  }

  if (msg_id == kShopBuyId) {
    return {'{', '}', static_cast<uint8_t>(seed)};
  }

  return {};
}

class KcpRoutingIntegrationTest : public KcpIntegrationTestBase {
 protected:
  bool EstablishDualChannel() {
    if (!client_ || !server_) {
      return false;
    }
    if (!client_->connect(kHost, kTcpPort)) {
      return false;
    }
    if (!WaitForCondition([this]() { return client_->is_connected(); }, kHandshakeTimeout)) {
      return false;
    }
    if (!WaitForCondition([this]() {
           return client_->get_kcp_state() == KcpUpgradeState::kConfirmed;
         }, kHandshakeTimeout)) {
      return false;
    }
    return true;
  }

  void RegisterEchoHandler(uint16_t request_id, uint16_t response_id) {
    if (!server_) {
      return;
    }
    server_->RegisterHandler(
        request_id,
        [response_id](const std::shared_ptr<mir2::network::TcpSession>& session,
                      const std::vector<uint8_t>& payload) {
          if (!session) {
            return;
          }
          session->Send(response_id, payload);
        });
  }

  void ClearPendingPackets() {
    pending_packets_.clear();
  }

  std::optional<NetworkPacket> WaitForClientPacket(uint16_t msg_id,
                                                   std::chrono::milliseconds timeout) {
    if (auto pending = PopPending(msg_id)) {
      return pending;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpOnce();

      while (true) {
        auto packet = client_->receive();
        if (!packet) {
          break;
        }
        if (packet->msg_id == msg_id) {
          return packet;
        }
        pending_packets_.push_back(std::move(*packet));
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      if (auto pending = PopPending(msg_id)) {
        return pending;
      }
    }
    return std::nullopt;
  }

 private:
  std::optional<NetworkPacket> PopPending(uint16_t msg_id) {
    auto it = std::find_if(pending_packets_.begin(), pending_packets_.end(),
                           [msg_id](const NetworkPacket& packet) {
                             return packet.msg_id == msg_id;
                           });
    if (it == pending_packets_.end()) {
      return std::nullopt;
    }
    NetworkPacket packet = std::move(*it);
    pending_packets_.erase(it);
    return packet;
  }

  std::deque<NetworkPacket> pending_packets_;
};

}  // namespace

TEST_F(KcpRoutingIntegrationTest, KcpMessagesRouting) {
  auto capture = std::make_shared<MessageCapture>();
  server_->SetMessageHook([capture_weak = std::weak_ptr<MessageCapture>(capture)](
                              ChannelType channel,
                              const std::shared_ptr<mir2::network::TcpSession>&,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
    if (auto capture_locked = capture_weak.lock()) {
      capture_locked->Capture(channel, msg_id, payload);
    }
  });

  ASSERT_TRUE(EstablishDualChannel());
  capture->Clear();
  ClearPendingPackets();

  const std::vector<uint16_t> kcp_messages = {
      kMoveReqId
  };

  for (uint16_t msg_id : kcp_messages) {
    RegisterEchoHandler(msg_id, msg_id);
  }

  PerformanceMonitor perf;
  uint8_t seed = 0x10;

  for (uint16_t msg_id : kcp_messages) {
    const auto payload = BuildPayload(msg_id, seed++);
    const auto start = std::chrono::steady_clock::now();

    client_->send(msg_id, payload);

    auto captured = capture->WaitForMessage(msg_id, kMessageTimeout);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->channel, ChannelType::kKcp);

    auto response = WaitForClientPacket(msg_id, kMessageTimeout);
    ASSERT_TRUE(response.has_value());

    const auto rtt = std::chrono::steady_clock::now() - start;
    perf.RecordRtt(std::chrono::duration_cast<std::chrono::nanoseconds>(rtt));
    const double rtt_ms = std::chrono::duration<double, std::milli>(rtt).count();
    EXPECT_LT(rtt_ms, kKcpRttLimitMs);
  }

  const auto stats = perf.GetRttStats();
  EXPECT_EQ(stats.count, kcp_messages.size());
  EXPECT_LT(stats.p95_ms, kKcpRttLimitMs);
}

TEST_F(KcpRoutingIntegrationTest, TcpMessagesRouting) {
  auto capture = std::make_shared<MessageCapture>();
  server_->SetMessageHook([capture_weak = std::weak_ptr<MessageCapture>(capture)](
                              ChannelType channel,
                              const std::shared_ptr<mir2::network::TcpSession>&,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
    if (auto capture_locked = capture_weak.lock()) {
      capture_locked->Capture(channel, msg_id, payload);
    }
  });

  ASSERT_TRUE(EstablishDualChannel());
  capture->Clear();
  ClearPendingPackets();

  const std::vector<uint16_t> tcp_messages = {
      kLoginReqId,
      kChatReqId,
      kShopBuyId
  };

  for (uint16_t msg_id : tcp_messages) {
    RegisterEchoHandler(msg_id, msg_id);
  }

  PerformanceMonitor perf;
  uint8_t seed = 0x20;

  for (uint16_t msg_id : tcp_messages) {
    const auto payload = BuildPayload(msg_id, seed++);
    perf.RecordSent();

    client_->send(msg_id, payload);

    auto captured = capture->WaitForMessage(msg_id, kMessageTimeout);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->channel, ChannelType::kTcp);

    auto response = WaitForClientPacket(msg_id, kMessageTimeout);
    ASSERT_TRUE(response.has_value());
    perf.RecordReceived();
  }

  EXPECT_EQ(perf.sent_count(), perf.received_count());
  EXPECT_DOUBLE_EQ(perf.GetPacketLossRate(), 0.0);
}

TEST_F(KcpRoutingIntegrationTest, MixedRoutingPerformance) {
  auto capture = std::make_shared<MessageCapture>();
  server_->SetMessageHook([capture_weak = std::weak_ptr<MessageCapture>(capture)](
                              ChannelType channel,
                              const std::shared_ptr<mir2::network::TcpSession>&,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
    if (auto capture_locked = capture_weak.lock()) {
      capture_locked->Capture(channel, msg_id, payload);
    }
  });

  ASSERT_TRUE(EstablishDualChannel());
  capture->Clear();
  ClearPendingPackets();

  const std::vector<uint16_t> kcp_messages = {kMoveReqId};
  const std::vector<uint16_t> tcp_messages = {kLoginReqId, kChatReqId, kShopBuyId};

  for (uint16_t msg_id : kcp_messages) {
    RegisterEchoHandler(msg_id, msg_id);
  }
  for (uint16_t msg_id : tcp_messages) {
    RegisterEchoHandler(msg_id, msg_id);
  }

  constexpr int kIterations = 5;
  std::mutex send_mutex;
  std::mutex time_mutex;
  std::unordered_map<uint16_t, std::deque<std::chrono::steady_clock::time_point>> send_times;

  auto record_send = [&](uint16_t msg_id) {
    std::lock_guard<std::mutex> lock(time_mutex);
    send_times[msg_id].push_back(std::chrono::steady_clock::now());
  };

  auto send_loop = [&](const std::vector<uint16_t>& msg_ids, uint8_t seed) {
    for (int i = 0; i < kIterations; ++i) {
      for (uint16_t msg_id : msg_ids) {
        const auto payload = BuildPayload(msg_id, seed++);
        {
          std::lock_guard<std::mutex> lock(send_mutex);
          record_send(msg_id);
          client_->send(msg_id, payload);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };

  std::thread kcp_thread([&]() { send_loop(kcp_messages, 0x30); });
  std::thread tcp_thread([&]() { send_loop(tcp_messages, 0x50); });

  kcp_thread.join();
  tcp_thread.join();

  const size_t expected_kcp = kcp_messages.size() * kIterations;
  const size_t expected_tcp = tcp_messages.size() * kIterations;

  ASSERT_TRUE(WaitForCondition([&]() {
    for (uint16_t msg_id : kcp_messages) {
      if (server_->GetReceivedCount(ChannelType::kKcp, msg_id) < kIterations) {
        return false;
      }
    }
    for (uint16_t msg_id : tcp_messages) {
      if (server_->GetReceivedCount(ChannelType::kTcp, msg_id) < kIterations) {
        return false;
      }
    }
    return true;
  }, kMixedTimeout));

  PerformanceMonitor kcp_perf;
  PerformanceMonitor tcp_perf;
  size_t received_kcp = 0;
  size_t received_tcp = 0;

  const auto deadline = std::chrono::steady_clock::now() + kMixedTimeout;
  while ((received_kcp < expected_kcp || received_tcp < expected_tcp) &&
         std::chrono::steady_clock::now() < deadline) {
    PumpOnce();
    bool any_packet = false;
    while (true) {
      auto packet = client_->receive();
      if (!packet) {
        break;
      }
      any_packet = true;

      const uint16_t msg_id = packet->msg_id;
      std::optional<std::chrono::steady_clock::time_point> start;

      {
        std::lock_guard<std::mutex> lock(time_mutex);
        auto it = send_times.find(msg_id);
        if (it != send_times.end() && !it->second.empty()) {
          start = it->second.front();
          it->second.pop_front();
        }
      }

      if (!start) {
        continue;
      }

      const auto rtt = std::chrono::steady_clock::now() - *start;
      if (std::find(kcp_messages.begin(), kcp_messages.end(), msg_id) !=
          kcp_messages.end()) {
        kcp_perf.RecordRtt(std::chrono::duration_cast<std::chrono::nanoseconds>(rtt));
        ++received_kcp;
      } else if (std::find(tcp_messages.begin(), tcp_messages.end(), msg_id) !=
                 tcp_messages.end()) {
        tcp_perf.RecordRtt(std::chrono::duration_cast<std::chrono::nanoseconds>(rtt));
        ++received_tcp;
      }
    }

    if (!any_packet) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  EXPECT_EQ(received_kcp, expected_kcp);
  EXPECT_EQ(received_tcp, expected_tcp);

  const auto kcp_stats = kcp_perf.GetRttStats();
  const auto tcp_stats = tcp_perf.GetRttStats();

  EXPECT_GT(kcp_stats.count, 0u);
  EXPECT_GT(tcp_stats.count, 0u);
  EXPECT_LT(kcp_stats.p95_ms, kKcpRttLimitMs);
  EXPECT_LT(tcp_stats.p95_ms, kTcpRttLimitMs);

  auto captured = capture->Drain();
  std::unordered_map<uint16_t, size_t> kcp_counts;
  std::unordered_map<uint16_t, size_t> tcp_counts;

  for (const auto& entry : captured) {
    if (std::find(kcp_messages.begin(), kcp_messages.end(), entry.msg_id) !=
        kcp_messages.end()) {
      EXPECT_EQ(entry.channel, ChannelType::kKcp);
      kcp_counts[entry.msg_id]++;
    } else if (std::find(tcp_messages.begin(), tcp_messages.end(), entry.msg_id) !=
               tcp_messages.end()) {
      EXPECT_EQ(entry.channel, ChannelType::kTcp);
      tcp_counts[entry.msg_id]++;
    }
  }

  for (uint16_t msg_id : kcp_messages) {
    EXPECT_EQ(kcp_counts[msg_id], kIterations);
    EXPECT_EQ(server_->GetReceivedCount(ChannelType::kTcp, msg_id), 0u);
  }
  for (uint16_t msg_id : tcp_messages) {
    EXPECT_EQ(tcp_counts[msg_id], kIterations);
    EXPECT_EQ(server_->GetReceivedCount(ChannelType::kKcp, msg_id), 0u);
  }
}
