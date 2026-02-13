/**
 * @file chat_handler_test.cc
 * @brief Tests for logic ChatHandler coroutine behavior.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <flatbuffers/flatbuffers.h>

#include <string>
#include <vector>

#include "chat_generated.h"
#include "common/enums.h"
#include "game/map/aoi_manager.h"
#include "logic/services/client_registry.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/chat/chat_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/player_presence_service.h"

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

}  // namespace

class ChatHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    player_presence_service_ =
        std::make_unique<PlayerPresenceService>(ecs_registry_);
    aoi_mgr_ = std::make_unique<mir2::game::map::AOIManager>(100, 100);
    handler_ = std::make_unique<ChatHandler>(*response_sender_,
                                             *player_presence_service_,
                                             *aoi_mgr_,
                                             ecs_registry_);
  }

  void TearDown() override {
    ecs_registry_.clear();
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  asio::io_context io_context_;
  entt::registry ecs_registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  ClientRegistry client_registry_;
  std::unique_ptr<PlayerPresenceService> player_presence_service_;
  std::unique_ptr<mir2::game::map::AOIManager> aoi_mgr_;
  std::unique_ptr<ChatHandler> handler_;
};

// 空载荷应返回 ERR_INVALID_ACTION。
TEST_F(ChatHandlerTest, HandleEmptyPayload) {
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
TEST_F(ChatHandlerTest, HandleMalformedPayload) {
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
TEST_F(ChatHandlerTest, HandleEmptyContent) {
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

}  // namespace mir2::logic::test
