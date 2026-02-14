#include <gtest/gtest.h>

#include <algorithm>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "common/character_data.h"
#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/systems/character_utils.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/level_up_system.h"
#include "ecs/systems/growth_formulas.h"
#include "ecs/systems/movement_system.h"
#include "ecs/persistence/character_codec.h"

namespace legend2 {
using mir2::common::CharacterClass;
using mir2::common::CharacterData;
using mir2::common::Direction;
using mir2::common::Gender;
using mir2::common::Position;
using json = nlohmann::json;

namespace {

CharacterData BuildFullCharacterData() {
    CharacterData data;
    data.id = 12345;
    data.account_id = "account_001";
    data.name = "TestWarrior";
    data.char_class = CharacterClass::WARRIOR;
    data.gender = Gender::MALE;
    data.stats.level = 50;
    data.stats.experience = 123456;
    data.stats.hp = 800;
    data.stats.max_hp = 1000;
    data.stats.mp = 200;
    data.stats.max_mp = 500;
    data.stats.attack = 150;
    data.stats.defense = 100;
    data.stats.magic_attack = 50;
    data.stats.magic_defense = 80;
    data.stats.speed = 30;
    data.stats.gold = 99999;
    data.map_id = 1001;
    data.position = {100, 200};
    data.inventory_json = R"([{"slot":0,"item":{"instance_id":0,"template_id":2001,"quantity":2,"durability":12,"enhancement_level":1}}])";
    data.equipment_json = R"([{"slot":0,"item":{"instance_id":0,"template_id":3001,"quantity":1,"durability":100,"enhancement_level":0}}])";
    data.skills_json = R"([{"id":4001,"level":3}])";
    data.created_at = 1700000000;
    data.last_login = 1700001234;
    return data;
}

entt::entity CreateEntityWithComponents(entt::registry& registry) {
    auto entity = registry.create();

    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = 1001;
    identity.account_id = "acc";
    identity.name = "Hero";
    identity.char_class = CharacterClass::MAGE;
    identity.gender = Gender::FEMALE;

    auto& state = registry.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = 7;
    state.position = {120, 250};
    state.direction = Direction::LEFT;
    state.created_at = 1700000000;
    state.last_login = 1700001234;
    state.last_active = 1700001234;

    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = 3;
    attributes.experience = 250;
    attributes.hp = 80;
    attributes.max_hp = 120;
    attributes.mp = 40;
    attributes.max_mp = 60;
    attributes.attack = 15;
    attributes.defense = 8;
    attributes.magic_attack = 22;
    attributes.magic_defense = 11;
    attributes.speed = 6;
    attributes.gold = 777;

    registry.emplace<mir2::ecs::InventorySnapshotComponent>(entity);

    auto& equipment = registry.emplace<mir2::ecs::EquipmentSlotComponent>(entity);
    equipment.slots.fill(entt::null);

    const auto bag_item = registry.create();
    auto& bag_item_component = registry.emplace<mir2::ecs::ItemComponent>(bag_item);
    bag_item_component.item_id = 2001;
    bag_item_component.count = 2;
    bag_item_component.durability = 12;
    bag_item_component.max_durability = 20;
    bag_item_component.enhancement_level = 1;
    auto& bag_owner = registry.emplace<mir2::ecs::InventoryOwnerComponent>(bag_item);
    bag_owner.owner = entity;
    bag_owner.slot_index = 0;

    const auto equipped_item = registry.create();
    auto& equipped_component = registry.emplace<mir2::ecs::ItemComponent>(equipped_item);
    equipped_component.item_id = 3001;
    equipped_component.count = 1;
    equipped_component.durability = 100;
    equipped_component.max_durability = 100;
    equipped_component.equip_slot = static_cast<int>(mir2::common::EquipSlot::WEAPON);
    auto& equipped_owner = registry.emplace<mir2::ecs::InventoryOwnerComponent>(equipped_item);
    equipped_owner.owner = entity;
    equipped_owner.slot_index = -1;
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)] = equipped_item;

    const auto learned_skill = registry.create();
    auto& learned_skill_component = registry.emplace<mir2::ecs::SkillComponent>(learned_skill);
    learned_skill_component.skill_id = 4001;
    learned_skill_component.level = 3;
    auto& skill_owner = registry.emplace<mir2::ecs::InventoryOwnerComponent>(learned_skill);
    skill_owner.owner = entity;
    skill_owner.slot_index = -1;

    registry.emplace<mir2::ecs::DirtyComponent>(entity, false, false, false, false);

    return entity;
}

}  // namespace

TEST(CharacterCodecTest, LoadCharacterEntity_AllComponents) {
    entt::registry registry;
    CharacterData data = BuildFullCharacterData();

    entt::entity entity = LoadCharacterEntity(registry, data);

    ASSERT_TRUE(registry.all_of<mir2::ecs::CharacterIdentityComponent>(entity));
    auto& identity = registry.get<mir2::ecs::CharacterIdentityComponent>(entity);
    EXPECT_EQ(identity.id, data.id);
    EXPECT_EQ(identity.account_id, data.account_id);
    EXPECT_EQ(identity.name, data.name);
    EXPECT_EQ(identity.char_class, data.char_class);
    EXPECT_EQ(identity.gender, data.gender);

    ASSERT_TRUE(registry.all_of<mir2::ecs::CharacterAttributesComponent>(entity));
    auto& attrs = registry.get<mir2::ecs::CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.level, data.stats.level);
    EXPECT_EQ(attrs.experience, data.stats.experience);
    EXPECT_EQ(attrs.hp, data.stats.hp);
    EXPECT_EQ(attrs.max_hp, data.stats.max_hp);
    EXPECT_EQ(attrs.gold, data.stats.gold);

    ASSERT_TRUE(registry.all_of<mir2::ecs::CharacterStateComponent>(entity));
    auto& state = registry.get<mir2::ecs::CharacterStateComponent>(entity);
    EXPECT_EQ(state.map_id, data.map_id);
    EXPECT_EQ(state.position.x, data.position.x);
    EXPECT_EQ(state.position.y, data.position.y);

    ASSERT_TRUE(registry.all_of<mir2::ecs::InventorySnapshotComponent>(entity));
    auto& inv = registry.get<mir2::ecs::InventorySnapshotComponent>(entity);
    ASSERT_TRUE(inv.slots[0].has_value());
    EXPECT_EQ(inv.slots[0]->item_id, 2001u);
    EXPECT_EQ(inv.slots[0]->count, 2);
    EXPECT_EQ(inv.slots[0]->durability, 12);

    ASSERT_TRUE(inv.equipment[0].has_value());
    EXPECT_EQ(inv.equipment[0]->item_id, 3001u);
    EXPECT_EQ(inv.equipment[0]->count, 1);

    ASSERT_EQ(inv.skills.size(), 1u);
    EXPECT_EQ(inv.skills[0].skill_id, 4001u);
    EXPECT_EQ(inv.skills[0].level, 3);

    ASSERT_TRUE(registry.all_of<mir2::ecs::DirtyComponent>(entity));
    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_FALSE(dirty.identity_dirty);
    EXPECT_FALSE(dirty.attributes_dirty);
    EXPECT_FALSE(dirty.state_dirty);
    EXPECT_FALSE(dirty.inventory_dirty);
}

TEST(CharacterCodecTest, LoadCharacterEntity_ParsesNumericAccountId) {
    entt::registry registry;
    CharacterData data;
    data.id = 77;
    data.account_id = "123456";
    data.name = "NumericAccount";

    entt::entity entity = LoadCharacterEntity(registry, data);
    const auto& identity =
        registry.get<mir2::ecs::CharacterIdentityComponent>(entity);
    EXPECT_EQ(identity.account_id, "123456");
    EXPECT_EQ(identity.account_id_value, 123456u);
    EXPECT_EQ(identity.ResolveAccountIdValue(), 123456u);
}

TEST(CharacterCodecTest, LoadCharacterEntity_MinimalData) {
    entt::registry registry;
    CharacterData data;
    data.id = 42;
    data.name = "Minimal";
    data.inventory_json = "";
    data.equipment_json = "";
    data.skills_json = "";

    entt::entity entity = LoadCharacterEntity(registry, data);

    EXPECT_TRUE(registry.all_of<mir2::ecs::CharacterIdentityComponent>(entity));
    EXPECT_TRUE(registry.all_of<mir2::ecs::CharacterAttributesComponent>(entity));
    EXPECT_TRUE(registry.all_of<mir2::ecs::CharacterStateComponent>(entity));
    EXPECT_TRUE(registry.all_of<mir2::ecs::DirtyComponent>(entity));
    EXPECT_TRUE(registry.all_of<mir2::ecs::InventorySnapshotComponent>(entity));
    const auto& inv = registry.get<mir2::ecs::InventorySnapshotComponent>(entity);
    EXPECT_TRUE(std::all_of(inv.slots.begin(), inv.slots.end(),
                            [](const auto& slot) { return !slot.has_value(); }));
    EXPECT_TRUE(std::all_of(inv.equipment.begin(), inv.equipment.end(),
                            [](const auto& slot) { return !slot.has_value(); }));
    EXPECT_TRUE(inv.skills.empty());
}

TEST(CharacterCodecTest, LoadCharacterEntity_DirtyFlagsInitialized) {
    entt::registry registry;
    CharacterData data = BuildFullCharacterData();

    entt::entity entity = LoadCharacterEntity(registry, data);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_FALSE(dirty.identity_dirty);
    EXPECT_FALSE(dirty.attributes_dirty);
    EXPECT_FALSE(dirty.state_dirty);
    EXPECT_FALSE(dirty.inventory_dirty);
}

TEST(CharacterCodecTest, SaveCharacterData_AllComponents) {
    entt::registry registry;
    entt::entity entity = CreateEntityWithComponents(registry);

    CharacterData data = SaveCharacterData(registry, entity);

    EXPECT_EQ(data.id, 1001u);
    EXPECT_EQ(data.account_id, "acc");
    EXPECT_EQ(data.name, "Hero");
    EXPECT_EQ(data.char_class, CharacterClass::MAGE);
    EXPECT_EQ(data.gender, Gender::FEMALE);
    EXPECT_EQ(data.stats.level, 3);
    EXPECT_EQ(data.stats.experience, 250);
    EXPECT_EQ(data.stats.hp, 80);
    EXPECT_EQ(data.stats.max_hp, 120);
    EXPECT_EQ(data.stats.mp, 40);
    EXPECT_EQ(data.stats.max_mp, 60);
    EXPECT_EQ(data.stats.attack, 15);
    EXPECT_EQ(data.stats.defense, 8);
    EXPECT_EQ(data.stats.magic_attack, 22);
    EXPECT_EQ(data.stats.magic_defense, 11);
    EXPECT_EQ(data.stats.speed, 6);
    EXPECT_EQ(data.stats.gold, 777);
    EXPECT_EQ(data.map_id, 7u);
    EXPECT_EQ(data.position.x, 120);
    EXPECT_EQ(data.position.y, 250);

    const auto inventory_json = json::parse(data.inventory_json);
    ASSERT_TRUE(inventory_json.is_array());
    ASSERT_EQ(inventory_json.size(), 1u);
    EXPECT_EQ(inventory_json[0].at("slot").get<int>(), 0);
    EXPECT_EQ(inventory_json[0].at("item").at("template_id").get<uint32_t>(), 2001u);
    EXPECT_EQ(inventory_json[0].at("item").at("quantity").get<int>(), 2);

    const auto equipment_json = json::parse(data.equipment_json);
    ASSERT_TRUE(equipment_json.is_array());
    ASSERT_EQ(equipment_json.size(), 1u);
    EXPECT_EQ(equipment_json[0].at("slot").get<int>(), 0);
    EXPECT_EQ(equipment_json[0].at("item").at("template_id").get<uint32_t>(), 3001u);

    const auto skills_json = json::parse(data.skills_json);
    ASSERT_TRUE(skills_json.is_array());
    ASSERT_EQ(skills_json.size(), 1u);
    EXPECT_EQ(skills_json[0].at("id").get<uint32_t>(), 4001u);
    EXPECT_EQ(skills_json[0].at("level").get<int>(), 3);

    EXPECT_EQ(data.created_at, 1700000000);
    EXPECT_EQ(data.last_login, 1700001234);
}

TEST(CharacterCodecTest, SaveCharacterData_FallsBackToNumericAccountId) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = 9001;
    identity.name = "NumericOnly";
    identity.SetAccountIdValue(778899);
    identity.account_id.clear();

    CharacterData data = SaveCharacterData(registry, entity);
    EXPECT_EQ(data.account_id, "778899");
}

TEST(CharacterCodecTest, SaveCharacterData_KeepsAccountIdEmptyWhenInvalid) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = 9002;
    identity.name = "NoAccount";

    CharacterData data = SaveCharacterData(registry, entity);
    EXPECT_TRUE(data.account_id.empty());
}

TEST(CharacterCodecTest, SaveCharacterData_MissingComponents) {
    entt::registry registry;
    entt::entity entity = registry.create();

    CharacterData data = SaveCharacterData(registry, entity);

    EXPECT_EQ(data.id, 0u);
    EXPECT_EQ(data.name, "");
    EXPECT_EQ(data.char_class, CharacterClass::WARRIOR);
    EXPECT_EQ(data.gender, Gender::MALE);
    EXPECT_EQ(data.map_id, 1u);
    EXPECT_EQ(data.position.x, 100);
    EXPECT_EQ(data.position.y, 100);
}

TEST(CharacterCodecTest, RoundTripPersistence) {
    entt::registry registry;
    CharacterData original_data = BuildFullCharacterData();

    entt::entity entity = LoadCharacterEntity(registry, original_data);
    CharacterData loaded_data = SaveCharacterData(registry, entity);

    EXPECT_EQ(json::parse(loaded_data.inventory_json), json::parse(original_data.inventory_json));
    EXPECT_EQ(json::parse(loaded_data.equipment_json), json::parse(original_data.equipment_json));
    EXPECT_EQ(json::parse(loaded_data.skills_json), json::parse(original_data.skills_json));

    loaded_data.inventory_json = original_data.inventory_json;
    loaded_data.equipment_json = original_data.equipment_json;
    loaded_data.skills_json = original_data.skills_json;
    EXPECT_EQ(loaded_data, original_data);
}

TEST(CharacterCodecTest, MovementSystemIntegration) {
    entt::registry registry;
    entt::entity entity = registry.create();
    registry.emplace<mir2::ecs::CharacterStateComponent>(entity);

    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_EQ(dirty, nullptr);

    mir2::ecs::MovementSystem::SetPosition(registry, entity, 100, 200);
    auto& state = registry.get<mir2::ecs::CharacterStateComponent>(entity);
    EXPECT_EQ(state.position.x, 100);
    EXPECT_EQ(state.position.y, 200);

    mir2::ecs::MovementSystem::SetMapId(registry, entity, 9);
    EXPECT_EQ(state.map_id, 9u);

    mir2::ecs::MovementSystem::SetDirection(registry, entity, Direction::UP_LEFT);
    EXPECT_EQ(state.direction, Direction::UP_LEFT);

    auto& dirty_state = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_TRUE(dirty_state.state_dirty);
}

TEST(CharacterCodecTest, CombatSystemIntegration_DamageAndHeal) {
    entt::registry registry;
    entt::entity entity = registry.create();
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.hp = 50;
    attributes.max_hp = 100;

    EXPECT_EQ(mir2::ecs::CombatSystem::TakeDamage(registry, entity, 15), 15);
    EXPECT_EQ(attributes.hp, 35);
    EXPECT_EQ(mir2::ecs::CombatSystem::TakeDamage(registry, entity, 0), 0);
    EXPECT_EQ(attributes.hp, 35);

    EXPECT_EQ(mir2::ecs::CombatSystem::Heal(registry, entity, 40), 40);
    EXPECT_EQ(attributes.hp, 75);
    EXPECT_EQ(mir2::ecs::CombatSystem::Heal(registry, entity, 50), 25);
    EXPECT_EQ(attributes.hp, 100);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_TRUE(dirty.attributes_dirty);
}

TEST(CharacterCodecTest, CombatSystemIntegration_MPOperations) {
    entt::registry registry;
    entt::entity entity = registry.create();
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.mp = 10;
    attributes.max_mp = 20;

    EXPECT_EQ(mir2::ecs::CombatSystem::RestoreMP(registry, entity, 15), 10);
    EXPECT_EQ(attributes.mp, 20);
    EXPECT_EQ(mir2::ecs::CombatSystem::RestoreMP(registry, entity, 0), 0);
    EXPECT_EQ(attributes.mp, 20);

    EXPECT_FALSE(mir2::ecs::CombatSystem::ConsumeMP(registry, entity, 25));
    EXPECT_EQ(attributes.mp, 20);

    EXPECT_TRUE(mir2::ecs::CombatSystem::ConsumeMP(registry, entity, 5));
    EXPECT_EQ(attributes.mp, 15);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_TRUE(dirty.attributes_dirty);
}

TEST(CharacterCodecTest, CombatSystemIntegration_Respawn) {
    entt::registry registry;
    entt::entity entity = registry.create();
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.max_hp = 100;
    attributes.hp = 20;
    attributes.max_mp = 50;
    attributes.mp = 10;

    Position respawn_pos{5, 6};
    mir2::ecs::CombatSystem::Respawn(registry, entity, respawn_pos, 0.0f, 0.5f);

    auto& state = registry.get<mir2::ecs::CharacterStateComponent>(entity);
    EXPECT_EQ(state.position.x, 5);
    EXPECT_EQ(state.position.y, 6);
    EXPECT_EQ(attributes.hp, 1);
    EXPECT_EQ(attributes.mp, 25);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_TRUE(dirty.attributes_dirty);
}

TEST(CharacterCodecTest, LevelUpSystemIntegration_SingleLevel) {
    entt::registry registry;
    entt::entity entity = registry.create();

    registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity, uint32_t{1}, "acc", "Test", CharacterClass::WARRIOR,
                                                           Gender::MALE);
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = 1;
    attributes.experience = 0;
    attributes.hp = 100;
    attributes.max_hp = 100;
    attributes.mp = 50;
    attributes.max_mp = 50;

    registry.emplace<mir2::ecs::DirtyComponent>(entity, false, false, false, false);

    const int64_t exp_needed = mir2::ecs::GrowthFormulas::GetExpForLevel(1);
    const int old_max_hp = attributes.max_hp;

    bool leveled_up = mir2::ecs::LevelUpSystem::GainExperience(registry, entity, exp_needed);

    EXPECT_TRUE(leveled_up);
    EXPECT_EQ(attributes.level, 2);
    EXPECT_EQ(attributes.experience, 0);
    EXPECT_GT(attributes.max_hp, 0);
    EXPECT_LE(attributes.hp, attributes.max_hp);
    EXPECT_LE(attributes.mp, attributes.max_mp);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_TRUE(dirty.attributes_dirty);
}

TEST(CharacterCodecTest, LevelUpSystemIntegration_MultiLevel) {
    entt::registry registry;
    entt::entity entity = registry.create();

    registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity, uint32_t{1}, "acc", "Test",
                                                           CharacterClass::TAOIST, Gender::FEMALE);
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = 1;
    attributes.experience = 0;
    attributes.hp = 100;
    attributes.max_hp = 100;
    attributes.mp = 80;
    attributes.max_mp = 80;

    registry.emplace<mir2::ecs::DirtyComponent>(entity, false, false, false, false);

    // 使用新经验表计算升5级所需总经验
    int64_t total_exp_needed = 0;
    for (int level = 1; level < 6; ++level) {
        total_exp_needed += mir2::ecs::GrowthFormulas::GetExpForLevel(level);
    }

    bool leveled_up = mir2::ecs::LevelUpSystem::GainExperience(registry, entity, total_exp_needed);

    EXPECT_TRUE(leveled_up);
    EXPECT_EQ(attributes.level, 6);
    EXPECT_GT(attributes.max_hp, 0);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_TRUE(dirty.attributes_dirty);
}

TEST(CharacterCodecTest, GainExperience_NegativeValue) {
    entt::registry registry;
    entt::entity entity = registry.create();

    registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity, uint32_t{1}, "acc", "Test",
                                                           CharacterClass::WARRIOR, Gender::MALE);
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = 2;
    attributes.experience = 100;

    registry.emplace<mir2::ecs::DirtyComponent>(entity, false, false, false, false);

    bool leveled_up = mir2::ecs::LevelUpSystem::GainExperience(registry, entity, -5);

    EXPECT_FALSE(leveled_up);
    EXPECT_EQ(attributes.experience, 100);

    auto& dirty = registry.get<mir2::ecs::DirtyComponent>(entity);
    EXPECT_FALSE(dirty.attributes_dirty);
}

TEST(CharacterCodecTest, SpendGold_InsufficientFunds) {
    entt::registry registry;
    entt::entity entity = registry.create();
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.gold = 100;

    EXPECT_FALSE(mir2::ecs::CharacterUtils::SpendGold(registry, entity, 200));
    EXPECT_EQ(attributes.gold, 100);
}

TEST(CharacterCodecTest, SystemCalls_MissingEntity) {
    entt::registry registry;
    entt::entity entity = registry.create();

    EXPECT_EQ(mir2::ecs::CombatSystem::TakeDamage(registry, entity, 5), 0);
    EXPECT_EQ(mir2::ecs::CombatSystem::Heal(registry, entity, 10), 0);
    EXPECT_EQ(mir2::ecs::CombatSystem::RestoreMP(registry, entity, 10), 0);
    EXPECT_FALSE(mir2::ecs::CombatSystem::ConsumeMP(registry, entity, 1));
    EXPECT_FALSE(mir2::ecs::LevelUpSystem::GainExperience(registry, entity, 100));

    mir2::ecs::MovementSystem::SetPosition(registry, entity, 10, 20);
    auto& state = registry.get<mir2::ecs::CharacterStateComponent>(entity);
    EXPECT_EQ(state.position.x, 10);
    EXPECT_EQ(state.position.y, 20);
}

}  // namespace legend2
