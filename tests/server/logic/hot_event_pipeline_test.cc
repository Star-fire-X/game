#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>

#include "combat_generated.h"
#include "chat_generated.h"
#include "common/protocol/message_codec.h"
#include "logic/events/hot_event_pipeline.h"
#include "logic/handler_context.h"
#include "system_generated.h"

namespace mir2::logic::events {
namespace {

class ScopedEnv {
 public:
  ScopedEnv(const char* key, const char* value) : key_(key) {
    const char* old = std::getenv(key_);
    if (old) {
      had_old_ = true;
      old_value_ = old;
    }
    if (value) {
      setenv(key_, value, 1);
    } else {
      unsetenv(key_);
    }
  }

  ~ScopedEnv() {
    if (had_old_) {
      setenv(key_, old_value_.c_str(), 1);
    } else {
      unsetenv(key_);
    }
  }

 private:
  const char* key_ = nullptr;
  bool had_old_ = false;
  std::string old_value_;
};

TEST(HotEventPipelineTest, MoveRoundTripToPayload) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 1001;
  ctx.entity = static_cast<entt::entity>(123);
  ctx.entity_version = 7;

  mir2::common::MoveRequest req;
  req.target_x = 345;
  req.target_y = 678;
  auto payload = mir2::common::EncodeMoveRequest(req);

  ASSERT_EQ(pipeline->TryEnqueue(
                ctx,
                static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
                payload.data(),
                payload.size()),
            HotEventPipeline::EnqueueResult::kEnqueued);

  HotEvent event{};
  ASSERT_TRUE(pipeline->TryDequeue(&event));
  EXPECT_EQ(event.client_id, 1001U);
  EXPECT_EQ(event.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));
  EXPECT_EQ(event.type, HotEventType::kMove);
  EXPECT_EQ(event.data.move.target_x, 345);
  EXPECT_EQ(event.data.move.target_y, 678);
}

TEST(HotEventPipelineTest, SkillAndHeartbeatRoundTripToPayload) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 2002;
  ctx.entity = static_cast<entt::entity>(456);
  ctx.entity_version = 3;

  {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateSkillReq(builder, 77, 8899);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    std::vector<uint8_t> payload(data, data + builder.GetSize());
    ASSERT_EQ(pipeline->TryEnqueue(
                  ctx,
                  static_cast<uint16_t>(mir2::common::MsgId::kSkillReq),
                  payload.data(),
                  payload.size()),
              HotEventPipeline::EnqueueResult::kEnqueued);
  }

  {
    flatbuffers::FlatBufferBuilder builder;
    const auto hb = mir2::proto::CreateHeartbeat(builder, 9, 123456);
    builder.Finish(hb);
    const uint8_t* data = builder.GetBufferPointer();
    std::vector<uint8_t> payload(data, data + builder.GetSize());
    ASSERT_EQ(pipeline->TryEnqueue(
                  ctx,
                  static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat),
                  payload.data(),
                  payload.size()),
              HotEventPipeline::EnqueueResult::kEnqueued);
  }

  HotEvent skill_event{};
  HotEvent heartbeat_event{};
  ASSERT_TRUE(pipeline->TryDequeue(&skill_event));
  ASSERT_TRUE(pipeline->TryDequeue(&heartbeat_event));
  EXPECT_EQ(skill_event.type, HotEventType::kSkill);
  EXPECT_EQ(skill_event.data.skill.skill_id, 77U);
  EXPECT_EQ(skill_event.data.skill.target_id, 8899U);

  EXPECT_EQ(heartbeat_event.type, HotEventType::kHeartbeat);
  EXPECT_EQ(heartbeat_event.data.heartbeat.seq, 9U);
  EXPECT_EQ(heartbeat_event.data.heartbeat.client_time, 123456U);
}

TEST(HotEventPipelineTest, ChatVarPayloadRoundTrip) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 3003;
  ctx.entity = static_cast<entt::entity>(789);
  ctx.entity_version = 11;

  flatbuffers::FlatBufferBuilder builder;
  const auto text = builder.CreateString("hello arena");
  const auto req = mir2::proto::CreateChatReq(
      builder, mir2::proto::ChatChannel::WORLD, text, 0);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  ASSERT_EQ(pipeline->TryEnqueue(
                ctx,
                static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                payload.data(),
                payload.size()),
            HotEventPipeline::EnqueueResult::kEnqueued);

  HotEvent event{};
  ASSERT_TRUE(pipeline->TryDequeue(&event));
  ASSERT_EQ(event.type, HotEventType::kChat);

  const uint8_t* var_data = nullptr;
  uint32_t var_size = 0;
  ASSERT_TRUE(pipeline->TryReadVarPayload(event, &var_data, &var_size));
  flatbuffers::Verifier verifier(var_data, var_size);
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::ChatReq>(nullptr));
  const auto* decoded = flatbuffers::GetRoot<mir2::proto::ChatReq>(var_data);
  ASSERT_NE(decoded, nullptr);
  ASSERT_NE(decoded->content(), nullptr);
  EXPECT_EQ(decoded->content()->str(), "hello arena");

  pipeline->ReleaseVarPayload(event);
}

TEST(HotEventPipelineTest, DisabledPipelineReturnsBypass) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "0");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 4004;
  ctx.entity = static_cast<entt::entity>(1000);
  ctx.entity_version = 1;

  mir2::common::MoveRequest req;
  req.target_x = 11;
  req.target_y = 22;
  auto payload = mir2::common::EncodeMoveRequest(req);

  EXPECT_EQ(pipeline->TryEnqueue(
                ctx,
                static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
                payload.data(),
                payload.size()),
            HotEventPipeline::EnqueueResult::kBypass);

  HotEvent event{};
  EXPECT_FALSE(pipeline->TryDequeue(&event));
}

TEST(HotEventPipelineTest, GenericPayloadTooLargeRejected) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 5005;
  ctx.entity = static_cast<entt::entity>(1001);
  ctx.entity_version = 1;

  std::vector<uint8_t> payload(1500, 0x5A);
  EXPECT_EQ(pipeline->TryEnqueue(
                ctx,
                static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
                payload.data(),
                payload.size()),
            HotEventPipeline::EnqueueResult::kPayloadTooLarge);
}

TEST(HotEventPipelineTest, ChatPayloadTooLargeRejected) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 6006;
  ctx.entity = static_cast<entt::entity>(1002);
  ctx.entity_version = 2;

  flatbuffers::FlatBufferBuilder builder;
  const auto content = builder.CreateString(std::string(1500, 'x'));
  const auto req = mir2::proto::CreateChatReq(
      builder, mir2::proto::ChatChannel::WORLD, content, 0);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  ASSERT_GT(payload.size(), 1024u);

  EXPECT_EQ(pipeline->TryEnqueue(
                ctx,
                static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                payload.data(),
                payload.size()),
            HotEventPipeline::EnqueueResult::kPayloadTooLarge);
}

TEST(HotEventPipelineTest, ArenaExhaustionReturnsSpecificResult) {
  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  auto pipeline = std::make_unique<HotEventPipeline>();
  pipeline->InitializeFromEnv();

  HandlerContext ctx;
  ctx.client_id = 7007;
  ctx.entity = static_cast<entt::entity>(1003);
  ctx.entity_version = 3;

  std::vector<uint8_t> payload(1024, 0x33);
  HotEventPipeline::EnqueueResult result = HotEventPipeline::EnqueueResult::kEnqueued;
  size_t enqueued = 0;
  for (size_t i = 0; i < 3000; ++i) {
    result = pipeline->TryEnqueue(
        ctx,
        static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
        payload.data(),
        payload.size());
    if (result != HotEventPipeline::EnqueueResult::kEnqueued) {
      break;
    }
    ++enqueued;
  }

  EXPECT_GT(enqueued, 0u);
  EXPECT_EQ(result, HotEventPipeline::EnqueueResult::kArenaExhausted);
}

}  // namespace
}  // namespace mir2::logic::events
