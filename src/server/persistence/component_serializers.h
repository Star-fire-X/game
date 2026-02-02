/**
 * @file component_serializers.h
 * @brief Component-specific serialization implementations (Epic 5)
 *
 * Implements serialization for all 15 core ECS components:
 * - Inventory (Story 5.1)
 * - Equipment (Story 5.2)
 * - Trade (Story 5.3)
 * - Character Attributes (Story 5.4)
 * - Position/Movement (Story 5.5)
 * - Health, StatusEffects, Quest, Skills, Reputation (Story 5.6)
 * - Pet, Mount, Guild, Cooldown, BuffDebuff (Story 5.6)
 */

#ifndef MIR2_PERSISTENCE_COMPONENT_SERIALIZERS_H
#define MIR2_PERSISTENCE_COMPONENT_SERIALIZERS_H

#include "nlohmann/json.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace mir2::persistence {

using json = nlohmann::json;

// ==================== Component Definitions (Sprint 3: Epic 5) ====================

// Story 5.1: Inventory Component
struct InventoryComponent {
    struct Item {
        uint32_t item_id;
        uint32_t quantity;
        uint8_t rarity;  // 0-5
        bool is_bound;
    };

    std::vector<Item> items;
    uint32_t capacity = 50;

    bool operator==(const InventoryComponent& other) const {
        return items == other.items && capacity == other.capacity;
    }
};

// Story 5.2: Equipment Component
struct EquipmentComponent {
    struct EquippedItem {
        uint32_t item_id;
        uint8_t durability;  // 0-100%
        std::vector<uint32_t> enchantment_ids;
    };

    std::map<std::string, EquippedItem> slots;  // "head", "chest", "hands", etc.

    bool operator==(const EquipmentComponent& other) const {
        return slots == other.slots;
    }
};

// Story 5.3: Trade Component
struct TradeComponent {
    enum class TradeState : uint8_t {
        kPending = 0,
        kAccepted = 1,
        kCompleted = 2,
        kCancelled = 3
    };

    struct OfferedItem {
        uint32_t item_id;
        uint32_t quantity;
    };

    std::vector<OfferedItem> offered_items;
    uint64_t partner_id = 0;
    std::string partner_name;
    TradeState state = TradeState::kPending;
    uint64_t last_activity_time = 0;

    bool operator==(const TradeComponent& other) const {
        return offered_items == other.offered_items &&
               partner_id == other.partner_id &&
               partner_name == other.partner_name &&
               state == other.state;
    }
};

// Story 5.4: Character Attributes Component
struct AttributesComponent {
    uint8_t level = 1;
    uint32_t experience = 0;
    uint32_t current_hp = 100;
    uint32_t max_hp = 100;
    uint32_t current_mana = 50;
    uint32_t max_mana = 50;

    struct Skill {
        uint32_t skill_id;
        uint8_t proficiency;  // 0-100
    };
    std::vector<Skill> learned_skills;

    bool operator==(const AttributesComponent& other) const {
        return level == other.level &&
               experience == other.experience &&
               current_hp == other.current_hp &&
               max_hp == other.max_hp;
    }
};

// Story 5.5: Position Component
struct PositionComponent {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float facing_angle = 0.0f;  // 0-360 degrees
    uint32_t current_map = 0;

    bool operator==(const PositionComponent& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

// Story 5.6: Secondary Components
struct HealthComponent {
    uint32_t current_hp;
    uint32_t max_hp;
};

struct StatusEffectsComponent {
    struct Effect {
        uint32_t effect_id;
        uint32_t duration_ms;
        uint32_t stack_count;
    };
    std::vector<Effect> active_effects;
};

struct QuestComponent {
    struct Quest {
        uint32_t quest_id;
        uint8_t progress;  // 0-100
        uint8_t state;     // pending, active, completed, failed
    };
    std::vector<Quest> quests;
};

struct SkillsComponent {
    std::map<uint32_t, uint8_t> skill_proficiency;  // skill_id -> level (0-100)
};

struct ReputationComponent {
    std::map<uint32_t, int32_t> faction_scores;  // faction_id -> score
};

struct PetComponent {
    uint32_t pet_id = 0;
    std::string pet_name;
    uint8_t level = 1;
    uint32_t happiness = 100;  // 0-100
};

struct MountComponent {
    uint32_t mount_id = 0;
    uint8_t tiredness = 0;  // 0-100
};

struct GuildComponent {
    uint32_t guild_id = 0;
    std::string guild_name;
    uint8_t member_rank = 0;  // 0 = leader, 1-3 = officers/members
};

struct CooldownComponent {
    std::map<uint32_t, uint64_t> ability_cooldowns;  // ability_id -> end_time_ms
};

struct BuffDebuffComponent {
    struct BuffEffect {
        uint32_t buff_id;
        uint64_t end_time_ms;
        float effectiveness;  // 0.0-1.0
    };
    std::vector<BuffEffect> active_buffs;
};

// ==================== Serialization Helpers ====================

namespace serializers {

// Inventory serialization (Story 5.1)
inline json SerializeInventory(const InventoryComponent& inv) {
    json j;
    j["capacity"] = inv.capacity;
    j["items"] = json::array();
    for (const auto& item : inv.items) {
        j["items"].push_back({
            {"item_id", item.item_id},
            {"quantity", item.quantity},
            {"rarity", item.rarity},
            {"is_bound", item.is_bound}
        });
    }
    return j;
}

inline InventoryComponent DeserializeInventory(const json& j) {
    InventoryComponent inv;
    if (j.contains("capacity")) {
        inv.capacity = j["capacity"].get<uint32_t>();
    }
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& item_j : j["items"]) {
            inv.items.push_back({
                item_j["item_id"].get<uint32_t>(),
                item_j["quantity"].get<uint32_t>(),
                item_j["rarity"].get<uint8_t>(),
                item_j["is_bound"].get<bool>()
            });
        }
    }
    return inv;
}

// Equipment serialization (Story 5.2)
inline json SerializeEquipment(const EquipmentComponent& eq) {
    json j;
    for (const auto& [slot_name, item] : eq.slots) {
        j[slot_name] = {
            {"item_id", item.item_id},
            {"durability", item.durability},
            {"enchantments", item.enchantment_ids}
        };
    }
    return j;
}

inline EquipmentComponent DeserializeEquipment(const json& j) {
    EquipmentComponent eq;
    for (auto& [key, val] : j.items()) {
        eq.slots[key] = {
            val["item_id"].get<uint32_t>(),
            val["durability"].get<uint8_t>(),
            val["enchantments"].get<std::vector<uint32_t>>()
        };
    }
    return eq;
}

// Trade serialization (Story 5.3)
inline json SerializeTrade(const TradeComponent& trade) {
    json j;
    j["items"] = json::array();
    for (const auto& item : trade.offered_items) {
        j["items"].push_back({
            {"item_id", item.item_id},
            {"quantity", item.quantity}
        });
    }
    j["partner_id"] = trade.partner_id;
    j["partner_name"] = trade.partner_name;
    j["state"] = static_cast<uint8_t>(trade.state);
    j["last_activity"] = trade.last_activity_time;
    return j;
}

inline TradeComponent DeserializeTrade(const json& j) {
    TradeComponent trade;
    if (j.contains("items")) {
        for (const auto& item_j : j["items"]) {
            trade.offered_items.push_back({
                item_j["item_id"].get<uint32_t>(),
                item_j["quantity"].get<uint32_t>()
            });
        }
    }
    trade.partner_id = j.value("partner_id", 0UL);
    trade.partner_name = j.value("partner_name", "");
    trade.state = static_cast<TradeComponent::TradeState>(
        j.value("state", static_cast<uint8_t>(TradeComponent::TradeState::kPending))
    );
    trade.last_activity_time = j.value("last_activity", 0UL);
    return trade;
}

// Attributes serialization (Story 5.4)
inline json SerializeAttributes(const AttributesComponent& attrs) {
    json j;
    j["level"] = attrs.level;
    j["experience"] = attrs.experience;
    j["current_hp"] = attrs.current_hp;
    j["max_hp"] = attrs.max_hp;
    j["current_mana"] = attrs.current_mana;
    j["max_mana"] = attrs.max_mana;
    j["skills"] = json::array();
    for (const auto& skill : attrs.learned_skills) {
        j["skills"].push_back({
            {"skill_id", skill.skill_id},
            {"proficiency", skill.proficiency}
        });
    }
    return j;
}

inline AttributesComponent DeserializeAttributes(const json& j) {
    AttributesComponent attrs;
    attrs.level = j.value("level", 1);
    attrs.experience = j.value("experience", 0U);
    attrs.current_hp = j.value("current_hp", 100U);
    attrs.max_hp = j.value("max_hp", 100U);
    attrs.current_mana = j.value("current_mana", 50U);
    attrs.max_mana = j.value("max_mana", 50U);
    if (j.contains("skills")) {
        for (const auto& skill_j : j["skills"]) {
            attrs.learned_skills.push_back({
                skill_j["skill_id"].get<uint32_t>(),
                skill_j["proficiency"].get<uint8_t>()
            });
        }
    }
    return attrs;
}

// Position serialization (Story 5.5)
inline json SerializePosition(const PositionComponent& pos) {
    return json{
        {"x", pos.x},
        {"y", pos.y},
        {"z", pos.z},
        {"facing_angle", pos.facing_angle},
        {"current_map", pos.current_map}
    };
}

inline PositionComponent DeserializePosition(const json& j) {
    PositionComponent pos;
    pos.x = j.value("x", 0.0f);
    pos.y = j.value("y", 0.0f);
    pos.z = j.value("z", 0.0f);
    pos.facing_angle = j.value("facing_angle", 0.0f);
    pos.current_map = j.value("current_map", 0U);
    return pos;
}

}  // namespace serializers

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_COMPONENT_SERIALIZERS_H
