#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include "server/common/error_codes.h"
#include "common/enums.h"
#include "chat_generated.h"
#include "handlers/chat/chat_handler.h"

namespace {

std::vector<uint8_t> BuildChatReq(mir2::proto::ChatChannel channel,
                                  const std::string& content,
                                  uint64_t target_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto content_offset = builder.CreateString(content);
    const auto req = mir2::proto::CreateChatReq(builder, channel, content_offset, target_id);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

TEST(ChatHandlerTest, WorldChatBroadcastsMessage) {
    legend2::handlers::ClientRegistry registry;
    registry.Track(1);
    registry.Track(2);

    legend2::handlers::ChatHandler handler(registry);
    legend2::handlers::HandlerContext context;
    context.client_id = 1;

    const auto payload = BuildChatReq(mir2::proto::ChatChannel::WORLD, "hello", 0);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 3u);
    EXPECT_EQ(responses[0].msg_id,
              static_cast<uint16_t>(mir2::common::MsgId::kChatRsp));

    flatbuffers::Verifier verifier(responses[1].payload.data(), responses[1].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatMessage>(nullptr));
    const auto* msg = flatbuffers::GetRoot<mir2::proto::ChatMessage>(responses[1].payload.data());
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->content()->str(), "hello");
}

TEST(ChatHandlerTest, PrivateChatToUnknownTargetReturnsError) {
    legend2::handlers::ClientRegistry registry;
    registry.Track(1);

    legend2::handlers::ChatHandler handler(registry);
    legend2::handlers::HandlerContext context;
    context.client_id = 1;

    const auto payload = BuildChatReq(mir2::proto::ChatChannel::PRIVATE, "hi", 2);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kTargetNotFound));
}

TEST(ChatHandlerTest, EmptyContentReturnsError) {
    legend2::handlers::ClientRegistry registry;
    registry.Track(1);

    legend2::handlers::ChatHandler handler(registry);
    legend2::handlers::HandlerContext context;
    context.client_id = 1;

    const auto payload = BuildChatReq(mir2::proto::ChatChannel::WORLD, "", 0);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::ChatRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}
