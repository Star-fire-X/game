/**
 * @file npc_command_handler_test.cc
 * @brief Tests for logic NPC command handler security checks.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/enums.h"
#include "common/protocol/npc_message_codec.h"
#include "ecs/components/character_components.h"
#include "game/map/scene_manager.h"
#include "game/npc/npc_manager.h"
#include "game/npc/npc_types.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/npc/npc_command_handler.h"
#include "logic/mock_response_sender.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildNpcInteractReq(uint64_t npc_id, uint64_t player_id) {
  nlohmann::json j;
  j["version"] = mir2::common::kNpcCodecVersion;
  j["npc_id"] = npc_id;
  j["player_id"] = player_id;
  const auto dumped = j.dump();
  return std::vector<uint8_t>(dumped.begin(), dumped.end());
}

std::vector<uint8_t> BuildNpcMenuSelectReq(uint64_t npc_id, uint8_t option_index) {
  nlohmann::json j;
  j["version"] = mir2::common::kNpcCodecVersion;
  j["npc_id"] = npc_id;
  j["option_index"] = option_index;
  const auto dumped = j.dump();
  return std::vector<uint8_t>(dumped.begin(), dumped.end());
}

nlohmann::json ParseJsonPayload(const std::vector<uint8_t>& payload) {
  if (payload.empty()) {
    return nlohmann::json::object();
  }
  return nlohmann::json::parse(payload.begin(), payload.end());
}

class ThrowOnceResponseSender final : public MockResponseSender {
 public:
  void ArmThrowOnce() { throw_remaining_ = 1; }

  Task<void> SendAsync(uint64_t client_id,
                       uint16_t msg_id,
                       std::vector<uint8_t> payload) override {
    if (throw_remaining_ > 0) {
      --throw_remaining_;
      throw std::runtime_error("injected send failure");
    }
    co_await MockResponseSender::SendAsync(client_id, msg_id, std::move(payload));
  }

 private:
  int throw_remaining_ = 0;
};

}  // namespace

class NpcCommandHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    game::npc::NpcManager::Instance().Clear();
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<ThrowOnceResponseSender>();
    handler_ = std::make_unique<mir2::logic::NpcCommandHandler>(
        *executor_, *response_sender_, registry_, scene_manager_);
  }

  void TearDown() override {
    game::npc::NpcManager::Instance().Clear();
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  entt::entity CreatePlayer(uint64_t player_id, uint32_t map_id, int32_t x, int32_t y) {
    const entt::entity player = registry_.create();
    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(player);
    identity.id = player_id;
    identity.name = "Player" + std::to_string(player_id);
    auto& state = registry_.emplace<ecs::CharacterStateComponent>(player);
    state.map_id = map_id;
    state.position = {x, y};
    return player;
  }

  uint64_t CreateNpc(uint64_t npc_id,
                     uint32_t map_id,
                     int32_t x,
                     int32_t y,
                     bool enabled = true) {
    game::npc::NpcConfig config;
    config.id = npc_id;
    config.name = "QuestNpc";
    config.type = game::npc::NpcType::kQuest;
    config.map_id = map_id;
    config.x = x;
    config.y = y;
    config.enabled = enabled;
    auto npc = game::npc::NpcManager::Instance().CreateNpc(config);
    EXPECT_NE(npc, nullptr);
    return npc ? npc->GetId() : 0;
  }

  HandlerContext BuildContext(uint64_t client_id, entt::entity player) {
    HandlerContext context;
    context.client_id = client_id;
    context.entity = player;
    context.registry = &registry_;
    return context;
  }

  asio::io_context io_context_;
  entt::registry registry_;
  game::map::SceneManager scene_manager_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<ThrowOnceResponseSender> response_sender_;
  std::unique_ptr<mir2::logic::NpcCommandHandler> handler_;
};

TEST_F(NpcCommandHandlerTest, InteractRejectsSpoofedPlayerId) {
  const entt::entity player = CreatePlayer(10001, 1, 100, 100);
  const uint64_t npc_id = CreateNpc(5001, 1, 101, 100);
  ASSERT_NE(npc_id, 0u);

  auto context = BuildContext(7001, player);
  const auto payload = BuildNpcInteractReq(npc_id, 99999);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp));
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 0u), 1u);
}

TEST_F(NpcCommandHandlerTest, InteractRejectsCrossMapNpc) {
  const entt::entity player = CreatePlayer(10002, 1, 100, 100);
  const uint64_t npc_id = CreateNpc(5002, 2, 101, 100);
  ASSERT_NE(npc_id, 0u);

  auto context = BuildContext(7002, player);
  const auto payload = BuildNpcInteractReq(npc_id, 10002);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 0u), 1u);
}

TEST_F(NpcCommandHandlerTest, InteractRejectsOutOfRangeNpc) {
  const entt::entity player = CreatePlayer(10003, 1, 10, 10);
  const uint64_t npc_id = CreateNpc(5003, 1, 200, 200);
  ASSERT_NE(npc_id, 0u);

  auto context = BuildContext(7003, player);
  const auto payload = BuildNpcInteractReq(npc_id, 10003);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 0u), 1u);
}

TEST_F(NpcCommandHandlerTest, InteractAcceptsReachableNpc) {
  const entt::entity player = CreatePlayer(10004, 1, 100, 100);
  const uint64_t npc_id = CreateNpc(5004, 1, 104, 103);
  ASSERT_NE(npc_id, 0u);

  auto context = BuildContext(7004, player);
  const auto payload = BuildNpcInteractReq(npc_id, 10004);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 1u), 0u);
  EXPECT_EQ(rsp.value("npc_name", std::string()), "QuestNpc");
}

TEST_F(NpcCommandHandlerTest, MenuSelectRejectsOutOfRangeNpc) {
  const entt::entity player = CreatePlayer(10005, 1, 0, 0);
  const uint64_t npc_id = CreateNpc(5005, 1, 100, 100);
  ASSERT_NE(npc_id, 0u);

  auto context = BuildContext(7005, player);
  const auto payload = BuildNpcMenuSelectReq(npc_id, 1);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcMenuSelect),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp));
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 0u), 1u);
}

TEST_F(NpcCommandHandlerTest, InteractSendExceptionFallsBackFailureResponse) {
  const entt::entity player = CreatePlayer(10006, 1, 100, 100);
  const uint64_t npc_id = CreateNpc(5006, 1, 101, 100);
  ASSERT_NE(npc_id, 0u);
  response_sender_->ArmThrowOnce();

  auto context = BuildContext(7006, player);
  const auto payload = BuildNpcInteractReq(npc_id, 10006);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp));
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 0u), 1u);
}

TEST_F(NpcCommandHandlerTest, MenuSelectSendExceptionFallsBackFailureResponse) {
  const entt::entity player = CreatePlayer(10007, 1, 0, 0);
  const uint64_t npc_id = CreateNpc(5007, 1, 100, 100);
  ASSERT_NE(npc_id, 0u);
  response_sender_->ArmThrowOnce();

  auto context = BuildContext(7007, player);
  const auto payload = BuildNpcMenuSelectReq(npc_id, 1);
  executor_->Spawn(handler_->HandleMessage(context,
                                           static_cast<uint16_t>(mir2::common::MsgId::kNpcMenuSelect),
                                           payload.data(),
                                           payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp));
  const auto rsp = ParseJsonPayload(responses[0].payload);
  EXPECT_EQ(rsp.value("npc_id", 0ull), npc_id);
  EXPECT_EQ(rsp.value("result", 0u), 1u);
}

}  // namespace mir2::logic::test
