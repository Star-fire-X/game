#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "common/enums.h"
#include "common/protocol/npc_message_codec.h"
#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "logic/mock_response_sender.h"
#include "logic/services/merchant_service.h"
#include "logic/services/npc_shop_response_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

class NpcShopResponseServiceTest : public ::testing::Test {
 protected:
  entt::entity AddPlayerEntity(uint64_t role_id) {
    const auto entity = registry_.create();
    auto& identity = registry_.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = role_id;
    identity.name = "player_" + std::to_string(role_id);
    return entity;
  }

  entt::registry registry_;
  mir2::ecs::EventBus event_bus_{registry_};
  MockResponseSender response_sender_;
  RoleStore role_store_;
  MerchantService merchant_service_{registry_, event_bus_};
};

TEST_F(NpcShopResponseServiceTest, SendsCanonicalNpcShopOpenResponse) {
  const auto player = AddPlayerEntity(/*role_id=*/1001);
  role_store_.BindClientRole(/*client_id=*/9001, /*player_id=*/1001);
  merchant_service_.ReplaceAllShops({{
      77,
      ShopConfig{
          77,
          "basic",
          {
              ShopItem{2001u, 10, -1},
              ShopItem{2002u, 20, 70000},
          },
          1.0f,
          0.5f,
      },
  }});
  NpcShopResponseService service(
      response_sender_, event_bus_, role_store_, merchant_service_);

  mir2::ecs::events::NpcOpenMerchantEvent event{};
  event.player = player;
  event.npc_id = 5001;
  event.store_id = 77;
  event_bus_.Publish(event);

  const auto responses = response_sender_.GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, 9001u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kNpcShopOpen));

  mir2::common::NpcShopOpenMsg decoded;
  ASSERT_EQ(mir2::common::DecodeNpcShopOpen(
                static_cast<uint16_t>(mir2::common::MsgId::kNpcShopOpen),
                responses[0].payload,
                &decoded),
            mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(decoded.shop_id, 77u);
  EXPECT_EQ(decoded.npc_id, 5001u);
  ASSERT_EQ(decoded.items.size(), 2u);
  EXPECT_EQ(decoded.items[0].item_id, 2001u);
  EXPECT_EQ(decoded.items[1].item_id, 2002u);
  EXPECT_EQ(decoded.items[1].stock, std::numeric_limits<uint16_t>::max());

  const auto payload_text =
      std::string(responses[0].payload.begin(), responses[0].payload.end());
  const auto payload_json = nlohmann::json::parse(payload_text);
  ASSERT_TRUE(payload_json.contains("store_id"));
  ASSERT_TRUE(payload_json.contains("items"));
  ASSERT_EQ(payload_json["items"].size(), 2u);
  EXPECT_FALSE(payload_json["items"][0].contains("stock"));
  EXPECT_EQ(payload_json["items"][1]["stock"].get<uint32_t>(),
            static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
}

TEST_F(NpcShopResponseServiceTest, MissingStoreIdDoesNotSendResponse) {
  const auto player = AddPlayerEntity(/*role_id=*/1002);
  role_store_.BindClientRole(/*client_id=*/9002, /*player_id=*/1002);
  merchant_service_.ReplaceAllShops({{
      77,
      ShopConfig{77, "basic", {ShopItem{2001u, 10, 5}}, 1.0f, 0.5f},
  }});
  NpcShopResponseService service(
      response_sender_, event_bus_, role_store_, merchant_service_);

  mir2::ecs::events::NpcOpenMerchantEvent event{};
  event.player = player;
  event.npc_id = 5002;
  event.store_id = 0;
  event_bus_.Publish(event);

  EXPECT_TRUE(response_sender_.GetCapturedResponses().empty());
}

TEST_F(NpcShopResponseServiceTest, MissingClientBindingDoesNotSendResponse) {
  const auto player = AddPlayerEntity(/*role_id=*/1003);
  merchant_service_.ReplaceAllShops({{
      77,
      ShopConfig{77, "basic", {ShopItem{2001u, 10, 5}}, 1.0f, 0.5f},
  }});
  NpcShopResponseService service(
      response_sender_, event_bus_, role_store_, merchant_service_);

  mir2::ecs::events::NpcOpenMerchantEvent event{};
  event.player = player;
  event.npc_id = 5003;
  event.store_id = 77;
  event_bus_.Publish(event);

  EXPECT_TRUE(response_sender_.GetCapturedResponses().empty());
}

}  // namespace
}  // namespace mir2::logic::test
