#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>

#include <thread>
#include <vector>

#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "game/map/scene_manager.h"
#include "logic/services/client_registry.h"
#include "logic/handlers/movement/movement_validator.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/movement/movement_handler.h"
#include "logic/response_sender.h"
#include "network/network_manager.h"
#include "security/anti_cheat.h"

namespace {

struct SentMessage {
  uint64_t client_id = 0;
  uint16_t msg_id = 0;
  std::vector<uint8_t> payload;
};

struct TestDispatcher {
  TestDispatcher()
      : executor(io_context, 1),
        network(io_context),
        sender(network,
               [this](uint64_t client_id, uint16_t msg_id,
                      const std::vector<uint8_t>& payload) {
                 messages.push_back({client_id, msg_id, payload});
               }) {}

  void Run() {
    io_context.run();
    io_context.restart();
  }

  asio::io_context io_context;
  mir2::logic::CoroutineExecutor executor;
  mir2::network::NetworkManager network;
  std::vector<SentMessage> messages;
  mir2::logic::ResponseSender sender;
};

std::vector<uint8_t> BuildMoveReq(int x, int y) {
  mir2::common::MoveRequest request;
  request.target_x = x;
  request.target_y = y;
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeMoveRequest(request, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

mir2::game::map::SceneManager::MapConfig BuildMapConfig(int map_id,
                                                        int width,
                                                        int height) {
  mir2::game::map::SceneManager::MapConfig config;
  config.map_id = map_id;
  config.width = width;
  config.height = height;
  config.grid_size = 10;
  config.load_walkability = false;
  return config;
}

entt::entity EnsureEntity(mir2::ecs::CharacterEntityManager& manager,
                          entt::registry& registry,
                          uint32_t character_id,
                          int x,
                          int y,
                          uint32_t map_id,
                          int speed) {
  manager.SetPosition(character_id, x, y, map_id);
  auto entity = manager.TryGet(character_id);
  if (!entity.has_value()) {
    return entt::null;
  }
  auto& attrs = registry.get_or_emplace<mir2::ecs::CharacterAttributesComponent>(*entity);
  attrs.speed = speed;
  return *entity;
}

size_t CountByMsgId(const std::vector<SentMessage>& messages, uint16_t msg_id) {
  size_t count = 0;
  for (const auto& message : messages) {
    if (message.msg_id == msg_id) {
      ++count;
    }
  }
  return count;
}

}  // namespace

TEST(MovementHandlerTest, ValidMoveBroadcastsAndResponds) {
  mir2::logic::ClientRegistry registry;
  registry.Track(1);
  registry.Track(2);
  entt::registry ecs_registry;
  mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
  mir2::game::map::SceneManager scene_manager;
  scene_manager.GetOrCreateMap(BuildMapConfig(1, 50, 50));

  TestDispatcher dispatcher;
  mir2::logic::MovementHandler handler(dispatcher.executor,
                                       dispatcher.sender,
                                       registry,
                                       character_manager,
                                       scene_manager,
                                       ecs_registry);

  const entt::entity entity =
      EnsureEntity(character_manager, ecs_registry, 1, 1, 1, 1, 6);
  ASSERT_TRUE(entity != entt::null);

  mir2::logic::HandlerContext context;
  context.client_id = 1;
  context.entity = entity;
  context.registry = &ecs_registry;

  const auto payload = BuildMoveReq(5, 6);
  dispatcher.executor.Spawn(handler.HandleMessage(
      context, payload.data(), payload.size()));
  dispatcher.Run();

  ASSERT_EQ(dispatcher.messages.size(), 3u);
  EXPECT_EQ(dispatcher.messages[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp));
  EXPECT_EQ(CountByMsgId(dispatcher.messages,
                         static_cast<uint16_t>(mir2::common::MsgId::kEntityMove)),
            2u);

  mir2::common::MoveResponse response;
  const auto status = mir2::common::DecodeMoveResponse(
      mir2::common::kMoveResponseMsgId, dispatcher.messages[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(static_cast<uint16_t>(response.code),
            static_cast<uint16_t>(mir2::common::ErrorCode::kOk));
  EXPECT_EQ(response.x, 5);
  EXPECT_EQ(response.y, 6);
}

TEST(MovementHandlerTest, TargetOutOfRangeReturnsErrorAndKeepsState) {
  mir2::logic::ClientRegistry registry;
  registry.Track(1);
  entt::registry ecs_registry;
  mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
  mir2::game::map::SceneManager scene_manager;
  scene_manager.GetOrCreateMap(BuildMapConfig(1, 20, 20));
  mir2::logic::MovementValidator::Config config;
  config.max_steps = 1;

  TestDispatcher dispatcher;
  mir2::logic::MovementHandler handler(dispatcher.executor,
                                       dispatcher.sender,
                                       registry,
                                       character_manager,
                                       scene_manager,
                                       ecs_registry,
                                       1,
                                       config);

  const entt::entity created_entity =
      EnsureEntity(character_manager, ecs_registry, 1, 2, 2, 1, 5);
  ASSERT_TRUE(created_entity != entt::null);

  mir2::logic::HandlerContext context;
  context.client_id = 1;
  context.entity = created_entity;
  context.registry = &ecs_registry;

  const auto payload = BuildMoveReq(6, 2);
  dispatcher.executor.Spawn(handler.HandleMessage(
      context, payload.data(), payload.size()));
  dispatcher.Run();

  ASSERT_EQ(dispatcher.messages.size(), 1u);
  mir2::common::MoveResponse response;
  const auto status = mir2::common::DecodeMoveResponse(
      mir2::common::kMoveResponseMsgId, dispatcher.messages[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(static_cast<uint16_t>(response.code),
            static_cast<uint16_t>(mir2::common::ErrorCode::kTargetOutOfRange));

  auto entity = character_manager.TryGet(1);
  ASSERT_TRUE(entity.has_value());
  const auto& state = ecs_registry.get<mir2::ecs::CharacterStateComponent>(*entity);
  EXPECT_EQ(state.position.x, 2);
  EXPECT_EQ(state.position.y, 2);
}

TEST(MovementHandlerTest, InvalidPathRecordsAntiCheatViolation) {
  const uint64_t client_id = 7;
  mir2::security::AntiCheat::Instance().ClearPlayerRecord(client_id);

  mir2::logic::ClientRegistry registry;
  registry.Track(client_id);
  entt::registry ecs_registry;
  mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
  mir2::game::map::SceneManager scene_manager;
  scene_manager.GetOrCreateMap(BuildMapConfig(1, 10, 10));
  mir2::logic::MovementValidator::Config config;
  config.max_steps = 1;

  TestDispatcher dispatcher;
  mir2::logic::MovementHandler handler(dispatcher.executor,
                                       dispatcher.sender,
                                       registry,
                                       character_manager,
                                       scene_manager,
                                       ecs_registry,
                                       1,
                                       config);

  const entt::entity entity =
      EnsureEntity(character_manager, ecs_registry, client_id, 1, 1, 1, 5);
  ASSERT_TRUE(entity != entt::null);

  mir2::logic::HandlerContext context;
  context.client_id = client_id;
  context.entity = entity;
  context.registry = &ecs_registry;

  const auto payload = BuildMoveReq(4, 1);
  dispatcher.executor.Spawn(handler.HandleMessage(
      context, payload.data(), payload.size()));
  dispatcher.Run();

  ASSERT_EQ(dispatcher.messages.size(), 1u);
  EXPECT_EQ(mir2::security::AntiCheat::Instance().GetViolationCount(
                client_id, mir2::security::CheatType::kTeleportHack),
            1);
}

TEST(MovementHandlerTest, ConcurrentMovesUpdateState) {
  mir2::logic::ClientRegistry registry;
  entt::registry ecs_registry;
  mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
  mir2::game::map::SceneManager scene_manager;
  scene_manager.GetOrCreateMap(BuildMapConfig(1, 30, 30));

  TestDispatcher dispatcher;
  mir2::logic::MovementHandler handler(dispatcher.executor,
                                       dispatcher.sender,
                                       registry,
                                       character_manager,
                                       scene_manager,
                                       ecs_registry);

  constexpr int kClients = 4;
  std::vector<entt::entity> entities(kClients, entt::entity{});
  for (int i = 0; i < kClients; ++i) {
    const uint32_t client_id = static_cast<uint32_t>(i + 1);
    mir2::security::AntiCheat::Instance().ClearPlayerRecord(client_id);
    registry.Track(client_id);
    const entt::entity entity =
        EnsureEntity(character_manager, ecs_registry, client_id, 1, 1, 1, 8);
    ASSERT_TRUE(entity != entt::null);
    entities[i] = entity;
  }

  std::vector<std::thread> threads;
  threads.reserve(kClients);
  for (int i = 0; i < kClients; ++i) {
    const uint32_t client_id = static_cast<uint32_t>(i + 1);
    const entt::entity entity = entities[i];
    threads.emplace_back([&handler, &dispatcher, &ecs_registry, client_id, entity]() {
      mir2::logic::HandlerContext context;
      context.client_id = client_id;
      context.entity = entity;
      context.registry = &ecs_registry;
      const auto payload = BuildMoveReq(2 + static_cast<int>(client_id),
                                        2 + static_cast<int>(client_id));
      dispatcher.executor.Spawn(handler.HandleMessage(
          context, payload.data(), payload.size()));
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  dispatcher.Run();

  for (int i = 0; i < kClients; ++i) {
    const uint32_t client_id = static_cast<uint32_t>(i + 1);
    auto entity = character_manager.TryGet(client_id);
    ASSERT_TRUE(entity.has_value());
    const auto& state = ecs_registry.get<mir2::ecs::CharacterStateComponent>(*entity);
    EXPECT_EQ(state.position.x, 2 + static_cast<int>(client_id));
    EXPECT_EQ(state.position.y, 2 + static_cast<int>(client_id));
  }
}

TEST(MovementHandlerTest, InvalidPayloadReturnsError) {
  mir2::logic::ClientRegistry registry;
  registry.Track(1);
  entt::registry ecs_registry;
  mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
  mir2::game::map::SceneManager scene_manager;
  scene_manager.GetOrCreateMap(BuildMapConfig(1, 10, 10));

  TestDispatcher dispatcher;
  mir2::logic::MovementHandler handler(dispatcher.executor,
                                       dispatcher.sender,
                                       registry,
                                       character_manager,
                                       scene_manager,
                                       ecs_registry);

  mir2::logic::HandlerContext context;
  context.client_id = 1;

  const std::vector<uint8_t> payload = {0x01, 0x02};
  dispatcher.executor.Spawn(handler.HandleMessage(
      context, payload.data(), payload.size()));
  dispatcher.Run();

  ASSERT_EQ(dispatcher.messages.size(), 1u);
  mir2::common::MoveResponse response;
  const auto status = mir2::common::DecodeMoveResponse(
      mir2::common::kMoveResponseMsgId, dispatcher.messages[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(static_cast<uint16_t>(response.code),
            static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}
