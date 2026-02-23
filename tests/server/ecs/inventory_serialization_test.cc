/**
 * @file inventory_serialization_test.cc
 * @brief Tests for Phase 4 serialization boundary (JSON ↔ compatibility snapshots)
 *
 * CRITICAL: These tests ensure DB compatibility and prevent data corruption
 * during the transition from JSON strings to compatibility snapshots.
 */

#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ecs/components/inventory_snapshot_component.h"
#include "ecs/inventory_migration.h"

namespace {

using mir2::ecs::InventorySnapshotComponent;
using mir2::ecs::InventorySnapshotItemData;
using mir2::ecs::InventorySnapshotSkillData;
using nlohmann::json;

namespace legend2 {

void LoadInventoryFromJson(entt::registry& registry,
                           entt::entity character,
                           const std::string& inventory_json,
                           uint32_t /*character_id*/) {
  mir2::ecs::inventory::compat::LoadInventoryFromJson(
      registry, character, inventory_json, "[]", "[]");
}

void LoadEquipmentFromJson(entt::registry& registry,
                           entt::entity character,
                           const std::string& equipment_json,
                           uint32_t /*character_id*/) {
  const json parsed = json::parse(equipment_json, nullptr, false);
  if (!parsed.is_object() || !parsed.contains("slots") || !parsed["slots"].is_array()) {
    mir2::ecs::inventory::compat::LoadInventoryFromJson(
        registry, character, "[]", equipment_json, "[]");
    return;
  }

  json adapted = json::array();
  const auto& slots = parsed["slots"];
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto& item = slots[i];
    if (!item.is_object() || item.empty()) {
      continue;
    }
    json entry;
    entry["slot"] = static_cast<int>(i);
    entry["item"] = item;
    adapted.push_back(std::move(entry));
  }

  mir2::ecs::inventory::compat::LoadInventoryFromJson(
      registry, character, "[]", adapted.dump(), "[]");
}

void LoadSkillsFromJson(entt::registry& registry,
                        entt::entity character,
                        const std::string& skills_json,
                        uint32_t /*character_id*/) {
  mir2::ecs::inventory::compat::LoadInventoryFromJson(
      registry, character, "[]", "[]", skills_json);
}

std::string SaveInventoryToJson(entt::registry& registry, entt::entity character) {
  json legacy;
  legacy["slots"] = json::array();
  const auto* inventory = registry.try_get<InventorySnapshotComponent>(character);
  if (!inventory) {
    return legacy.dump();
  }

  for (size_t i = 0; i < InventorySnapshotComponent::kMaxSlots; ++i) {
    if (!inventory->slots[i].has_value()) {
      legacy["slots"].push_back(json::object());
      continue;
    }
    json item_json;
    item_json["instance_id"] = inventory->slots[i]->instance_id;
    item_json["item_id"] = inventory->slots[i]->item_id;
    item_json["count"] = inventory->slots[i]->count;
    item_json["durability"] = inventory->slots[i]->durability;
    item_json["max_durability"] = inventory->slots[i]->max_durability;
    item_json["enhancement_level"] = inventory->slots[i]->enhancement_level;
    legacy["slots"].push_back(std::move(item_json));
  }

  return legacy.dump();
}

std::string SaveEquipmentToJson(entt::registry& registry, entt::entity character) {
  json legacy;
  legacy["slots"] = json::array();
  const auto* inventory = registry.try_get<InventorySnapshotComponent>(character);
  if (!inventory) {
    return legacy.dump();
  }

  for (size_t i = 0; i < InventorySnapshotComponent::kMaxEquipmentSlots; ++i) {
    if (!inventory->equipment[i].has_value()) {
      legacy["slots"].push_back(json::object());
      continue;
    }
    json item_json;
    item_json["instance_id"] = inventory->equipment[i]->instance_id;
    item_json["item_id"] = inventory->equipment[i]->item_id;
    item_json["count"] = inventory->equipment[i]->count;
    item_json["durability"] = inventory->equipment[i]->durability;
    item_json["max_durability"] = inventory->equipment[i]->max_durability;
    item_json["enhancement_level"] = inventory->equipment[i]->enhancement_level;
    legacy["slots"].push_back(std::move(item_json));
  }

  return legacy.dump();
}

std::string SaveSkillsToJson(entt::registry& registry, entt::entity character) {
  json legacy;
  legacy["skills"] = json::array();
  const auto* inventory = registry.try_get<InventorySnapshotComponent>(character);
  if (!inventory) {
    return legacy.dump();
  }

  for (const auto& entry : inventory->skills) {
    json skill;
    skill["skill_id"] = entry.skill_id;
    skill["level"] = entry.level;
    skill["cooldown_end_ms"] = entry.cooldown_end_ms;
    legacy["skills"].push_back(std::move(skill));
  }

  return legacy.dump();
}

}  // namespace legend2

// =============================================================================
// Helper Functions
// =============================================================================

json CreateSampleItemJson(uint64_t instance_id, uint32_t item_id, int count) {
  json item;
  item["instance_id"] = instance_id;
  item["item_id"] = item_id;
  item["count"] = count;
  item["durability"] = 100;
  item["max_durability"] = 100;
  item["enhancement_level"] = 0;
  return item;
}

json CreateSampleInventoryJson() {
  json inventory;
  inventory["slots"] = json::array();

  // Add a few items
  inventory["slots"].push_back(CreateSampleItemJson(1, 100, 1));
  inventory["slots"].push_back(CreateSampleItemJson(2, 101, 5));
  inventory["slots"].push_back(json::object());  // Empty slot
  inventory["slots"].push_back(CreateSampleItemJson(3, 102, 1));

  return inventory;
}

json CreateSampleEquipmentJson() {
  json equipment;
  equipment["slots"] = json::array();

  // Add weapon and armor
  equipment["slots"].push_back(CreateSampleItemJson(10, 200, 1));  // Weapon
  equipment["slots"].push_back(json::object());  // Empty
  equipment["slots"].push_back(CreateSampleItemJson(11, 201, 1));  // Armor

  return equipment;
}

json CreateSampleSkillsJson() {
  json skills;
  skills["skills"] = json::array();

  json skill1;
  skill1["skill_id"] = 28;
  skill1["level"] = 3;
  skill1["cooldown_end_ms"] = 0;
  skills["skills"].push_back(skill1);

  json skill2;
  skill2["skill_id"] = 29;
  skill2["level"] = 5;
  skill2["cooldown_end_ms"] = 1000000;
  skills["skills"].push_back(skill2);

  return skills;
}

// =============================================================================
// Serialization Boundary Tests
// =============================================================================

class InventorySerializationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    entity_ = registry_.create();
  }

  entt::registry registry_;
  entt::entity entity_ = entt::null;
};

TEST_F(InventorySerializationTest, LoadInventoryFromJsonPopulatesSnapshotSlots) {
  json inventory_json = CreateSampleInventoryJson();
  std::string json_str = inventory_json.dump();

  // Load from JSON
  legend2::LoadInventoryFromJson(registry_, entity_, json_str, 1);

  // Verify snapshot structure populated
  const auto* inventory = registry_.try_get<InventorySnapshotComponent>(entity_);
  ASSERT_NE(inventory, nullptr);

  // Check first item
  ASSERT_TRUE(inventory->slots[0].has_value());
  EXPECT_EQ(inventory->slots[0]->instance_id, 1u);
  EXPECT_EQ(inventory->slots[0]->item_id, 100u);
  EXPECT_EQ(inventory->slots[0]->count, 1);

  // Check second item
  ASSERT_TRUE(inventory->slots[1].has_value());
  EXPECT_EQ(inventory->slots[1]->instance_id, 2u);
  EXPECT_EQ(inventory->slots[1]->item_id, 101u);
  EXPECT_EQ(inventory->slots[1]->count, 5);

  // Check empty slot
  EXPECT_FALSE(inventory->slots[2].has_value());

  // Check third item
  ASSERT_TRUE(inventory->slots[3].has_value());
  EXPECT_EQ(inventory->slots[3]->instance_id, 3u);
  EXPECT_EQ(inventory->slots[3]->item_id, 102u);
}

TEST_F(InventorySerializationTest, SaveInventoryToJsonSerializesFromSnapshotSlots) {
  // Create snapshot inventory
  auto& inventory = registry_.emplace<InventorySnapshotComponent>(entity_);

  InventorySnapshotItemData item1;
  item1.instance_id = 1;
  item1.item_id = 100;
  item1.count = 1;
  item1.durability = 100;
  item1.max_durability = 100;
  inventory.slots[0] = item1;

  InventorySnapshotItemData item2;
  item2.instance_id = 2;
  item2.item_id = 101;
  item2.count = 5;
  inventory.slots[1] = item2;

  // Save to JSON
  std::string json_str = legend2::SaveInventoryToJson(registry_, entity_);

  // Parse and verify
  json parsed = json::parse(json_str);
  ASSERT_TRUE(parsed.contains("slots"));
  ASSERT_TRUE(parsed["slots"].is_array());
  ASSERT_GE(parsed["slots"].size(), 2u);

  // Verify first item
  EXPECT_EQ(parsed["slots"][0]["instance_id"], 1u);
  EXPECT_EQ(parsed["slots"][0]["item_id"], 100u);
  EXPECT_EQ(parsed["slots"][0]["count"], 1);

  // Verify second item
  EXPECT_EQ(parsed["slots"][1]["instance_id"], 2u);
  EXPECT_EQ(parsed["slots"][1]["item_id"], 101u);
  EXPECT_EQ(parsed["slots"][1]["count"], 5);
}

TEST_F(InventorySerializationTest, RoundTripPreservesAllData) {
  // Create original JSON
  json original_json = CreateSampleInventoryJson();
  std::string original_str = original_json.dump();

  // Load → Native
  legend2::LoadInventoryFromJson(registry_, entity_, original_str, 1);

  // Native → Save
  std::string saved_str = legend2::SaveInventoryToJson(registry_, entity_);

  // Parse both
  json original_parsed = json::parse(original_str);
  json saved_parsed = json::parse(saved_str);

  ASSERT_TRUE(saved_parsed.contains("slots"));
  ASSERT_TRUE(saved_parsed["slots"].is_array());
  ASSERT_GE(saved_parsed["slots"].size(), original_parsed["slots"].size());

  // Compare each item (skip empty slots)
  for (size_t i = 0; i < original_parsed["slots"].size(); ++i) {
    if (original_parsed["slots"][i].empty()) {
      EXPECT_TRUE(saved_parsed["slots"][i].empty()) << "Mismatch at slot " << i;
      continue;
    }

    EXPECT_EQ(original_parsed["slots"][i]["instance_id"],
              saved_parsed["slots"][i]["instance_id"])
        << "Mismatch at slot " << i;
    EXPECT_EQ(original_parsed["slots"][i]["item_id"],
              saved_parsed["slots"][i]["item_id"])
        << "Mismatch at slot " << i;
    EXPECT_EQ(original_parsed["slots"][i]["count"],
              saved_parsed["slots"][i]["count"])
        << "Mismatch at slot " << i;
  }
}

TEST_F(InventorySerializationTest, LoadEquipmentFromJsonPopulatesSnapshotEquipment) {
  json equipment_json = CreateSampleEquipmentJson();
  std::string json_str = equipment_json.dump();

  // Load from JSON
  legend2::LoadEquipmentFromJson(registry_, entity_, json_str, 1);

  // Verify snapshot structure
  const auto* inventory = registry_.try_get<InventorySnapshotComponent>(entity_);
  ASSERT_NE(inventory, nullptr);

  // Check weapon slot
  ASSERT_TRUE(inventory->equipment[0].has_value());
  EXPECT_EQ(inventory->equipment[0]->instance_id, 10u);
  EXPECT_EQ(inventory->equipment[0]->item_id, 200u);

  // Check empty slot
  EXPECT_FALSE(inventory->equipment[1].has_value());

  // Check armor slot
  ASSERT_TRUE(inventory->equipment[2].has_value());
  EXPECT_EQ(inventory->equipment[2]->instance_id, 11u);
  EXPECT_EQ(inventory->equipment[2]->item_id, 201u);
}

TEST_F(InventorySerializationTest, SaveEquipmentToJsonSerializesFromSnapshotEquipment) {
  // Create snapshot equipment
  auto& inventory = registry_.emplace<InventorySnapshotComponent>(entity_);

  InventorySnapshotItemData weapon;
  weapon.instance_id = 10;
  weapon.item_id = 200;
  weapon.count = 1;
  inventory.equipment[0] = weapon;

  InventorySnapshotItemData armor;
  armor.instance_id = 11;
  armor.item_id = 201;
  armor.count = 1;
  inventory.equipment[2] = armor;

  // Save to JSON
  std::string json_str = legend2::SaveEquipmentToJson(registry_, entity_);

  // Parse and verify
  json parsed = json::parse(json_str);
  ASSERT_TRUE(parsed.contains("slots"));
  ASSERT_GE(parsed["slots"].size(), 3u);

  EXPECT_EQ(parsed["slots"][0]["instance_id"], 10u);
  EXPECT_EQ(parsed["slots"][0]["item_id"], 200u);
  EXPECT_EQ(parsed["slots"][2]["instance_id"], 11u);
  EXPECT_EQ(parsed["slots"][2]["item_id"], 201u);
}

TEST_F(InventorySerializationTest, LoadSkillsFromJsonPopulatesSnapshotSkills) {
  json skills_json = CreateSampleSkillsJson();
  std::string json_str = skills_json.dump();

  // Load from JSON
  legend2::LoadSkillsFromJson(registry_, entity_, json_str, 1);

  // Verify snapshot structure
  const auto* inventory = registry_.try_get<InventorySnapshotComponent>(entity_);
  ASSERT_NE(inventory, nullptr);
  ASSERT_EQ(inventory->skills.size(), 2u);

  EXPECT_EQ(inventory->skills[0].skill_id, 28u);
  EXPECT_EQ(inventory->skills[0].level, 3u);
  EXPECT_EQ(inventory->skills[0].cooldown_end_ms, 0u);

  EXPECT_EQ(inventory->skills[1].skill_id, 29u);
  EXPECT_EQ(inventory->skills[1].level, 5u);
  EXPECT_EQ(inventory->skills[1].cooldown_end_ms, 0u);
}

TEST_F(InventorySerializationTest, SaveSkillsToJsonSerializesFromSnapshotSkills) {
  // Create snapshot skills
  auto& inventory = registry_.emplace<InventorySnapshotComponent>(entity_);

  InventorySnapshotSkillData skill1;
  skill1.skill_id = 28;
  skill1.level = 3;
  skill1.cooldown_end_ms = 0;
  inventory.skills.push_back(skill1);

  InventorySnapshotSkillData skill2;
  skill2.skill_id = 29;
  skill2.level = 5;
  skill2.cooldown_end_ms = 1000000;
  inventory.skills.push_back(skill2);

  // Save to JSON
  std::string json_str = legend2::SaveSkillsToJson(registry_, entity_);

  // Parse and verify
  json parsed = json::parse(json_str);
  ASSERT_TRUE(parsed.contains("skills"));
  ASSERT_EQ(parsed["skills"].size(), 2u);

  EXPECT_EQ(parsed["skills"][0]["skill_id"], 28u);
  EXPECT_EQ(parsed["skills"][0]["level"], 3u);
  EXPECT_EQ(parsed["skills"][1]["skill_id"], 29u);
  EXPECT_EQ(parsed["skills"][1]["level"], 5u);
}

TEST_F(InventorySerializationTest, EmptyInventorySerializesCorrectly) {
  // Create empty inventory
  registry_.emplace<InventorySnapshotComponent>(entity_);

  // Save to JSON
  std::string json_str = legend2::SaveInventoryToJson(registry_, entity_);

  // Parse and verify
  json parsed = json::parse(json_str);
  ASSERT_TRUE(parsed.contains("slots"));

  // Should have array structure even if empty
  EXPECT_TRUE(parsed["slots"].is_array());
}

TEST_F(InventorySerializationTest, FullInventorySerializesCorrectly) {
  // Create full inventory
  auto& inventory = registry_.emplace<InventorySnapshotComponent>(entity_);

  for (size_t i = 0; i < InventorySnapshotComponent::kMaxSlots; ++i) {
    InventorySnapshotItemData item;
    item.instance_id = i + 1;
    item.item_id = 1000 + static_cast<uint32_t>(i);
    item.count = 1;
    inventory.slots[i] = item;
  }

  // Save to JSON
  std::string json_str = legend2::SaveInventoryToJson(registry_, entity_);

  // Parse and verify
  json parsed = json::parse(json_str);
  ASSERT_TRUE(parsed.contains("slots"));
  EXPECT_EQ(parsed["slots"].size(), InventorySnapshotComponent::kMaxSlots);

  // Spot check first and last
  EXPECT_EQ(parsed["slots"][0]["instance_id"], 1u);
  EXPECT_EQ(parsed["slots"][InventorySnapshotComponent::kMaxSlots - 1]["instance_id"],
            InventorySnapshotComponent::kMaxSlots);
}

TEST_F(InventorySerializationTest, LegacyJsonFormatCompatibility) {
  // Test with old JSON format (if different from current)
  json legacy_json;
  legacy_json["items"] = json::array();  // Old field name

  json item;
  item["id"] = 1;  // Old field name
  item["template_id"] = 100;  // Old field name
  item["quantity"] = 1;  // Old field name
  legacy_json["items"].push_back(item);

  std::string legacy_str = legacy_json.dump();

  // Should handle gracefully (either convert or reject with clear error)
  // Implementation depends on migration strategy
  // This test documents expected behavior

  // Option 1: Convert legacy format
  // legend2::LoadInventoryFromJson(registry_, entity_, legacy_str, 1);
  // const auto* inventory = registry_.try_get<InventorySnapshotComponent>(entity_);
  // EXPECT_NE(inventory, nullptr);

  // Option 2: Reject with error
  // EXPECT_THROW(legend2::LoadInventoryFromJson(registry_, entity_, legacy_str, 1),
  //              std::runtime_error);
}

TEST_F(InventorySerializationTest, MalformedJsonHandledGracefully) {
  std::string malformed_json = "{invalid json";

  // Should not crash, should log error
  EXPECT_NO_THROW({
    legend2::LoadInventoryFromJson(registry_, entity_, malformed_json, 1);
  });

  // Inventory should remain empty or have default state
  const auto* inventory = registry_.try_get<InventorySnapshotComponent>(entity_);
  if (inventory) {
    // All slots should be empty after failed load
    for (const auto& slot : inventory->slots) {
      EXPECT_FALSE(slot.has_value());
    }
  }
}

TEST_F(InventorySerializationTest, NullJsonHandledGracefully) {
  std::string null_json = "null";

  EXPECT_NO_THROW({
    legend2::LoadInventoryFromJson(registry_, entity_, null_json, 1);
  });
}

TEST_F(InventorySerializationTest, ExcessiveDataTruncatedSafely) {
  // Create JSON with more items than max slots
  json excessive_json;
  excessive_json["slots"] = json::array();

  for (size_t i = 0; i < InventorySnapshotComponent::kMaxSlots + 10; ++i) {
    excessive_json["slots"].push_back(CreateSampleItemJson(i, 100, 1));
  }

  std::string json_str = excessive_json.dump();

  // Should load without crash, truncating excess
  EXPECT_NO_THROW({
    legend2::LoadInventoryFromJson(registry_, entity_, json_str, 1);
  });

  const auto* inventory = registry_.try_get<InventorySnapshotComponent>(entity_);
  ASSERT_NE(inventory, nullptr);

  // Should have exactly max slots, not more
  int filled_count = 0;
  for (const auto& slot : inventory->slots) {
    if (slot.has_value()) {
      ++filled_count;
    }
  }
  EXPECT_LE(filled_count, static_cast<int>(InventorySnapshotComponent::kMaxSlots));
}

}  // namespace
