#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ecs/components/bonus_point_component.h"
#include "ecs/components/character_components.h"
#include "ecs/world.h"
#include "game_generated.h"
#include "logic/coroutine_executor.h"
#include "logic/handler_context.h"
#include "logic/handlers/character/bonus_point_handler.h"

namespace mir2::logic::test {
namespace {

void RunTask(Task<void> task) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  ASSERT_TRUE(executor.Spawn(std::move(task)));
  io_context.run();
}

std::vector<uint8_t> BuildBonusPointReq(const std::string& attribute_name,
                                        uint8_t action) {
  flatbuffers::FlatBufferBuilder builder;
  const auto attr_offset = builder.CreateString(attribute_name);
  const auto req = mir2::proto::CreateBonusPointReq(builder, attr_offset, action);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

HandlerContext BuildContext(ecs::World* world,
                            entt::registry* registry,
                            entt::entity entity) {
  HandlerContext context;
  context.world = world;
  context.registry = registry;
  context.entity = entity;
  context.map_id = 1;
  return context;
}

TEST(BonusPointHandlerTest, InvalidContextReturnsWithoutMutation) {
  HandlerContext context;

  RunTask(HandleBonusPointReq(context, nullptr, 0));
  SUCCEED();
}

TEST(BonusPointHandlerTest, ValidPayloadAllocatesPoint) {
  ecs::World world;
  entt::registry& registry = world.Registry();

  const entt::entity entity = registry.create();
  auto& identity = registry.emplace<ecs::CharacterIdentityComponent>(entity);
  identity.id = 1001;
  identity.char_class = mir2::common::CharacterClass::WARRIOR;

  auto& attributes = registry.emplace<ecs::CharacterAttributesComponent>(entity);
  attributes.level = 80;
  attributes.hp = 100;
  attributes.mp = 50;

  HandlerContext context = BuildContext(&world, &registry, entity);
  const auto payload = BuildBonusPointReq("dc", 0);

  RunTask(HandleBonusPointReq(context, payload.data(), payload.size()));

  const auto* bonus = registry.try_get<ecs::BonusPointComponent>(entity);
  ASSERT_NE(bonus, nullptr);
  EXPECT_GE(bonus->total_available, 1);
  EXPECT_EQ(bonus->allocated.dc, 1);
  EXPECT_EQ(bonus->allocated.total_spent(), 1);
  EXPECT_EQ(attributes.bonus_remaining, bonus->remaining());
}

TEST(BonusPointHandlerTest, ResetActionClearsAllocatedPoints) {
  ecs::World world;
  entt::registry& registry = world.Registry();

  const entt::entity entity = registry.create();
  auto& identity = registry.emplace<ecs::CharacterIdentityComponent>(entity);
  identity.id = 1002;
  identity.char_class = mir2::common::CharacterClass::WARRIOR;

  auto& attributes = registry.emplace<ecs::CharacterAttributesComponent>(entity);
  attributes.level = 80;
  attributes.hp = 100;
  attributes.mp = 50;

  auto& bonus = registry.emplace<ecs::BonusPointComponent>(entity);
  bonus.total_available = 20;
  bonus.allocated.dc = 3;
  bonus.allocated.mc = 2;

  HandlerContext context = BuildContext(&world, &registry, entity);
  const auto payload = BuildBonusPointReq("dc", 1);

  RunTask(HandleBonusPointReq(context, payload.data(), payload.size()));

  const auto* updated = registry.try_get<ecs::BonusPointComponent>(entity);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->allocated.total_spent(), 0);
  EXPECT_EQ(attributes.bonus_remaining, updated->remaining());
}

TEST(BonusPointHandlerTest, InvalidPayloadFallsBackToDefaultActionPath) {
  ecs::World world;
  entt::registry& registry = world.Registry();

  const entt::entity entity = registry.create();
  auto& identity = registry.emplace<ecs::CharacterIdentityComponent>(entity);
  identity.id = 1003;
  identity.char_class = mir2::common::CharacterClass::WARRIOR;

  auto& attributes = registry.emplace<ecs::CharacterAttributesComponent>(entity);
  attributes.level = 80;
  attributes.hp = 100;
  attributes.mp = 50;

  HandlerContext context = BuildContext(&world, &registry, entity);
  const std::vector<uint8_t> invalid_payload = {1, 2, 3, 4, 5};

  RunTask(HandleBonusPointReq(context,
                              invalid_payload.data(),
                              invalid_payload.size()));

  const auto* bonus = registry.try_get<ecs::BonusPointComponent>(entity);
  ASSERT_NE(bonus, nullptr);
  EXPECT_GE(bonus->total_available, 1);
  EXPECT_EQ(bonus->allocated.total_spent(), 0);
  EXPECT_EQ(attributes.bonus_remaining, bonus->remaining());
}

}  // namespace
}  // namespace mir2::logic::test
