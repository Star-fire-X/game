/**
 * @file chat_handler_test.cc
 * @brief Tests for logic ChatHandler coroutine behavior.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "chat_generated.h"
#include "common/enums.h"
#include "game/map/aoi_manager.h"
#include "logic/services/client_registry.h"
#include "logic/coroutine_executor.h"
#define private public
#include "logic/handlers/chat/chat_handler.h"
#undef private
#include "logic/mock_response_sender.h"
#include "logic/services/player_presence_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildChatReq(mir2::proto::ChatChannel channel,
                                  const std::string& content,
                                  uint64_t target_id = 0) {
  flatbuffers::FlatBufferBuilder builder;
  const auto content_offset = builder.CreateString(content);
  const auto req = mir2::proto::CreateChatReq(builder, channel, content_offset, target_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

bool BenchmarkOnlyEnabled() {
  const char* env = std::getenv("LEGEND2_BENCHMARK_ONLY");
  if (!env) {
    return false;
  }

  const std::string value(env);
  return value == "1" || value == "true" || value == "TRUE";
}

struct LatencyStatsUs {
  double min_us = 0.0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double max_us = 0.0;
  double avg_us = 0.0;
};

LatencyStatsUs BuildLatencyStatsUs(const std::vector<int64_t>& samples_us) {
  LatencyStatsUs stats;
  if (samples_us.empty()) {
    return stats;
  }

  std::vector<int64_t> sorted(samples_us.begin(), samples_us.end());
  std::sort(sorted.begin(), sorted.end());

  const auto percentile = [&sorted](size_t p) -> double {
    const size_t index = (sorted.size() - 1) * p / 100;
    return static_cast<double>(sorted[index]);
  };

  stats.min_us = static_cast<double>(sorted.front());
  stats.p50_us = percentile(50);
  stats.p95_us = percentile(95);
  stats.max_us = static_cast<double>(sorted.back());
  stats.avg_us = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
                 static_cast<double>(sorted.size());
  return stats;
}

class ThrowOnceResponseSender final : public MockResponseSender {
 public:
  void ArmThrowOnce() { throw_remaining_ = 1; }

  size_t SendManyCallCount() const { return send_many_call_count_; }
  const std::vector<size_t>& SendManyBatchSizes() const { return send_many_batch_sizes_; }

  Task<void> SendAsync(uint64_t client_id,
                       uint16_t msg_id,
                       std::vector<uint8_t> payload) override {
    if (throw_remaining_ > 0) {
      --throw_remaining_;
      throw std::runtime_error("injected send failure");
    }
    co_await MockResponseSender::SendAsync(client_id, msg_id, std::move(payload));
  }

  Task<SendManyResult> SendMany(const std::vector<uint64_t>& client_ids,
                                uint16_t msg_id,
                                const std::vector<uint8_t>& payload) override {
    ++send_many_call_count_;
    send_many_batch_sizes_.push_back(client_ids.size());
    co_return co_await MockResponseSender::SendMany(client_ids, msg_id, payload);
  }

 private:
  int throw_remaining_ = 0;
  size_t send_many_call_count_ = 0;
  std::vector<size_t> send_many_batch_sizes_;
};

}  // namespace

class LogicChatHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<ThrowOnceResponseSender>();
    player_presence_service_ =
        std::make_unique<PlayerPresenceService>(ecs_registry_);
    role_store_ = std::make_unique<RoleStore>();
    aoi_mgr_ = std::make_unique<mir2::game::map::AOIManager>(100, 100);
    RebuildHandler(true);
  }

  void TearDown() override {
    ecs_registry_.clear();
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  void RebuildHandler(bool batch_send_enabled) {
    handler_ = std::make_unique<ChatHandler>(*response_sender_,
                                             *player_presence_service_,
                                             *role_store_,
                                             *aoi_mgr_,
                                             ecs_registry_,
                                             batch_send_enabled);
  }

  void BindDispatchRecipients(const mir2::game::chat::ChatDispatchList& dispatches) {
    for (const auto& dispatch : dispatches) {
      const uint64_t role_id = dispatch.first;
      if (role_id == 0 || !role_store_) {
        continue;
      }
      role_store_->BindClientRole(role_id, role_id);
    }
  }

  asio::io_context io_context_;
  entt::registry ecs_registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<ThrowOnceResponseSender> response_sender_;
  ClientRegistry client_registry_;
  std::unique_ptr<PlayerPresenceService> player_presence_service_;
  std::unique_ptr<RoleStore> role_store_;
  std::unique_ptr<mir2::game::map::AOIManager> aoi_mgr_;
  std::unique_ptr<ChatHandler> handler_;
};

// 空载荷应返回 ERR_INVALID_ACTION。
TEST_F(LogicChatHandlerTest, HandleEmptyPayload) {
  HandlerContext context;
  context.client_id = 100;

  executor_->Spawn(handler_->HandleMessage(context, nullptr, 0));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kChatRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

// 损坏的 FlatBuffers 载荷应返回 ERR_INVALID_ACTION。
TEST_F(LogicChatHandlerTest, HandleMalformedPayload) {
  HandlerContext context;
  context.client_id = 101;

  const std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kChatRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

// 空字符串内容应返回 ERR_INVALID_ACTION。
TEST_F(LogicChatHandlerTest, HandleEmptyContent) {
  HandlerContext context;
  context.client_id = 102;

  const auto payload = BuildChatReq(mir2::proto::ChatChannel::WORLD, "");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kChatRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(LogicChatHandlerTest, HandleMessageSendExceptionFallsBackInvalidAction) {
  HandlerContext context;
  context.client_id = 103;
  response_sender_->ArmThrowOnce();

  executor_->Spawn(handler_->HandleMessage(context, nullptr, 0));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kChatRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(LogicChatHandlerTest, HandleHotSendExceptionFallsBackInvalidAction) {
  HandlerContext context;
  context.client_id = 104;
  response_sender_->ArmThrowOnce();

  const auto payload = BuildChatReq(mir2::proto::ChatChannel::WORLD, "hello");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kChatRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(LogicChatHandlerTest, SendChatDispatchesBatchesSamePayload) {
  mir2::game::chat::ChatDispatchList dispatches;
  dispatches.emplace_back(2001u, std::vector<uint8_t>{0xAA, 0xBB});
  dispatches.emplace_back(2002u, std::vector<uint8_t>{0xAA, 0xBB});
  dispatches.emplace_back(2003u, std::vector<uint8_t>{0xCC});
  dispatches.emplace_back(2004u, std::vector<uint8_t>{0xAA, 0xBB});
  BindDispatchRecipients(dispatches);

  executor_->Spawn(handler_->SendChatDispatches(
      static_cast<uint16_t>(mir2::common::MsgId::kChatReq), dispatches));
  RunIoContext();

  EXPECT_EQ(response_sender_->SendManyCallCount(), 2u);
  const auto& batch_sizes = response_sender_->SendManyBatchSizes();
  ASSERT_EQ(batch_sizes.size(), 2u);
  EXPECT_EQ(batch_sizes[0], 3u);
  EXPECT_EQ(batch_sizes[1], 1u);

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 4u);
}

TEST_F(LogicChatHandlerTest, SendChatDispatchesFallsBackToSequentialWhenBatchDisabled) {
  RebuildHandler(false);

  mir2::game::chat::ChatDispatchList dispatches;
  dispatches.emplace_back(2101u, std::vector<uint8_t>{0xAA, 0xBB});
  dispatches.emplace_back(2102u, std::vector<uint8_t>{0xAA, 0xBB});
  dispatches.emplace_back(2103u, std::vector<uint8_t>{0xCC});
  dispatches.emplace_back(2104u, std::vector<uint8_t>{0xAA, 0xBB});
  BindDispatchRecipients(dispatches);

  executor_->Spawn(handler_->SendChatDispatches(
      static_cast<uint16_t>(mir2::common::MsgId::kChatReq), dispatches));
  RunIoContext();

  EXPECT_EQ(response_sender_->SendManyCallCount(), 0u);
  EXPECT_TRUE(response_sender_->SendManyBatchSizes().empty());

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 4u);
}

TEST_F(LogicChatHandlerTest, WorldChatHighFanoutLatencyBenchmarkBatchOnVsOff) {
  if (!BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Set LEGEND2_BENCHMARK_ONLY=1 to run benchmark tests.";
  }

  constexpr size_t kRecipients = 3000;
  constexpr size_t kIterations = 24;

  std::vector<uint8_t> shared_payload(128, 0x5A);
  mir2::game::chat::ChatDispatchList dispatches;
  dispatches.reserve(kRecipients);
  for (size_t i = 0; i < kRecipients; ++i) {
    dispatches.emplace_back(100000u + i, shared_payload);
  }
  BindDispatchRecipients(dispatches);

  auto run_benchmark = [&](bool batch_enabled) {
    RebuildHandler(batch_enabled);
    std::vector<int64_t> elapsed_samples_us;
    elapsed_samples_us.reserve(kIterations);

    const size_t send_many_before = response_sender_->SendManyCallCount();

    for (size_t i = 0; i < kIterations; ++i) {
      response_sender_->Clear();
      const auto started_at = std::chrono::steady_clock::now();
      executor_->Spawn(handler_->SendChatDispatches(
          static_cast<uint16_t>(mir2::common::MsgId::kChatReq), dispatches));
      RunIoContext();
      const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - started_at);
      elapsed_samples_us.push_back(elapsed_us.count());

      EXPECT_EQ(response_sender_->ResponseCount(), kRecipients);
    }

    const size_t send_many_after = response_sender_->SendManyCallCount();
    return std::pair{
        BuildLatencyStatsUs(elapsed_samples_us),
        send_many_after - send_many_before};
  };

  const auto [batch_stats, batch_send_many_calls] = run_benchmark(true);
  const auto [sequential_stats, sequential_send_many_calls] = run_benchmark(false);

  EXPECT_GT(batch_send_many_calls, 0u);
  EXPECT_EQ(sequential_send_many_calls, 0u);

  const double speedup = sequential_stats.avg_us > 0.0
                             ? sequential_stats.avg_us / batch_stats.avg_us
                             : 0.0;
  std::cout << "\n[Chat Benchmark] world fanout recipients=" << kRecipients
            << " iterations=" << kIterations
            << " batch(avg/p95 us)=" << batch_stats.avg_us << "/"
            << batch_stats.p95_us
            << " sequential(avg/p95 us)=" << sequential_stats.avg_us << "/"
            << sequential_stats.p95_us
            << " speedup=" << speedup << "x" << std::endl;
}

}  // namespace mir2::logic::test
