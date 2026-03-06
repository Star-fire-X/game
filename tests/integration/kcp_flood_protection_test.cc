#include <gtest/gtest.h>

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/enums.h"
#include "common/time_utils.h"
#include "network/kcp_server.h"

namespace mir2::test::integration {
namespace {

using mir2::network::ConvBlacklist;
using mir2::network::IpRateLimiter;
using mir2::network::KcpServer;
using mir2::network::KcpSession;

constexpr uint16_t kTestMsgId = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
constexpr uint32_t kRateLimitPps = 1000;

std::array<uint8_t, KcpSession::kTokenSize> MakeToken(uint8_t seed) {
  std::array<uint8_t, KcpSession::kTokenSize> token{};
  for (size_t i = 0; i < token.size(); ++i) {
    token[i] = static_cast<uint8_t>(seed + i);
  }
  return token;
}

asio::ip::udp::endpoint MakeEndpoint(const std::string& ip, uint16_t port) {
  return asio::ip::udp::endpoint(asio::ip::make_address(ip), port);
}

bool WaitForCount(const std::atomic<uint64_t>& counter,
                  uint64_t expected,
                  std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (counter.load(std::memory_order_relaxed) >= expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return counter.load(std::memory_order_relaxed) >= expected;
}

class KcpUdpClient {
 public:
  KcpUdpClient(const asio::ip::udp::endpoint& local_endpoint,
               const asio::ip::udp::endpoint& server_endpoint,
               uint32_t conv,
               const std::array<uint8_t, KcpSession::kTokenSize>& token)
      : socket_(io_context_),
        session_(conv, token) {
    asio::error_code ec;
    socket_.open(asio::ip::udp::v4(), ec);
    if (!ec) {
      socket_.bind(local_endpoint, ec);
    }
    last_error_ = ec;

    session_.SetRemoteEndpoint(server_endpoint);
    session_.SetOutputHandler(
        [this](const asio::ip::udp::endpoint& endpoint, std::vector<uint8_t>&& packet) {
          if (last_error_) {
            return;
          }
          asio::error_code send_ec;
          socket_.send_to(asio::buffer(packet), endpoint, 0, send_ec);
        });
  }

  bool ok() const { return !last_error_; }

  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
    if (last_error_) {
      return;
    }
    session_.Send(msg_id, payload);
    session_.Update(static_cast<uint32_t>(mir2::common::now_ms()));
  }

 private:
  asio::io_context io_context_;
  asio::ip::udp::socket socket_;
  KcpSession session_;
  asio::error_code last_error_;
};

void SendInvalidTokenPacket(asio::ip::udp::socket& socket,
                            const asio::ip::udp::endpoint& server_endpoint,
                            uint32_t conv,
                            const std::array<uint8_t, KcpSession::kTokenSize>& token) {
  std::array<uint8_t, sizeof(uint32_t) + KcpSession::kTokenSize> buffer{};
  std::memcpy(buffer.data(), &conv, sizeof(conv));
  std::memcpy(buffer.data() + sizeof(conv), token.data(), token.size());
  asio::error_code ec;
  socket.send_to(asio::buffer(buffer), server_endpoint, 0, ec);
}

class KcpFloodProtectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    work_guard_.emplace(io_context_.get_executor());
    server_ = std::make_unique<KcpServer>(io_context_);

    IpRateLimiter::Config limiter_config{};
    limiter_config.max_packets_per_sec = kRateLimitPps;
    limiter_config.window_ms = 1000;
    limiter_config.idle_expire_ms = 60000;
    server_->SetRateLimiterConfig(limiter_config);

    ConvBlacklist::Config blacklist_config{};
    blacklist_config.max_failures = 3;
    blacklist_config.blacklist_ttl_ms = 5 * 60 * 1000;
    blacklist_config.failure_ttl_ms = 5 * 60 * 1000;
    server_->SetConvBlacklistConfig(blacklist_config);

    ASSERT_TRUE(server_->Start("127.0.0.1", 0));
    port_ = server_->GetBoundPort();

    io_thread_ = std::thread([this]() { io_context_.run(); });
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
    }
    work_guard_.reset();
    io_context_.stop();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
  }

  uint16_t port() const { return port_; }
  KcpServer& server() { return *server_; }

 private:
  asio::io_context io_context_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
  std::thread io_thread_;
  std::unique_ptr<KcpServer> server_;
  uint16_t port_ = 0;
};

}  // namespace

TEST_F(KcpFloodProtectionTest, IpRateLimitProtection) {
  const int thread_count = 4;
  const int target_pps = 2000;
  const int per_thread_pps = target_pps / thread_count;
  const auto attack_duration = std::chrono::seconds(1);

  const std::string attacker_ip = "127.0.0.2";
  const std::string legit_ip = "127.0.0.3";
  const auto server_endpoint = MakeEndpoint("127.0.0.1", port());

  std::atomic<uint64_t> attacker_sent{0};
  std::atomic<uint64_t> attacker_processed{0};
  std::atomic<uint64_t> legit_sent{0};
  std::atomic<uint64_t> legit_processed{0};

  constexpr uint32_t kAttackerConvBase = 5000;
  const uint32_t legit_conv = 6000;

  std::vector<std::array<uint8_t, KcpSession::kTokenSize>> attacker_tokens;
  attacker_tokens.reserve(thread_count);
  for (int i = 0; i < thread_count; ++i) {
    const auto token = MakeToken(static_cast<uint8_t>(10 + i));
    const uint32_t conv = kAttackerConvBase + static_cast<uint32_t>(i);
    ASSERT_NE(server().CreateSession(conv, token), nullptr);
    attacker_tokens.push_back(token);
  }

  const auto legit_token = MakeToken(42);
  ASSERT_NE(server().CreateSession(legit_conv, legit_token), nullptr);

  server().SetMessageHandler(
      [&](const std::shared_ptr<KcpSession>& session, const KcpSession::Packet&) {
        if (!session) {
          return;
        }
        const uint32_t conv = session->GetConvId();
        if (conv == legit_conv) {
          legit_processed.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        if (conv >= kAttackerConvBase && conv < kAttackerConvBase + thread_count) {
          attacker_processed.fetch_add(1, std::memory_order_relaxed);
        }
      });

  std::atomic<bool> start_flag{false};
  std::atomic<int> init_failures{0};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&, i]() {
      const uint32_t conv = kAttackerConvBase + static_cast<uint32_t>(i);
      KcpUdpClient client(MakeEndpoint(attacker_ip, 0),
                          server_endpoint,
                          conv,
                          attacker_tokens[static_cast<size_t>(i)]);
      if (!client.ok()) {
        init_failures.fetch_add(1, std::memory_order_relaxed);
        return;
      }

      const auto interval = std::chrono::microseconds(1000000 / per_thread_pps);
      const std::vector<uint8_t> payload{};

      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      auto next_send = std::chrono::steady_clock::now();
      const auto end_time = next_send + attack_duration;
      while (std::chrono::steady_clock::now() < end_time) {
        client.Send(kTestMsgId, payload);
        attacker_sent.fetch_add(1, std::memory_order_relaxed);
        next_send += interval;
        std::this_thread::sleep_until(next_send);
      }
    });
  }

  KcpUdpClient legit_client(MakeEndpoint(legit_ip, 0),
                            server_endpoint,
                            legit_conv,
                            legit_token);
  ASSERT_TRUE(legit_client.ok());

  const auto attack_start = std::chrono::steady_clock::now();
  start_flag.store(true, std::memory_order_release);

  auto next_legit = attack_start;
  const auto legit_interval = std::chrono::milliseconds(10);  // 100 pps
  const auto end_time = attack_start + attack_duration;
  const std::vector<uint8_t> legit_payload{};

  while (std::chrono::steady_clock::now() < end_time) {
    legit_client.Send(kTestMsgId, legit_payload);
    legit_sent.fetch_add(1, std::memory_order_relaxed);
    next_legit += legit_interval;
    std::this_thread::sleep_until(next_legit);
  }

  for (auto& thread : threads) {
    thread.join();
  }

  ASSERT_EQ(init_failures.load(std::memory_order_relaxed), 0);

  WaitForCount(legit_processed, legit_sent.load(std::memory_order_relaxed),
               std::chrono::milliseconds(500));

  const auto attacker_sent_count = attacker_sent.load(std::memory_order_relaxed);
  const auto attacker_processed_count = attacker_processed.load(std::memory_order_relaxed);
  const auto legit_sent_count = legit_sent.load(std::memory_order_relaxed);
  const auto legit_processed_count = legit_processed.load(std::memory_order_relaxed);

  const auto attack_duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(attack_duration).count();
  const double actual_pps = attack_duration_ms > 0
                                ? static_cast<double>(attacker_sent_count) * 1000.0 /
                                      static_cast<double>(attack_duration_ms)
                                : 0.0;

  EXPECT_GT(actual_pps, kRateLimitPps);
  EXPECT_GT(attacker_sent_count, kRateLimitPps);
  EXPECT_LT(attacker_processed_count, attacker_sent_count);
  EXPECT_LE(attacker_processed_count, static_cast<uint64_t>(kRateLimitPps + 100));

  EXPECT_EQ(legit_processed_count, legit_sent_count);
}

TEST_F(KcpFloodProtectionTest, ConvBlacklistProtection) {
  const std::string attacker_ip = "127.0.0.4";
  const std::string legit_ip = "127.0.0.5";
  const auto server_endpoint = MakeEndpoint("127.0.0.1", port());

  const uint32_t bad_conv = 7001;
  const uint32_t good_conv = 7002;

  const auto bad_token = MakeToken(11);
  const auto good_token = MakeToken(21);

  ASSERT_NE(server().CreateSession(bad_conv, bad_token), nullptr);
  ASSERT_NE(server().CreateSession(good_conv, good_token), nullptr);

  std::atomic<uint64_t> bad_processed{0};
  std::atomic<uint64_t> good_processed{0};

  server().SetMessageHandler(
      [&](const std::shared_ptr<KcpSession>& session, const KcpSession::Packet&) {
        if (!session) {
          return;
        }
        const uint32_t conv = session->GetConvId();
        if (conv == bad_conv) {
          bad_processed.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        if (conv == good_conv) {
          good_processed.fetch_add(1, std::memory_order_relaxed);
        }
      });

  KcpUdpClient bad_client(MakeEndpoint(attacker_ip, 0),
                          server_endpoint,
                          bad_conv,
                          bad_token);
  ASSERT_TRUE(bad_client.ok());

  bad_client.Send(kTestMsgId, {});
  EXPECT_TRUE(WaitForCount(bad_processed, 1, std::chrono::milliseconds(200)));

  asio::io_context io_context;
  asio::ip::udp::socket raw_socket(io_context);
  asio::error_code ec;
  raw_socket.open(asio::ip::udp::v4(), ec);
  ASSERT_FALSE(ec) << ec.message();
  raw_socket.bind(MakeEndpoint(attacker_ip, 0), ec);
  ASSERT_FALSE(ec) << ec.message();

  const auto wrong_token = MakeToken(99);
  for (int i = 0; i < 3; ++i) {
    SendInvalidTokenPacket(raw_socket, server_endpoint, bad_conv, wrong_token);
  }
  const uint32_t attacker_ip_u32 = asio::ip::make_address_v4(attacker_ip).to_uint();
  const uint32_t legit_ip_u32 = asio::ip::make_address_v4(legit_ip).to_uint();
  const auto blacklist_deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < blacklist_deadline) {
    if (server().IsConvBlacklisted(
            bad_conv, attacker_ip_u32, mir2::common::now_ms())) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_TRUE(server().IsConvBlacklisted(
      bad_conv, attacker_ip_u32, mir2::common::now_ms()));

  bad_client.Send(kTestMsgId, {});
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(bad_processed.load(std::memory_order_relaxed), 1u);

  KcpUdpClient good_client(MakeEndpoint(legit_ip, 0),
                           server_endpoint,
                           good_conv,
                           good_token);
  ASSERT_TRUE(good_client.ok());

  good_client.Send(kTestMsgId, {});
  EXPECT_TRUE(WaitForCount(good_processed, 1, std::chrono::milliseconds(200)));
  EXPECT_FALSE(server().IsConvBlacklisted(
      good_conv, legit_ip_u32, mir2::common::now_ms()));
}

}  // namespace mir2::test::integration
