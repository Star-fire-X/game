#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <cstdlib>
#include <filesystem>

#include "common/item_constants.h"
#include "common/types/constants.h"
#include "core/utils.h"
#include "data/item_template.h"
#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/guild_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/attribute_events.h"
#include "ecs/events/inventory_events.h"
#include "ecs/events/map_events.h"
#include "game/guild/guild_manager.h"
#include "game/item/item_effect_processor.h"
#include "game/map/map_instance.h"

namespace {

using mir2::data::ItemTemplateManager;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::CharacterStateComponent;
using mir2::ecs::DirtyComponent;
using mir2::ecs::EffectListComponent;
using mir2::ecs::EquipmentSlotComponent;
using mir2::ecs::InventoryOwnerComponent;
using mir2::ecs::ItemComponent;
using mir2::ecs::SkillComponent;
using mir2::ecs::SkillListComponent;
using mir2::ecs::events::ExperienceGainedEvent;
using mir2::ecs::events::LotteryDrawEvent;
using mir2::ecs::events::SkillLearnedEvent;
using mir2::ecs::events::TeleportRequestEvent;
using mir2::game::item::ItemEffectProcessor;
using mir2::game::map::MapAttributes;
using mir2::game::map::MapInstance;

std::filesystem::path GetItemEffectItemsPath() {
#ifdef MIR2_TEST_DATA_DIR
    const auto configured = std::filesystem::path(MIR2_TEST_DATA_DIR) / "item_effect_items.json";
    if (std::filesystem::exists(configured)) {
        return configured;
    }
#endif

    std::filesystem::path root = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto candidate = root / "tests" / "data" / "item_effect_items.json";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!root.has_parent_path()) {
            break;
        }
        root = root.parent_path();
    }

    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        / "data" / "item_effect_items.json";
}

void LoadItemEffectTemplates() {
    auto& manager = ItemTemplateManager::Instance();
    const auto path = GetItemEffectItemsPath();
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_TRUE(manager.LoadFromJson(path.string()));
}

entt::entity CreateCharacter(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<CharacterAttributesComponent>(entity);
    registry.emplace<CharacterIdentityComponent>(entity);
    registry.emplace<CharacterStateComponent>(entity);
    return entity;
}

entt::entity CreateItem(entt::registry& registry,
                        entt::entity owner,
                        uint32_t item_id,
                        int count,
                        int std_mode,
                        int slot_index = 0) {
    auto entity = registry.create();
    auto& item = registry.emplace<ItemComponent>(entity);
    item.item_id = item_id;
    item.count = count;
    item.std_mode = std_mode;

    auto& owner_component = registry.emplace<InventoryOwnerComponent>(entity);
    owner_component.owner = owner;
    owner_component.slot_index = slot_index;
    return entity;
}

int CountOwnedItems(entt::registry& registry, entt::entity owner) {
    int total = 0;
    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& owner_component = view.get<InventoryOwnerComponent>(entity);
        if (owner_component.owner == owner) {
            ++total;
        }
    }
    return total;
}

int CountOwnedItemsById(entt::registry& registry, entt::entity owner, uint32_t item_id) {
    int total = 0;
    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& owner_component = view.get<InventoryOwnerComponent>(entity);
        if (owner_component.owner != owner) {
            continue;
        }
        const auto& item = view.get<ItemComponent>(entity);
        if (item.item_id == item_id) {
            ++total;
        }
    }
    return total;
}

class ItemEffectProcessorTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ItemTemplateManager::Instance().Clear();
        LoadItemEffectTemplates();
    }

    void TearDown() override {
        ItemTemplateManager::Instance().Clear();
    }
};

}  // namespace

TEST_F(ItemEffectProcessorTest, ProcessDrugNormalAddsIncrementalRecovery) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.inc_health = 0;
    attributes.inc_spell = 0;

    const auto item = CreateItem(registry, character, 2001u, 1,
                                 mir2::common::item_std_mode::kDrug);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(attributes.inc_health, 30);
    EXPECT_EQ(attributes.inc_spell, 20);

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST_F(ItemEffectProcessorTest, ProcessDrugFastFillHealsAndRestoresMp) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.max_hp = 100;
    attributes.hp = 40;
    attributes.max_mp = 80;
    attributes.mp = 20;

    const auto item = CreateItem(registry, character, 2002u, 1,
                                 mir2::common::item_std_mode::kDrug);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(attributes.hp, 90);
    EXPECT_EQ(attributes.mp, 45);
}

TEST_F(ItemEffectProcessorTest, ProcessDrugFreeCurseSetsFlag) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.next_free_curse_item = false;

    const auto item = CreateItem(registry, character, 2003u, 1,
                                 mir2::common::item_std_mode::kDrug);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_TRUE(attributes.next_free_curse_item);
}

TEST_F(ItemEffectProcessorTest, ProcessScrollHomeScrollPublishesTeleportEvent) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    MapInstance map_instance(0, 10, 10);
    MapAttributes attrs;
    attrs.home_map = "0";
    attrs.home_x = 10;
    attrs.home_y = 15;
    map_instance.SetAttributes(attrs);
    registry.ctx().emplace<MapInstance*>(&map_instance);

    const auto character = CreateCharacter(registry);
    auto& state = registry.get<CharacterStateComponent>(character);
    state.is_in_activity = false;
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.pk_level = 0;

    TeleportRequestEvent captured{};
    int event_count = 0;
    event_bus.Subscribe<TeleportRequestEvent>([&](TeleportRequestEvent& event) {
        captured = event;
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2101u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(captured.target_map_id, 0);
    EXPECT_EQ(captured.target_x, 10);
    EXPECT_EQ(captured.target_y, 15);
}

TEST_F(ItemEffectProcessorTest, ProcessScrollRandomScrollPublishesTeleportEvent) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    MapInstance map_instance(0, 10, 10);
    MapAttributes attrs;
    attrs.no_random_move = false;
    attrs.fight3_zone = false;
    map_instance.SetAttributes(attrs);
    registry.ctx().emplace<MapInstance*>(&map_instance);

    const auto character = CreateCharacter(registry);
    auto& state = registry.get<CharacterStateComponent>(character);
    state.is_in_activity = false;
    state.last_random_scroll_time_ms = 0;

    TeleportRequestEvent captured{};
    int event_count = 0;
    event_bus.Subscribe<TeleportRequestEvent>([&](TeleportRequestEvent& event) {
        captured = event;
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2102u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(captured.target_map_id, 0);
    EXPECT_GE(captured.target_x, 0);
    EXPECT_GE(captured.target_y, 0);
    EXPECT_GT(state.last_random_scroll_time_ms, 0);
}

TEST_F(ItemEffectProcessorTest, ProcessScrollBlessOilChangesLuck) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& equipment = registry.emplace<EquipmentSlotComponent>(character);
    equipment.slots.fill(entt::null);

    auto weapon = registry.create();
    auto& weapon_item = registry.emplace<ItemComponent>(weapon);
    weapon_item.luck = 0;
    weapon_item.max_durability = 100;
    weapon_item.durability = 80;
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)] = weapon;

    const auto item = CreateItem(registry, character, 2103u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(std::abs(weapon_item.luck), 1);

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
    EXPECT_TRUE(dirty->equipment_dirty);
}

TEST_F(ItemEffectProcessorTest, ProcessScrollRepairOilRepairsWeapon) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& equipment = registry.emplace<EquipmentSlotComponent>(character);
    equipment.slots.fill(entt::null);

    auto weapon = registry.create();
    auto& weapon_item = registry.emplace<ItemComponent>(weapon);
    weapon_item.max_durability = 90;
    weapon_item.durability = 30;
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)] = weapon;

    const auto item = CreateItem(registry, character, 2104u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(weapon_item.durability, 50);
}

TEST_F(ItemEffectProcessorTest, ProcessScrollAbilityDrugAppliesEffect) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.attack = 10;
    attributes.magic_attack = 5;
    attributes.sc = 3;
    attributes.speed = 2;
    attributes.max_hp = 100;
    attributes.hp = 100;
    attributes.max_mp = 80;
    attributes.mp = 60;

    const auto item = CreateItem(registry, character, 2106u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));

    auto* effects = registry.try_get<EffectListComponent>(character);
    ASSERT_NE(effects, nullptr);
    EXPECT_THAT(effects->effects, ::testing::SizeIs(1));
    EXPECT_EQ(effects->effects.front().skill_id, 2106u);

    EXPECT_EQ(attributes.attack, 15);
    EXPECT_EQ(attributes.magic_attack, 8);
    EXPECT_EQ(attributes.sc, 5);
    EXPECT_EQ(attributes.speed, 3);
    EXPECT_EQ(attributes.max_hp, 110);
    EXPECT_EQ(attributes.max_mp, 100);
}

TEST_F(ItemEffectProcessorTest, ProcessScrollExpDrugGrantsExperience) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.level = 10;
    attributes.experience = 0;

    ExperienceGainedEvent captured{};
    int event_count = 0;
    event_bus.Subscribe<ExperienceGainedEvent>([&](ExperienceGainedEvent& event) {
        captured = event;
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2107u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(attributes.experience, 200);
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(captured.amount, 200);
}

TEST_F(ItemEffectProcessorTest, ProcessCastleScrollRequiresGuildOwnership) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    MapInstance map_instance(0, 10, 10);
    MapAttributes attrs;
    attrs.fight_zone = false;
    attrs.fight3_zone = false;
    map_instance.SetAttributes(attrs);
    registry.ctx().emplace<MapInstance*>(&map_instance);

    const auto character = CreateCharacter(registry);
    auto& state = registry.get<CharacterStateComponent>(character);
    state.is_in_activity = false;

    int event_count = 0;
    event_bus.Subscribe<TeleportRequestEvent>([&](TeleportRequestEvent&) {
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2108u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_FALSE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(event_count, 0);
}

TEST_F(ItemEffectProcessorTest, ProcessCastleScrollFailsWhenGuildNotOwning) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    MapInstance map_instance(0, 10, 10);
    MapAttributes attrs;
    attrs.fight_zone = false;
    attrs.fight3_zone = false;
    map_instance.SetAttributes(attrs);
    registry.ctx().emplace<MapInstance*>(&map_instance);

    const auto character = CreateCharacter(registry);
    auto& state = registry.get<CharacterStateComponent>(character);
    state.is_in_activity = false;

    auto& guild_manager = mir2::game::guild::GuildManager::Instance();
    const uint32_t guild_id = 2;
    guild_manager.CreateGuild(guild_id, "NoCastleGuild", character, registry);

    int event_count = 0;
    event_bus.Subscribe<TeleportRequestEvent>([&](TeleportRequestEvent&) {
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2108u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_FALSE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(event_count, 0);

    guild_manager.Clear(registry);
}

TEST_F(ItemEffectProcessorTest, ProcessCastleScrollPublishesTeleportWhenOwned) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    MapInstance map_instance(0, 10, 10);
    MapAttributes attrs;
    attrs.fight_zone = false;
    attrs.fight3_zone = false;
    map_instance.SetAttributes(attrs);
    registry.ctx().emplace<MapInstance*>(&map_instance);

    const auto character = CreateCharacter(registry);
    auto& state = registry.get<CharacterStateComponent>(character);
    state.is_in_activity = false;

    auto& guild_manager = mir2::game::guild::GuildManager::Instance();
    const uint32_t guild_id = 1;
    guild_manager.CreateGuild(guild_id, "TestGuild", character, registry);
    auto* guild = guild_manager.GetGuild(guild_id, registry);
    ASSERT_NE(guild, nullptr);
    guild->owns_castle = true;

    TeleportRequestEvent captured{};
    int event_count = 0;
    event_bus.Subscribe<TeleportRequestEvent>([&](TeleportRequestEvent& event) {
        captured = event;
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2108u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(captured.target_map_id, 3);
    EXPECT_EQ(captured.target_x, 330);
    EXPECT_EQ(captured.target_y, 330);

    guild_manager.Clear(registry);
}

TEST_F(ItemEffectProcessorTest, ProcessLotteryPublishesEvent) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);

    LotteryDrawEvent captured{};
    int event_count = 0;
    event_bus.Subscribe<LotteryDrawEvent>([&](LotteryDrawEvent& event) {
        captured = event;
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2105u, 1,
                                 mir2::common::item_std_mode::kScroll);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(captured.item_id, 2105u);
}

TEST_F(ItemEffectProcessorTest, ProcessSkillBookLearnsSkillWhenEligible) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& identity = registry.get<CharacterIdentityComponent>(character);
    identity.char_class = mir2::common::CharacterClass::MAGE;
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.level = 10;

    int event_count = 0;
    event_bus.Subscribe<SkillLearnedEvent>([&](SkillLearnedEvent&) {
        ++event_count;
    });

    const auto item = CreateItem(registry, character, 2201u, 1,
                                 mir2::common::item_std_mode::kSkillBook);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));

    auto view = registry.view<InventoryOwnerComponent, SkillComponent>();
    bool found = false;
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        const auto& skill = view.get<SkillComponent>(entity);
        if (owner.owner == character && skill.skill_id == 2201u) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(event_count, 1);
}

TEST_F(ItemEffectProcessorTest, ProcessSkillBookRejectsWrongClass) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& identity = registry.get<CharacterIdentityComponent>(character);
    identity.char_class = mir2::common::CharacterClass::WARRIOR;
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.level = 10;

    const auto item = CreateItem(registry, character, 2201u, 1,
                                 mir2::common::item_std_mode::kSkillBook);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_FALSE(processor.ProcessItemUse(character, item));
}

TEST_F(ItemEffectProcessorTest, ProcessSkillBookRejectsLowLevel) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& identity = registry.get<CharacterIdentityComponent>(character);
    identity.char_class = mir2::common::CharacterClass::MAGE;
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.level = 1;

    const auto item = CreateItem(registry, character, 2201u, 1,
                                 mir2::common::item_std_mode::kSkillBook);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_FALSE(processor.ProcessItemUse(character, item));
}

TEST_F(ItemEffectProcessorTest, ProcessSkillBookRejectsAlreadyLearned) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);
    auto& identity = registry.get<CharacterIdentityComponent>(character);
    identity.char_class = mir2::common::CharacterClass::MAGE;
    auto& attributes = registry.get<CharacterAttributesComponent>(character);
    attributes.level = 10;

    auto& skill_list = registry.emplace<SkillListComponent>(character);
    ASSERT_TRUE(skill_list.add_skill(2201u));

    const auto item = CreateItem(registry, character, 2201u, 1,
                                 mir2::common::item_std_mode::kSkillBook);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_FALSE(processor.ProcessItemUse(character, item));
}

TEST_F(ItemEffectProcessorTest, ProcessBundledDrugSplitsIntoPotions) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);

    const auto item = CreateItem(registry, character, 2301u, 1,
                                 mir2::common::item_std_mode::kBundledDrug, 0);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_TRUE(processor.ProcessItemUse(character, item));

    const auto& item_component = registry.get<ItemComponent>(item);
    EXPECT_EQ(item_component.item_id, 2001u);
    EXPECT_EQ(item_component.count, 2);
    EXPECT_EQ(CountOwnedItems(registry, character), 6);
    EXPECT_EQ(CountOwnedItemsById(registry, character, 2001u), 6);
}

TEST_F(ItemEffectProcessorTest, ProcessBundledDrugFailsWhenInventoryFull) {
    entt::registry registry;
    mir2::ecs::EventBus event_bus(registry);

    const auto character = CreateCharacter(registry);

    for (int slot = 0; slot < mir2::common::constants::MAX_INVENTORY_SIZE; ++slot) {
        if (slot == 0) {
            continue;
        }
        CreateItem(registry, character, 2001u, 1,
                   mir2::common::item_std_mode::kDrug, slot);
    }

    const auto item = CreateItem(registry, character, 2301u, 1,
                                 mir2::common::item_std_mode::kBundledDrug, 0);

    ItemEffectProcessor processor(registry, event_bus);
    EXPECT_FALSE(processor.ProcessItemUse(character, item));
    const auto& item_component = registry.get<ItemComponent>(item);
    EXPECT_EQ(item_component.item_id, 2301u);
    EXPECT_EQ(item_component.count, 1);
}
