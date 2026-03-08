#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <entt/entt.hpp>
#include <filesystem>

#include "common/item_constants.h"
#include "common/types/constants.h"
#include "data/item_template.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/components/npc_shop_component.h"
#include "ecs/systems/inventory_system.h"
#include "game/npc/npc_shop_service.h"

namespace {

using mir2::data::ItemTemplateManager;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::InventoryOwnerComponent;
using mir2::ecs::ItemComponent;
using mir2::ecs::NpcShopComponent;
using mir2::ecs::ShopItem;

std::filesystem::path GetTestItemsPath() {
#ifdef MIR2_TEST_DATA_DIR
    const auto configured = std::filesystem::path(MIR2_TEST_DATA_DIR) / "test_items.json";
    if (std::filesystem::exists(configured)) {
        return configured;
    }
#endif

    std::filesystem::path root = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto candidate = root / "tests" / "data" / "test_items.json";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!root.has_parent_path()) {
            break;
        }
        root = root.parent_path();
    }

    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        / "data" / "test_items.json";
}

void LoadShopTemplates() {
    auto& manager = ItemTemplateManager::Instance();
    const auto path = GetTestItemsPath();
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_TRUE(manager.LoadFromJson(path.string()));
}

entt::entity CreatePlayer(entt::registry& registry, int gold) {
    auto entity = registry.create();
    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.gold = gold;
    return entity;
}

entt::entity CreateNpcShop(entt::registry& registry,
                           std::initializer_list<ShopItem> items,
                           float tax_rate = 0.0f,
                           bool buy_back = true) {
    auto entity = registry.create();
    auto& shop = registry.emplace<NpcShopComponent>(entity);
    shop.tax_rate = tax_rate;
    shop.buy_back = buy_back;
    shop.items.assign(items.begin(), items.end());
    return entity;
}

void FillInventory(entt::registry& registry, entt::entity owner) {
    for (int slot = 0; slot < mir2::common::constants::MAX_INVENTORY_SIZE; ++slot) {
        auto item = registry.create();
        auto& item_component = registry.emplace<ItemComponent>(item);
        item_component.item_id = 1001u;
        item_component.count = 1;

        auto& owner_component = registry.emplace<InventoryOwnerComponent>(item);
        owner_component.owner = owner;
        owner_component.slot_index = slot;
    }
}

entt::entity CreateSellItem(entt::registry& registry, entt::entity owner,
                            uint32_t item_id, int count, int durability,
                            int max_durability = -1) {
    auto item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.count = count;
    if (max_durability < 0) {
        max_durability = durability;
    }
    item_component.max_durability = std::max(max_durability, 0);
    item_component.durability = durability;

    auto& owner_component = registry.emplace<InventoryOwnerComponent>(item);
    owner_component.owner = owner;
    owner_component.slot_index = 0;
    return item;
}

class NpcShopServiceTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ItemTemplateManager::Instance().Clear();
        LoadShopTemplates();
    }

    void TearDown() override {
        ItemTemplateManager::Instance().Clear();
    }
};

}  // namespace

TEST_F(NpcShopServiceTest, BuyItemFailsWhenGoldInsufficient) {
    entt::registry registry;
    const auto player = CreatePlayer(registry, 50);
    const auto npc = CreateNpcShop(registry, {{1001u, 0, -1}}, 0.0f);

    EXPECT_FALSE(mir2::game::npc::NpcShopService::BuyItem(
        registry, player, npc, 1001u, 1));
    const int count = mir2::ecs::InventorySystem::CountItem(registry, player, 1001u);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(registry.get<CharacterAttributesComponent>(player).gold, 50);
}

TEST_F(NpcShopServiceTest, BuyItemFailsWhenInventoryFull) {
    entt::registry registry;
    const auto player = CreatePlayer(registry, 500);
    const auto npc = CreateNpcShop(registry, {{1001u, 0, -1}}, 0.0f);

    FillInventory(registry, player);

    EXPECT_FALSE(mir2::game::npc::NpcShopService::BuyItem(
        registry, player, npc, 1001u, 1));
}

TEST_F(NpcShopServiceTest, BuyItemAppliesTaxAndDeductsGold) {
    entt::registry registry;
    const auto player = CreatePlayer(registry, 1000);
    const auto npc = CreateNpcShop(registry, {{1001u, 0, -1}}, 0.1f);

    EXPECT_TRUE(mir2::game::npc::NpcShopService::BuyItem(
        registry, player, npc, 1001u, 2));

    const auto& attributes = registry.get<CharacterAttributesComponent>(player);
    EXPECT_EQ(attributes.gold, 780);
    EXPECT_EQ(mir2::ecs::InventorySystem::CountItem(registry, player, 1001u), 2);
}

TEST_F(NpcShopServiceTest, SellItemAppliesTaxAndCreditsGold) {
    entt::registry registry;
    const auto player = CreatePlayer(registry, 0);
    const auto npc = CreateNpcShop(registry, {{1001u, 0, -1}}, 0.2f);

    auto item = CreateSellItem(registry, player, 1001u, 2, 5000);

    EXPECT_TRUE(mir2::game::npc::NpcShopService::SellItem(
        registry, player, npc, item));

    const auto& attributes = registry.get<CharacterAttributesComponent>(player);
    EXPECT_EQ(attributes.gold, 80);
    EXPECT_FALSE(registry.valid(item));
}

TEST_F(NpcShopServiceTest, SellItemFailsWhenDurabilityTooLow) {
    entt::registry registry;
    const auto player = CreatePlayer(registry, 0);
    const auto npc = CreateNpcShop(registry, {{1001u, 0, -1}}, 0.0f);

    auto item = CreateSellItem(registry, player, 1001u, 1, 3000, 5000);

    EXPECT_FALSE(mir2::game::npc::NpcShopService::SellItem(
        registry, player, npc, item));

    const auto& attributes = registry.get<CharacterAttributesComponent>(player);
    EXPECT_EQ(attributes.gold, 0);
    EXPECT_TRUE(registry.valid(item));
}
