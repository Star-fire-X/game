#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/systems/inventory_system.h"
#include "logic/services/merchant_service.h"

namespace mir2::logic::test {
namespace {

class MerchantServiceTest : public ::testing::Test {
 protected:
  MerchantServiceTest() : event_bus_(registry_), service_(registry_, event_bus_) {}

  void SetUp() override {
    static std::atomic<uint64_t> sequence{0};
    const uint64_t unique = sequence.fetch_add(1, std::memory_order_relaxed);
    const auto timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    test_dir_ = std::filesystem::temp_directory_path() /
                ("mir2_merchant_service_test_" + std::to_string(timestamp) + "_" +
                 std::to_string(unique));
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(test_dir_, ec))
        << "failed to create test dir: " << ec.message();
  }

  void TearDown() override {
    if (!test_dir_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(test_dir_, ec);
    }
  }

  std::filesystem::path WriteShopConfig(const std::string& content,
                                        const std::string& filename = "shops.yaml") {
    const auto path = test_dir_ / filename;
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    EXPECT_TRUE(out.is_open()) << "failed to open: " << path;
    out << content;
    out.flush();
    EXPECT_TRUE(out.good()) << "failed to write: " << path;
    return path;
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

  void FillInventory(entt::entity owner) {
    for (int slot = 0; slot < common::constants::MAX_INVENTORY_SIZE; ++slot) {
      CreateInventoryItem(owner, 1001u, 1, slot);
    }
  }

  entt::registry registry_;
  ecs::EventBus event_bus_;
  MerchantService service_;
  std::filesystem::path test_dir_;
};

TEST_F(MerchantServiceTest, LoadShopsParsesShopSequence) {
  const auto config_path = WriteShopConfig(
      "shops:\n"
      "  - store_id: 1001\n"
      "    name: basic\n"
      "    buyRate: 1.5\n"
      "    sellRate: 0.4\n"
      "    items:\n"
      "      - item_id: 2001\n"
      "        price: 10\n"
      "        stock: 5\n");

  service_.LoadShops(config_path.string());

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

TEST_F(MerchantServiceTest, LoadShopsParsesStoresAliasAndNormalizesRates) {
  const auto config_path = WriteShopConfig(
      "stores:\n"
      "  - store_id: 4001\n"
      "    name: alias_store\n"
      "    buy_rate: -2\n"
      "    sell_rate_pct: 0\n"
      "    items:\n"
      "      - item_id: 5001\n"
      "        price: 20\n"
      "        stock: 0\n");

  service_.LoadShops(config_path.string());

  const ShopConfig* parsed = service_.GetShop(4001);
  ASSERT_NE(parsed, nullptr);
  EXPECT_FLOAT_EQ(parsed->buy_rate, 1.0f);
  EXPECT_FLOAT_EQ(parsed->sell_rate, 0.5f);
  ASSERT_EQ(parsed->items.size(), 1u);
  EXPECT_EQ(parsed->items[0].item_id, 5001u);
  EXPECT_EQ(parsed->items[0].stock, -1);
  EXPECT_EQ(parsed->name, "alias_store");
}

TEST_F(MerchantServiceTest, BuyItemSucceedsAndDeductsGoldAndStock) {
  const auto config_path = WriteShopConfig(
      "shops:\n"
      "  - store_id: 1\n"
      "    buy_rate: 1.5\n"
      "    sell_rate: 0.5\n"
      "    items:\n"
      "      - item_id: 9001\n"
      "        price: 30\n"
      "        stock: 5\n");
  service_.LoadShops(config_path.string());

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
  const auto config_path = WriteShopConfig(
      "shops:\n"
      "  - store_id: 2\n"
      "    buy_rate: 1.0\n"
      "    items:\n"
      "      - item_id: 9101\n"
      "        price: 100\n"
      "        stock: 1\n"
      "  - store_id: 3\n"
      "    buy_rate: 2.0\n"
      "    items:\n"
      "      - item_id: 9201\n"
      "        price: 2147483647\n"
      "        stock: 1\n");
  service_.LoadShops(config_path.string());

  const entt::entity player = CreatePlayer(50, 0);

  EXPECT_FALSE(service_.BuyItem(player, 2, 9101, 0));
  EXPECT_FALSE(service_.BuyItem(player, 999, 9101, 1));
  EXPECT_FALSE(service_.BuyItem(player, 2, 9101, 2));
  EXPECT_FALSE(service_.BuyItem(player, 2, 9101, 1));
  EXPECT_FALSE(service_.BuyItem(player, 3, 9201, 1));
}

TEST_F(MerchantServiceTest, SellItemWithOpenShopRejectsStockOverflow) {
  const auto config_path = WriteShopConfig(
      "shops:\n"
      "  - store_id: 11\n"
      "    sell_rate: 1.0\n"
      "    items:\n"
      "      - item_id: 9301\n"
      "        price: 50\n"
      "        stock: 2147483647\n");
  service_.LoadShops(config_path.string());

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
  const auto config_path = WriteShopConfig(
      "shops:\n"
      "  - store_id: 21\n"
      "    sell_rate: 0.3\n"
      "    items:\n"
      "      - item_id: 9401\n"
      "        price: 10\n"
      "        stock: 10\n"
      "  - store_id: 22\n"
      "    sell_rate: 0.5\n"
      "    items:\n"
      "      - item_id: 9401\n"
      "        price: 20\n"
      "        stock: 1\n");
  service_.LoadShops(config_path.string());

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
  const auto config_path = WriteShopConfig(
      "shops:\n"
      "  - store_id: 31\n"
      "    sell_rate: 1.0\n"
      "    items:\n"
      "      - item_id: 9501\n"
      "        price: 100\n"
      "        stock: 5\n");
  service_.LoadShops(config_path.string());

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

TEST_F(MerchantServiceTest, LoadShopsHandlesMissingAndMalformedFileGracefully) {
  service_.LoadShops("");
  EXPECT_EQ(service_.GetShop(1), nullptr);

  service_.LoadShops((test_dir_ / "missing_file.yaml").string());
  EXPECT_EQ(service_.GetShop(1), nullptr);

  const auto bad = WriteShopConfig("shops: [", "broken.yaml");
  service_.LoadShops(bad.string());
  EXPECT_EQ(service_.GetShop(1), nullptr);
}

}  // namespace
}  // namespace mir2::logic::test
