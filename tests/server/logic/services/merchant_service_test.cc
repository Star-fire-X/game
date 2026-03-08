#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <limits>
#include <string>

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/inventory_system.h"
#include "logic/services/merchant_service.h"

namespace mir2::logic::test {
namespace {

class MerchantServiceTest : public ::testing::Test {
 protected:
  MerchantServiceTest() : event_bus_(registry_), service_(registry_, event_bus_) {}

  void SetUp() override {
    ecs::RegistryManager::Instance().GetCharacterManager().BindToCurrentThread();
  }

  entt::entity CreatePlayer(int gold, uint64_t role_id = 0) {
    const entt::entity player = registry_.create();
    auto& attributes = registry_.emplace<ecs::CharacterAttributesComponent>(player);
    attributes.gold = gold;

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(player);
    identity.id = role_id;
    identity.name = "player_" + std::to_string(role_id);

    return player;
  }

  entt::entity CreateInventoryItem(entt::entity owner,
                                   uint32_t item_id,
                                   int count,
                                   int slot_index) {
    const entt::entity item = registry_.create();
    auto& item_component = registry_.emplace<ecs::ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.count = count;

    auto& inventory_owner = registry_.emplace<ecs::InventoryOwnerComponent>(item);
    inventory_owner.owner = owner;
    inventory_owner.slot_index = slot_index;

    return item;
  }

  entt::registry registry_;
  ecs::EventBus event_bus_;
  MerchantService service_;
};

ShopConfig MakeShopConfig(uint32_t store_id,
                          std::string name,
                          float buy_rate,
                          float sell_rate,
                          std::vector<ShopItem> items) {
  ShopConfig shop;
  shop.store_id = store_id;
  shop.name = std::move(name);
  shop.buy_rate = buy_rate;
  shop.sell_rate = sell_rate;
  shop.items = std::move(items);
  return shop;
}

TEST_F(MerchantServiceTest, ReplaceAllShopsStoresProvidedConfigs) {
  service_.ReplaceAllShops({{
      1001,
      MakeShopConfig(1001, "basic", 1.5f, 0.4f, {{2001u, 10, 5}}),
  }});

  const ShopConfig* first = service_.GetShop(1001);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->name, "basic");
  EXPECT_FLOAT_EQ(first->buy_rate, 1.5f);
  EXPECT_FLOAT_EQ(first->sell_rate, 0.4f);
  ASSERT_EQ(first->items.size(), 1u);
  EXPECT_EQ(first->items[0].item_id, 2001u);
  EXPECT_EQ(first->items[0].price, 10);
  EXPECT_EQ(first->items[0].stock, 5);
}

TEST_F(MerchantServiceTest, BuyItemSucceedsAndDeductsGoldAndStock) {
  service_.ReplaceAllShops({{
      1,
      MakeShopConfig(1, "buy", 1.5f, 0.5f, {{9001u, 30, 5}}),
  }});

  const entt::entity player = CreatePlayer(100, 0);
  ASSERT_TRUE(service_.BuyItem(player, 1, 9001, 2));

  const auto& attributes = registry_.get<ecs::CharacterAttributesComponent>(player);
  EXPECT_EQ(attributes.gold, 10);
  EXPECT_EQ(ecs::InventorySystem::CountItem(registry_, player, 9001), 2);

  const ShopConfig* shop = service_.GetShop(1);
  ASSERT_NE(shop, nullptr);
  ASSERT_EQ(shop->items.size(), 1u);
  EXPECT_EQ(shop->items[0].stock, 3);
}

TEST_F(MerchantServiceTest, BuyItemRejectsInvalidInputAndPricingFailures) {
  service_.ReplaceAllShops({
      {2, MakeShopConfig(2, "ok", 1.0f, 0.5f, {{9101u, 100, 1}})},
      {3, MakeShopConfig(3, "overflow", 2.0f, 0.5f, {{9201u, 2147483647, 1}})},
  });

  const entt::entity player = CreatePlayer(50, 0);

  EXPECT_FALSE(service_.BuyItem(player, 2, 9101, 0));
  EXPECT_FALSE(service_.BuyItem(player, 999, 9101, 1));
  EXPECT_FALSE(service_.BuyItem(player, 2, 9101, 2));
  EXPECT_FALSE(service_.BuyItem(player, 2, 9101, 1));
  EXPECT_FALSE(service_.BuyItem(player, 3, 9201, 1));
}

TEST_F(MerchantServiceTest, SellItemWithOpenShopRejectsStockOverflow) {
  service_.ReplaceAllShops({{
      11,
      MakeShopConfig(11, "sell", 1.0f, 1.0f, {{9301u, 50, 2147483647}}),
  }});

  const entt::entity player = CreatePlayer(0, 777);
  const entt::entity item = CreateInventoryItem(player, 9301, 1, 0);

  ecs::events::NpcOpenMerchantEvent open_event;
  open_event.player = player;
  open_event.npc_id = 1;
  open_event.store_id = 11;
  event_bus_.Publish(open_event);

  EXPECT_FALSE(service_.SellItem(player, item, 1));
  EXPECT_TRUE(registry_.valid(item));
  EXPECT_EQ(registry_.get<ecs::CharacterAttributesComponent>(player).gold, 0);
}

TEST_F(MerchantServiceTest, SellItemFallsBackToBestShopAndCreditsGold) {
  service_.ReplaceAllShops({
      {21, MakeShopConfig(21, "weak", 1.0f, 0.3f, {{9401u, 10, 10}})},
      {22, MakeShopConfig(22, "best", 1.0f, 0.5f, {{9401u, 20, 1}})},
  });

  const entt::entity player = CreatePlayer(5, 0);
  const entt::entity item = CreateInventoryItem(player, 9401, 2, 0);

  ASSERT_TRUE(service_.SellItem(player, item, 2));
  EXPECT_FALSE(registry_.valid(item));
  EXPECT_EQ(registry_.get<ecs::CharacterAttributesComponent>(player).gold, 25);

  const ShopConfig* weak_shop = service_.GetShop(21);
  const ShopConfig* best_shop = service_.GetShop(22);
  ASSERT_NE(weak_shop, nullptr);
  ASSERT_NE(best_shop, nullptr);
  EXPECT_EQ(weak_shop->items[0].stock, 10);
  EXPECT_EQ(best_shop->items[0].stock, 3);
}

TEST_F(MerchantServiceTest, SellItemFailsWhenInventoryUseRejectedOrGoldWouldOverflow) {
  service_.ReplaceAllShops({{
      31,
      MakeShopConfig(31, "overflow", 1.0f, 1.0f, {{9501u, 100, 5}}),
  }});

  const entt::entity player = CreatePlayer(std::numeric_limits<int>::max() - 50, 0);
  const entt::entity item = CreateInventoryItem(player, 9501, 1, 0);
  EXPECT_FALSE(service_.SellItem(player, item, 1));
  EXPECT_TRUE(registry_.valid(item));

  auto& attributes = registry_.get<ecs::CharacterAttributesComponent>(player);
  attributes.gold = 0;

  auto& item_component = registry_.get<ecs::ItemComponent>(item);
  item_component.count = 1;

  auto& owner = registry_.get<ecs::InventoryOwnerComponent>(item);
  owner.slot_index = -1;

  EXPECT_FALSE(service_.SellItem(player, item, 1));
  EXPECT_TRUE(registry_.valid(item));
}

TEST_F(MerchantServiceTest, ReplaceAllShopsReplacesExistingShopsWithoutResidualData) {
  service_.ReplaceAllShops({{
      11,
      MakeShopConfig(11, "old", 1.0f, 0.5f, {{9301u, 50, 10}}),
  }});

  ASSERT_NE(service_.GetShop(11), nullptr);
  EXPECT_EQ(service_.GetShop(22), nullptr);

  service_.ReplaceAllShops({{
      22,
      MakeShopConfig(22, "new", 1.5f, 0.25f, {{9401u, 80, -1}}),
  }});

  EXPECT_EQ(service_.GetShop(11), nullptr);
  const auto* replacement = service_.GetShop(22);
  ASSERT_NE(replacement, nullptr);
  EXPECT_EQ(replacement->name, "new");
  EXPECT_FLOAT_EQ(replacement->buy_rate, 1.5f);
  ASSERT_EQ(replacement->items.size(), 1u);
  EXPECT_EQ(replacement->items[0].item_id, 9401u);
}

TEST_F(MerchantServiceTest, ReplaceAllShopsAcceptsEmptyTableAndClearsExistingShops) {
  service_.ReplaceAllShops({{
      11,
      MakeShopConfig(11, "old", 1.0f, 0.5f, {{9301u, 50, 10}}),
  }});

  ASSERT_NE(service_.GetShop(11), nullptr);

  service_.ReplaceAllShops({});

  EXPECT_EQ(service_.GetShop(11), nullptr);
}

TEST_F(MerchantServiceTest, ReplaceAllShopsDoesNotRetainRemovedOpenShopData) {
  const entt::entity player = CreatePlayer(0, 777);
  const entt::entity item = CreateInventoryItem(player, 9301, 2, 0);

  service_.ReplaceAllShops({{
      11,
      MakeShopConfig(11, "old", 1.0f, 1.0f, {{9301u, 50, 10}}),
  }});

  ecs::events::NpcOpenMerchantEvent open_event;
  open_event.player = player;
  open_event.npc_id = 1;
  open_event.store_id = 11;
  event_bus_.Publish(open_event);

  service_.ReplaceAllShops({{
      22,
      MakeShopConfig(22, "new", 1.0f, 0.5f, {{9301u, 20, 1}}),
  }});

  ASSERT_TRUE(service_.SellItem(player, item, 2));
  EXPECT_EQ(service_.GetShop(11), nullptr);
  const auto* replacement = service_.GetShop(22);
  ASSERT_NE(replacement, nullptr);
  EXPECT_EQ(registry_.get<ecs::CharacterAttributesComponent>(player).gold, 20);
  ASSERT_EQ(replacement->items.size(), 1u);
  EXPECT_EQ(replacement->items[0].stock, 3);
}

}  // namespace
}  // namespace mir2::logic::test
