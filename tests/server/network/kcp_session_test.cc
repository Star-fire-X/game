#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "network/kcp_session.h"
#include "network/tcp_session.h"

namespace {

using mir2::network::KcpSession;
using mir2::network::TcpSession;

constexpr uint32_t kTestConvId = 12345;
constexpr std::array<uint8_t, KcpSession::kTokenSize> kTestToken{
    0x10, 0x20, 0x30, 0x40,
    0x50, 0x60, 0x70, 0x80
};

class KcpSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    session_ = std::make_shared<KcpSession>(kTestConvId, kTestToken);
  }

  std::shared_ptr<KcpSession> session_;
};

TEST(KcpSessionStaticTest, AllocateConvIdIsNonZeroAndVaries) {
  std::unordered_set<uint32_t> ids;
  ids.reserve(16);
  for (int i = 0; i < 16; ++i) {
    const uint32_t id = KcpSession::AllocateConvId();
    EXPECT_NE(id, 0u);
    ids.insert(id);
  }
  EXPECT_GT(ids.size(), 1u);
}

TEST_F(KcpSessionTest, ValidateTokenMatchesExactSize) {
  EXPECT_TRUE(session_->ValidateToken(kTestToken.data(), kTestToken.size()));

  auto bad_token = kTestToken;
  bad_token[0] ^= 0xFF;
  EXPECT_FALSE(session_->ValidateToken(bad_token.data(), bad_token.size()));

  EXPECT_FALSE(session_->ValidateToken(nullptr, kTestToken.size()));
  EXPECT_FALSE(session_->ValidateToken(kTestToken.data(), kTestToken.size() - 1));
}

TEST_F(KcpSessionTest, BindTcpSessionStoresWeakReference) {
  auto tcp_session = std::make_shared<TcpSession>(nullptr);
  session_->BindTcpSession(tcp_session);

  auto bound = session_->GetTcpSession();
  ASSERT_TRUE(bound);
  EXPECT_EQ(bound.get(), tcp_session.get());

  bound.reset();
  tcp_session.reset();
  EXPECT_EQ(session_->GetTcpSession(), nullptr);
}

TEST_F(KcpSessionTest, TimeoutAfterDefaultInterval) {
  const int64_t last_active = session_->GetLastActiveMs();
  EXPECT_FALSE(session_->IsTimedOut(last_active + KcpSession::kDefaultTimeoutMs - 1));
  EXPECT_TRUE(session_->IsTimedOut(last_active + KcpSession::kDefaultTimeoutMs));
}

TEST_F(KcpSessionTest, OutputHandlerPrependsConvAndToken) {
  const asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 40000);
  session_->SetRemoteEndpoint(endpoint);

  std::vector<uint8_t> captured;
  asio::ip::udp::endpoint captured_endpoint;

  testing::MockFunction<void(const asio::ip::udp::endpoint&, const uint8_t*, size_t)> mock_output;
  EXPECT_CALL(mock_output, Call(testing::_, testing::_, testing::_))
      .Times(testing::AtLeast(1))
      .WillRepeatedly([&](const asio::ip::udp::endpoint& ep,
                          const uint8_t* data,
                          size_t size) {
        if (captured.empty()) {
          captured_endpoint = ep;
          captured.assign(data, data + size);
        }
      });

  session_->SetOutputHandler(mock_output.AsStdFunction());

  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  session_->Send(100, payload);
  for (uint32_t tick = 1; tick <= 20 && captured.empty(); ++tick) {
    session_->Update(tick * 10);
  }

  ASSERT_FALSE(captured.empty());
  EXPECT_EQ(captured_endpoint, endpoint);
  ASSERT_GE(captured.size(), sizeof(uint32_t) + KcpSession::kTokenSize);

  uint32_t conv = 0;
  std::memcpy(&conv, captured.data(), sizeof(conv));
  EXPECT_EQ(conv, kTestConvId);
  EXPECT_EQ(0, std::memcmp(captured.data() + sizeof(conv),
                           kTestToken.data(),
                           kTestToken.size()));
}

TEST_F(KcpSessionTest, ConcurrentSendAndUpdateIsSafe) {
  const asio::ip::udp::endpoint endpoint(asio::ip::address_v4::loopback(), 40001);
  session_->SetRemoteEndpoint(endpoint);

  std::atomic<int> output_calls{0};
  session_->SetOutputHandler([&](const asio::ip::udp::endpoint&,
                                 const uint8_t*,
                                 size_t) {
    output_calls.fetch_add(1, std::memory_order_relaxed);
  });

  const std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
  std::atomic<bool> start{false};

  std::vector<std::thread> senders;
  senders.reserve(4);
  for (int i = 0; i < 4; ++i) {
    senders.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int j = 0; j < 50; ++j) {
        session_->Send(200, payload);
      }
    });
  }

  std::thread updater([&]() {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (uint32_t tick = 1; tick <= 200; ++tick) {
      session_->Update(tick * 10);
    }
  });

  start.store(true, std::memory_order_release);

  for (auto& thread : senders) {
    thread.join();
  }

  // Ensure pending KCP segments are flushed after all producers finish.
  for (uint32_t tick = 201; tick <= 260; ++tick) {
    session_->Update(tick * 10);
  }
  updater.join();

  EXPECT_GT(output_calls.load(std::memory_order_relaxed), 0);
}

}  // namespace
