#include <gtest/gtest.h>

#include <filesystem>

#include "data/item_template.h"

namespace {

using mir2::data::ItemTemplate;
using mir2::data::ItemTemplateManager;

std::filesystem::path GetTestItemsPath() {
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

void LoadTestItems(ItemTemplateManager& manager) {
    const auto path = GetTestItemsPath();
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_TRUE(manager.LoadFromJson(path.string()));
}

class ItemTemplateManagerTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ItemTemplateManager::Instance().Clear();
    }

    void TearDown() override {
        ItemTemplateManager::Instance().Clear();
    }
};

}  // namespace

TEST_F(ItemTemplateManagerTest, InstanceReturnsSingleton) {
    auto& first = ItemTemplateManager::Instance();
    auto& second = ItemTemplateManager::Instance();
    EXPECT_EQ(&first, &second);
}

TEST_F(ItemTemplateManagerTest, LoadFromJsonLoadsTemplates) {
    auto& manager = ItemTemplateManager::Instance();
    LoadTestItems(manager);

    EXPECT_EQ(manager.Count(), 5u);

    const ItemTemplate* potion = manager.GetTemplate(1001u);
    ASSERT_NE(potion, nullptr);
    EXPECT_EQ(potion->name, "Small Heal");
    EXPECT_EQ(potion->ac, 50u);
    EXPECT_TRUE(potion->stackable);
    EXPECT_EQ(potion->stack_limit, 20);
}

TEST_F(ItemTemplateManagerTest, LoadFromJsonMissingFileReturnsFalse) {
    auto& manager = ItemTemplateManager::Instance();
    EXPECT_FALSE(manager.LoadFromJson("nonexistent_items.json"));
    EXPECT_EQ(manager.Count(), 0u);
}

TEST_F(ItemTemplateManagerTest, GetTemplateReturnsExistingTemplate) {
    auto& manager = ItemTemplateManager::Instance();
    LoadTestItems(manager);

    const ItemTemplate* scroll = manager.GetTemplate(1002u);
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->name, "Town Scroll");
    EXPECT_EQ(scroll->std_mode, 3u);
}

TEST_F(ItemTemplateManagerTest, GetTemplateReturnsNullptrForMissingTemplate) {
    auto& manager = ItemTemplateManager::Instance();
    LoadTestItems(manager);

    EXPECT_EQ(manager.GetTemplate(9999u), nullptr);
}

TEST_F(ItemTemplateManagerTest, HelperMethodsMatchStdMode) {
    auto& manager = ItemTemplateManager::Instance();
    LoadTestItems(manager);

    const ItemTemplate* drug = manager.GetTemplate(1001u);
    const ItemTemplate* scroll = manager.GetTemplate(1002u);
    const ItemTemplate* book = manager.GetTemplate(1003u);
    const ItemTemplate* amulet = manager.GetTemplate(1004u);
    const ItemTemplate* bundle = manager.GetTemplate(1005u);

    ASSERT_NE(drug, nullptr);
    ASSERT_NE(scroll, nullptr);
    ASSERT_NE(book, nullptr);
    ASSERT_NE(amulet, nullptr);
    ASSERT_NE(bundle, nullptr);

    EXPECT_TRUE(drug->IsDrug());
    EXPECT_FALSE(drug->IsScroll());
    EXPECT_FALSE(drug->IsSkillBook());
    EXPECT_FALSE(drug->IsAmulet());
    EXPECT_FALSE(drug->IsBundledDrug());

    EXPECT_TRUE(scroll->IsScroll());
    EXPECT_TRUE(book->IsSkillBook());
    EXPECT_TRUE(amulet->IsAmulet());
    EXPECT_TRUE(bundle->IsBundledDrug());
}
