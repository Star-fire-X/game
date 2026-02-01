/**
 * @file inventory_migration.cc
 * @brief JSON 与结构化组件迁移工具实现
 */

#include "ecs/inventory_migration.h"

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/skill_component.h"
#include "log/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace mir2::ecs::inventory {
namespace {

using json = nlohmann::json;

bool IsBlank(std::string_view value) {
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

uint32_t GetCharacterId(const entt::registry& registry, entt::entity character) {
    if (const auto* identity = registry.try_get<CharacterIdentityComponent>(character)) {
        return identity->id;
    }
    return 0;
}

std::string ToLower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

template <typename T>
bool TryGetNumber(const json& source, const char* key, T& out_value) {
    auto it = source.find(key);
    if (it == source.end() || it->is_null()) {
        return false;
    }
    try {
        if (it->is_number_integer()) {
            auto value = it->get<int64_t>();
            if constexpr (std::is_unsigned_v<T>) {
                if (value < 0) {
                    return false;
                }
            }
            out_value = static_cast<T>(value);
            return true;
        }
        if (it->is_number_unsigned()) {
            out_value = static_cast<T>(it->get<uint64_t>());
            return true;
        }
        if (it->is_number_float()) {
            auto value = it->get<double>();
            if constexpr (std::is_unsigned_v<T>) {
                if (value < 0.0) {
                    return false;
                }
            }
            out_value = static_cast<T>(value);
            return true;
        }
        if (it->is_string()) {
            const std::string text = it->get<std::string>();
            size_t idx = 0;
            long long value = std::stoll(text, &idx, 10);
            if (idx == text.size()) {
                if constexpr (std::is_unsigned_v<T>) {
                    if (value < 0) {
                        return false;
                    }
                }
                out_value = static_cast<T>(value);
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

template <typename T>
bool TryGetNumberFromKeys(const json& source,
                          std::initializer_list<const char*> keys,
                          T& out_value) {
    for (const char* key : keys) {
        if (TryGetNumber(source, key, out_value)) {
            return true;
        }
    }
    return false;
}

const json* ExtractArray(const json& root, std::initializer_list<const char*> keys) {
    if (root.is_array()) {
        return &root;
    }
    if (!root.is_object()) {
        return nullptr;
    }
    for (const char* key : keys) {
        auto it = root.find(key);
        if (it != root.end() && it->is_array()) {
            return &(*it);
        }
    }
    return nullptr;
}

bool PopulateItemFromJson(const json& item_json, ItemComponent& out_item) {
    if (!item_json.is_object()) {
        return false;
    }

    uint32_t template_id = 0;
    if (!TryGetNumberFromKeys(item_json,
                              {"template_id", "item_template_id", "id", "item_id"},
                              template_id) ||
        template_id == 0) {
        return false;
    }

    out_item.item_id = template_id;

    uint64_t instance_id = 0;
    if (TryGetNumberFromKeys(item_json, {"instance_id", "instance"}, instance_id)) {
        out_item.instance_id = instance_id;
    }

    int count = 1;
    if (TryGetNumberFromKeys(item_json, {"quantity", "count", "stack"}, count)) {
        out_item.count = std::max(1, count);
    }

    int durability = 0;
    bool has_durability = TryGetNumberFromKeys(item_json, {"durability"}, durability);
    int max_durability = 0;
    bool has_max_durability =
        TryGetNumberFromKeys(item_json, {"max_durability", "max_dura"}, max_durability);

    if (has_max_durability) {
        out_item.max_durability = std::max(0, max_durability);
    }
    if (has_durability) {
        out_item.durability = std::max(0, durability);
    } else if (has_max_durability) {
        out_item.durability = out_item.max_durability;
    }

    int shape = 0;
    if (TryGetNumberFromKeys(item_json, {"shape", "item_shape"}, shape)) {
        out_item.shape = shape;
    }

    int enhancement = 0;
    if (TryGetNumberFromKeys(item_json, {"enhancement_level", "enhancement"}, enhancement)) {
        out_item.enhancement_level = enhancement;
    }

    int luck = 0;
    if (TryGetNumberFromKeys(item_json, {"luck"}, luck)) {
        out_item.luck = luck;
    }

    return true;
}

json BuildItemJson(const ItemComponent& item) {
    json item_json;
    item_json["instance_id"] = item.instance_id;
    item_json["template_id"] = item.item_id;
    item_json["quantity"] = item.count;
    item_json["durability"] = item.durability;
    if (item.shape != 0) {
        item_json["shape"] = item.shape;
    }
    item_json["enhancement_level"] = item.enhancement_level;
    return item_json;
}

void ClearInventoryItems(entt::registry& registry, entt::entity character) {
    std::vector<entt::entity> to_destroy;
    auto view = registry.view<ItemComponent, InventoryOwnerComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner == character && owner.slot_index >= 0) {
            to_destroy.push_back(entity);
        }
    }

    for (auto entity : to_destroy) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
}

void ClearEquipmentItems(entt::registry& registry,
                         entt::entity character,
                         EquipmentSlotComponent& equipment) {
    std::vector<entt::entity> to_destroy;
    to_destroy.reserve(equipment.slots.size());

    for (const auto& entity : equipment.slots) {
        if (entity != entt::null && registry.valid(entity)) {
            to_destroy.push_back(entity);
        }
    }

    auto view = registry.view<ItemComponent, InventoryOwnerComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner == character && owner.slot_index < 0) {
            to_destroy.push_back(entity);
        }
    }

    for (auto entity : to_destroy) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }

    std::fill(equipment.slots.begin(), equipment.slots.end(), entt::null);
}

void ClearSkills(entt::registry& registry, entt::entity character) {
    std::vector<entt::entity> to_destroy;
    auto view = registry.view<SkillComponent, InventoryOwnerComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner == character) {
            to_destroy.push_back(entity);
        }
    }

    for (auto entity : to_destroy) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
}

std::optional<json> ParseJsonSafe(const std::string& input,
                                  std::string_view label,
                                  uint32_t character_id,
                                  bool& had_error) {
    had_error = false;
    if (input.empty() || IsBlank(input)) {
        return std::nullopt;
    }
    try {
        return json::parse(input);
    } catch (const std::exception& ex) {
        had_error = true;
        SYSLOG_WARN("InventoryMigration: parse {} failed id={} error={}",
                    label, character_id, ex.what());
    } catch (...) {
        had_error = true;
        SYSLOG_WARN("InventoryMigration: parse {} failed id={} error=unknown",
                    label, character_id);
    }
    return std::nullopt;
}

int ClampSlotIndex(int slot, int max_slots) {
    if (slot < 0 || slot >= max_slots) {
        return -1;
    }
    return slot;
}

std::optional<int> ParseSlotIndex(const json& entry) {
    int slot = -1;
    if (TryGetNumberFromKeys(entry, {"slot", "index"}, slot)) {
        return slot;
    }
    return std::nullopt;
}

std::optional<int> EquipSlotFromKey(const std::string& key,
                                    const EquipmentSlotComponent& equipment) {
    const std::string lower = ToLower(key);
    if (lower == "weapon") {
        return static_cast<int>(mir2::common::EquipSlot::WEAPON);
    }
    if (lower == "armor" || lower == "armour") {
        return static_cast<int>(mir2::common::EquipSlot::ARMOR);
    }
    if (lower == "helmet") {
        return static_cast<int>(mir2::common::EquipSlot::HELMET);
    }
    if (lower == "boots") {
        return static_cast<int>(mir2::common::EquipSlot::BOOTS);
    }
    if (lower == "necklace") {
        return static_cast<int>(mir2::common::EquipSlot::NECKLACE);
    }
    if (lower == "amulet" || lower == "armringl" || lower == "armring_left") {
        return static_cast<int>(mir2::common::EquipSlot::AMULET);
    }
    if (lower == "talisman" || lower == "bujuk") {
        return static_cast<int>(mir2::common::EquipSlot::TALISMAN);
    }
    if (lower == "belt") {
        return static_cast<int>(mir2::common::EquipSlot::BELT);
    }

    if (lower == "ring_left" || lower == "lring" || lower == "ringl") {
        return static_cast<int>(mir2::common::EquipSlot::RING_LEFT);
    }
    if (lower == "ring_right" || lower == "rring" || lower == "ringr") {
        return static_cast<int>(mir2::common::EquipSlot::RING_RIGHT);
    }
    if (lower == "ring") {
        const auto left = static_cast<int>(mir2::common::EquipSlot::RING_LEFT);
        const auto right = static_cast<int>(mir2::common::EquipSlot::RING_RIGHT);
        if (equipment.slots[left] == entt::null) {
            return left;
        }
        if (equipment.slots[right] == entt::null) {
            return right;
        }
        return std::nullopt;
    }

    if (lower == "bracelet_left" || lower == "lbracelet" || lower == "braceletl") {
        return static_cast<int>(mir2::common::EquipSlot::BRACELET_LEFT);
    }
    if (lower == "bracelet_right" || lower == "rbracelet" || lower == "braceletr") {
        return static_cast<int>(mir2::common::EquipSlot::BRACELET_RIGHT);
    }
    if (lower == "bracelet") {
        const auto left = static_cast<int>(mir2::common::EquipSlot::BRACELET_LEFT);
        const auto right = static_cast<int>(mir2::common::EquipSlot::BRACELET_RIGHT);
        if (equipment.slots[left] == entt::null) {
            return left;
        }
        if (equipment.slots[right] == entt::null) {
            return right;
        }
        return std::nullopt;
    }

    bool numeric = !lower.empty() &&
                   std::all_of(lower.begin(), lower.end(),
                               [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
    if (numeric) {
        try {
            return std::stoi(lower);
        } catch (...) {
        }
    }
    return std::nullopt;
}

bool IsEquipSlotKey(std::string_view key) {
    const std::string lower = ToLower(key);
    if (lower == "weapon" || lower == "armor" || lower == "armour" ||
        lower == "helmet" || lower == "boots" || lower == "necklace" ||
        lower == "belt" || lower == "amulet" || lower == "armringl" ||
        lower == "armring_left" || lower == "talisman" || lower == "bujuk") {
        return true;
    }
    if (lower == "ring" || lower == "ring_left" || lower == "ring_right" ||
        lower == "lring" || lower == "rring" || lower == "ringl" ||
        lower == "ringr") {
        return true;
    }
    if (lower == "bracelet" || lower == "bracelet_left" ||
        lower == "bracelet_right" || lower == "lbracelet" ||
        lower == "rbracelet" || lower == "braceletl" ||
        lower == "braceletr") {
        return true;
    }
    bool numeric = !lower.empty() &&
                   std::all_of(lower.begin(), lower.end(),
                               [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
    return numeric;
}

bool AssignEquipmentItem(entt::registry& registry,
                         entt::entity character,
                         EquipmentSlotComponent& equipment,
                         int slot,
                         const ItemComponent& item,
                         uint32_t character_id) {
    if (slot < 0 || slot >= static_cast<int>(equipment.slots.size())) {
        SYSLOG_WARN("InventoryMigration: invalid equipment slot={} id={}", slot, character_id);
        return false;
    }
    if (equipment.slots[slot] != entt::null) {
        SYSLOG_WARN("InventoryMigration: duplicate equipment slot={} id={}", slot, character_id);
        return false;
    }

    entt::entity item_entity = registry.create();
    ItemComponent equipped_item = item;
    equipped_item.equip_slot = slot;
    registry.emplace<ItemComponent>(item_entity, equipped_item);
    auto& owner = registry.emplace<InventoryOwnerComponent>(item_entity);
    owner.owner = character;
    owner.slot_index = -1;
    equipment.slots[slot] = item_entity;
    return true;
}

}  // namespace

void LoadInventoryFromJson(entt::registry& registry,
                           entt::entity character,
                           const std::string& inventory_json,
                           const std::string& equipment_json,
                           const std::string& skills_json) {
    if (!registry.valid(character)) {
        SYSLOG_ERROR("InventoryMigration: invalid character entity");
        return;
    }

    uint32_t character_id = GetCharacterId(registry, character);

    auto& equipment = registry.get_or_emplace<EquipmentSlotComponent>(character);

    int loaded_items = 0;
    int loaded_equipment = 0;
    int loaded_skills = 0;

    bool inventory_error = false;
    if (auto parsed = ParseJsonSafe(inventory_json, "inventory_json", character_id,
                                    inventory_error)) {
        const json* items = ExtractArray(*parsed, {"items", "slots"});
        if (items) {
            bool has_object_entry = std::any_of(items->begin(), items->end(),
                                                [](const json& entry) {
                                                    return entry.is_object();
                                                });
            if (items->empty() || has_object_entry) {
                ClearInventoryItems(registry, character);
            } else {
                SYSLOG_WARN("InventoryMigration: inventory_json items unsupported id={}",
                            character_id);
            }

            if (has_object_entry) {
                std::vector<bool> used_slots(mir2::common::constants::MAX_INVENTORY_SIZE, false);
                for (size_t index = 0; index < items->size(); ++index) {
                    const json& entry = (*items)[index];
                    if (!entry.is_object()) {
                        SYSLOG_WARN("InventoryMigration: inventory entry not object id={}",
                                    character_id);
                        continue;
                    }

                    int slot = -1;
                    if (auto parsed_slot = ParseSlotIndex(entry)) {
                        slot = *parsed_slot;
                    } else if (index < used_slots.size()) {
                        slot = static_cast<int>(index);
                    }

                    slot = ClampSlotIndex(slot, mir2::common::constants::MAX_INVENTORY_SIZE);
                    if (slot < 0) {
                        SYSLOG_WARN("InventoryMigration: invalid inventory slot id={}", character_id);
                        continue;
                    }
                    if (used_slots[slot]) {
                        SYSLOG_WARN("InventoryMigration: duplicate inventory slot={} id={}",
                                    slot, character_id);
                        continue;
                    }

                    const json* item_json = &entry;
                    auto item_it = entry.find("item");
                    if (item_it != entry.end()) {
                        item_json = &(*item_it);
                    }

                    ItemComponent item;
                    if (!PopulateItemFromJson(*item_json, item)) {
                        SYSLOG_WARN("InventoryMigration: invalid inventory item slot={} id={}",
                                    slot, character_id);
                        continue;
                    }

                    entt::entity item_entity = registry.create();
                    registry.emplace<ItemComponent>(item_entity, item);
                    auto& owner = registry.emplace<InventoryOwnerComponent>(item_entity);
                    owner.owner = character;
                    owner.slot_index = slot;
                    used_slots[slot] = true;
                    ++loaded_items;
                }
            }
        } else if (parsed->is_null()) {
            ClearInventoryItems(registry, character);
        } else {
            SYSLOG_WARN("InventoryMigration: inventory_json shape unsupported id={}", character_id);
        }
    } else if (!inventory_error) {
        ClearInventoryItems(registry, character);
    }

    bool equipment_error = false;
    if (auto parsed = ParseJsonSafe(equipment_json, "equipment_json", character_id,
                                    equipment_error)) {
        const json* entries = ExtractArray(*parsed, {"items", "slots"});
        if (entries) {
            bool has_object_entry = std::any_of(entries->begin(), entries->end(),
                                                [](const json& entry) {
                                                    return entry.is_object();
                                                });
            if (entries->empty() || has_object_entry) {
                ClearEquipmentItems(registry, character, equipment);
            } else {
                SYSLOG_WARN("InventoryMigration: equipment_json entries unsupported id={}",
                            character_id);
            }

            if (has_object_entry) {
                for (const auto& entry : *entries) {
                    if (!entry.is_object()) {
                        SYSLOG_WARN("InventoryMigration: equipment entry not object id={}",
                                    character_id);
                        continue;
                    }

                    auto parsed_slot = ParseSlotIndex(entry);
                    if (!parsed_slot) {
                        SYSLOG_WARN("InventoryMigration: missing equipment slot id={}", character_id);
                        continue;
                    }
                    int slot = ClampSlotIndex(*parsed_slot,
                                              static_cast<int>(equipment.slots.size()));
                    if (slot < 0) {
                        SYSLOG_WARN("InventoryMigration: invalid equipment slot id={}", character_id);
                        continue;
                    }

                    const json* item_json = &entry;
                    auto item_it = entry.find("item");
                    if (item_it != entry.end()) {
                        item_json = &(*item_it);
                    }

                    ItemComponent item;
                    if (!PopulateItemFromJson(*item_json, item)) {
                        SYSLOG_WARN("InventoryMigration: invalid equipment item slot={} id={}",
                                    slot, character_id);
                        continue;
                    }

                    if (AssignEquipmentItem(registry, character, equipment, slot, item,
                                            character_id)) {
                        ++loaded_equipment;
                    }
                }
            }
        } else if (parsed->is_object()) {
            bool has_supported_entry = false;
            for (auto it = parsed->begin(); it != parsed->end(); ++it) {
                if (it.value().is_object() && IsEquipSlotKey(it.key())) {
                    has_supported_entry = true;
                    break;
                }
            }
            if (parsed->empty() || has_supported_entry) {
                ClearEquipmentItems(registry, character, equipment);
            } else {
                SYSLOG_WARN("InventoryMigration: equipment_json values unsupported id={}",
                            character_id);
            }

            if (has_supported_entry) {
                for (auto it = parsed->begin(); it != parsed->end(); ++it) {
                    if (!it.value().is_object()) {
                        SYSLOG_WARN("InventoryMigration: equipment entry not object key={} id={}",
                                    it.key(), character_id);
                        continue;
                    }

                    auto slot = EquipSlotFromKey(it.key(), equipment);
                    if (!slot) {
                        continue;
                    }

                    ItemComponent item;
                    if (!PopulateItemFromJson(it.value(), item)) {
                        SYSLOG_WARN("InventoryMigration: invalid equipment item key={} id={}",
                                    it.key(), character_id);
                        continue;
                    }

                    if (AssignEquipmentItem(registry, character, equipment, *slot, item,
                                            character_id)) {
                        ++loaded_equipment;
                    }
                }
            }
        } else if (parsed->is_null()) {
            ClearEquipmentItems(registry, character, equipment);
        } else {
            SYSLOG_WARN("InventoryMigration: equipment_json shape unsupported id={}",
                        character_id);
        }
    } else if (!equipment_error) {
        ClearEquipmentItems(registry, character, equipment);
    }

    bool skills_error = false;
    if (auto parsed = ParseJsonSafe(skills_json, "skills_json", character_id,
                                    skills_error)) {
        const json* skills = ExtractArray(*parsed, {"learned", "skills"});
        if (skills) {
            ClearSkills(registry, character);
            std::vector<uint32_t> seen_skills;
            for (const auto& entry : *skills) {
                SkillComponent skill;
                bool valid = false;

                if (entry.is_number()) {
                    skill.skill_id = entry.get<uint32_t>();
                    valid = skill.skill_id != 0;
                } else if (entry.is_object()) {
                    if (TryGetNumberFromKeys(entry, {"skill_id", "id"}, skill.skill_id)) {
                        valid = skill.skill_id != 0;
                    }
                    int level = skill.level;
                    if (TryGetNumberFromKeys(entry, {"level", "lv"}, level)) {
                        skill.level = std::max(1, level);
                    }
                    int exp = skill.exp;
                    if (TryGetNumberFromKeys(entry, {"exp", "experience"}, exp)) {
                        skill.exp = std::max(0, exp);
                    }
                } else if (entry.is_string()) {
                    try {
                        skill.skill_id = static_cast<uint32_t>(std::stoul(entry.get<std::string>()));
                        valid = skill.skill_id != 0;
                    } catch (...) {
                        valid = false;
                    }
                }

                if (!valid) {
                    SYSLOG_WARN("InventoryMigration: invalid skill entry id={}", character_id);
                    continue;
                }

                if (std::find(seen_skills.begin(), seen_skills.end(),
                              skill.skill_id) != seen_skills.end()) {
                    SYSLOG_WARN("InventoryMigration: duplicate skill_id={} id={}",
                                skill.skill_id, character_id);
                    continue;
                }
                seen_skills.push_back(skill.skill_id);

                entt::entity skill_entity = registry.create();
                registry.emplace<SkillComponent>(skill_entity, skill);
                auto& owner = registry.emplace<InventoryOwnerComponent>(skill_entity);
                owner.owner = character;
                owner.slot_index = -1;
                ++loaded_skills;
            }
        } else if (parsed->is_null()) {
            ClearSkills(registry, character);
        } else {
            SYSLOG_WARN("InventoryMigration: skills_json shape unsupported id={}", character_id);
        }
    } else if (!skills_error) {
        ClearSkills(registry, character);
    }

    SYSLOG_INFO("InventoryMigration: loaded id={} items={} equipment={} skills={}",
                character_id, loaded_items, loaded_equipment, loaded_skills);
}

std::tuple<std::string, std::string, std::string>
SaveInventoryToJson(entt::registry& registry, entt::entity character) {
    if (!registry.valid(character)) {
        SYSLOG_ERROR("InventoryMigration: invalid character entity");
        return {"[]", "[]", "[]"};
    }

    uint32_t character_id = GetCharacterId(registry, character);

    json inventory = json::array();
    json equipment = json::array();
    json skills = json::array();

    std::vector<bool> used_slots(mir2::common::constants::MAX_INVENTORY_SIZE, false);

    auto item_view = registry.view<ItemComponent, InventoryOwnerComponent>();
    std::vector<std::pair<int, json>> inventory_entries;
    for (auto entity : item_view) {
        const auto& owner = item_view.get<InventoryOwnerComponent>(entity);
        if (owner.owner != character || owner.slot_index < 0) {
            continue;
        }
        int slot = ClampSlotIndex(owner.slot_index,
                                  mir2::common::constants::MAX_INVENTORY_SIZE);
        if (slot < 0) {
            SYSLOG_WARN("InventoryMigration: inventory slot out of range id={}", character_id);
            continue;
        }
        if (used_slots[slot]) {
            SYSLOG_WARN("InventoryMigration: duplicate inventory slot={} id={}",
                        slot, character_id);
            continue;
        }
        used_slots[slot] = true;
        const auto& item = item_view.get<ItemComponent>(entity);
        json slot_json;
        slot_json["slot"] = slot;
        slot_json["item"] = BuildItemJson(item);
        inventory_entries.emplace_back(slot, std::move(slot_json));
    }

    std::sort(inventory_entries.begin(), inventory_entries.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    for (auto& entry : inventory_entries) {
        inventory.push_back(std::move(entry.second));
    }

    if (auto* equipment_component = registry.try_get<EquipmentSlotComponent>(character)) {
        for (size_t slot = 0; slot < equipment_component->slots.size(); ++slot) {
            entt::entity item_entity = equipment_component->slots[slot];
            if (item_entity == entt::null || !registry.valid(item_entity)) {
                continue;
            }
            const auto* item = registry.try_get<ItemComponent>(item_entity);
            if (!item) {
                SYSLOG_WARN("InventoryMigration: equipment slot missing item component slot={} id={}",
                            slot, character_id);
                continue;
            }
            json slot_json;
            slot_json["slot"] = slot;
            slot_json["item"] = BuildItemJson(*item);
            equipment.push_back(std::move(slot_json));
        }
    }

    auto skill_view = registry.view<SkillComponent, InventoryOwnerComponent>();
    std::vector<SkillComponent> skill_entries;
    for (auto entity : skill_view) {
        const auto& owner = skill_view.get<InventoryOwnerComponent>(entity);
        if (owner.owner != character) {
            continue;
        }
        skill_entries.push_back(skill_view.get<SkillComponent>(entity));
    }

    std::sort(skill_entries.begin(), skill_entries.end(),
              [](const SkillComponent& lhs, const SkillComponent& rhs) {
                  return lhs.skill_id < rhs.skill_id;
              });

    for (const auto& skill : skill_entries) {
        json skill_json;
        skill_json["id"] = skill.skill_id;
        skill_json["level"] = skill.level;
        skill_json["exp"] = skill.exp;
        skills.push_back(std::move(skill_json));
    }

    return {inventory.dump(), equipment.dump(), skills.dump()};
}

void MigrateAllCharacters(entt::registry& registry) {
    auto view = registry.view<InventoryComponent>();
    size_t migrated = 0;
    for (auto entity : view) {
        const auto& inventory = view.get<InventoryComponent>(entity);
        LoadInventoryFromJson(registry, entity,
                              inventory.inventory_json,
                              inventory.equipment_json,
                              inventory.skills_json);
        ++migrated;
    }

    SYSLOG_INFO("InventoryMigration: migrated {} characters", migrated);
}

}  // namespace mir2::ecs::inventory
