/**
 * @file inventory_system.h
 * @brief Legend2 背包系统
 * 
 * 本文件包含背包管理系统的定义，包括：
 * - 物品模板和实例
 * - 背包槽位管理
 * - 装备系统
 * - 地面物品（掉落物）
 * - 物品拾取和丢弃
 */

#ifndef LEGEND2_INVENTORY_SYSTEM_H
#define LEGEND2_INVENTORY_SYSTEM_H

#include "common/types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <functional>

namespace legend2 {

class Character;

// =============================================================================
// 物品结构 (Item Structures)
// =============================================================================

/**
 * @brief 物品模板（定义物品类型的基础属性）
 * 
 * 物品模板是静态数据，定义了某种物品的所有基础属性。
 * 游戏中的每个物品实例都引用一个物品模板。
 */
struct ItemTemplate {
    uint32_t id = 0;                    ///< 模板ID
    std::string name;                   ///< 物品名称
    mir2::common::ItemType type = mir2::common::ItemType::MATERIAL; ///< 物品类型
    int required_level = 1;             ///< 使用等级要求
    mir2::common::CharacterClass required_class = mir2::common::CharacterClass::WARRIOR;  ///< 职业要求
    bool any_class = true;              ///< 是否任何职业都可使用
    
    // 装备属性加成
    int attack_bonus = 0;               ///< 攻击力加成
    int defense_bonus = 0;              ///< 防御力加成
    int hp_bonus = 0;                   ///< HP加成
    int mp_bonus = 0;                   ///< MP加成
    int magic_attack_bonus = 0;         ///< 魔法攻击加成
    int magic_defense_bonus = 0;        ///< 魔法防御加成
    int speed_bonus = 0;                ///< 速度加成
    
    // 消耗品效果
    int heal_hp = 0;                    ///< 恢复HP量
    int heal_mp = 0;                    ///< 恢复MP量
    
    // 物品属性
    int max_stack = 1;                  ///< 最大堆叠数（装备为1）
    int max_durability = 100;           ///< 最大耐久度
    int sell_price = 0;                 ///< 出售价格
    int buy_price = 0;                  ///< 购买价格
    
    // 图标/精灵索引（用于渲染）
    int icon_index = 0;                 ///< 图标索引
    
    /// 检查是否可装备
    bool is_equippable() const {
        return type == mir2::common::ItemType::WEAPON || type == mir2::common::ItemType::ARMOR ||
               type == mir2::common::ItemType::HELMET || type == mir2::common::ItemType::BOOTS ||
               type == mir2::common::ItemType::RING || type == mir2::common::ItemType::NECKLACE ||
               type == mir2::common::ItemType::BRACELET || type == mir2::common::ItemType::BELT;
    }
    
    /// 检查是否为消耗品
    bool is_consumable() const {
        return type == mir2::common::ItemType::CONSUMABLE;
    }
    
    /// 获取该物品类型对应的装备槽位
    std::optional<mir2::common::EquipSlot> get_equip_slot() const;
    
    /// 获取属性加成（以CharacterStats结构返回）
    mir2::common::CharacterStats get_stat_bonus() const;
};

/**
 * @brief 物品实例（游戏世界或背包中的具体物品）
 */
struct ItemInstance {
    uint32_t instance_id = 0;     ///< 唯一实例ID
    uint32_t template_id = 0;     ///< 引用的物品模板ID
    int quantity = 1;             ///< 堆叠数量
    int durability = 100;         ///< 当前耐久度
    int enhancement_level = 0;    ///< 强化等级（+1, +2等）
    
    bool operator==(const ItemInstance& other) const {
        return instance_id == other.instance_id &&
               template_id == other.template_id &&
               quantity == other.quantity &&
               durability == other.durability &&
               enhancement_level == other.enhancement_level;
    }
};

// JSON serialization for ItemTemplate
void to_json(nlohmann::json& j, const ItemTemplate& item);
void from_json(const nlohmann::json& j, ItemTemplate& item);

// JSON serialization for ItemInstance
void to_json(nlohmann::json& j, const ItemInstance& item);
void from_json(const nlohmann::json& j, ItemInstance& item);

// =============================================================================
// 地面物品 (Ground Item - 掉落物品)
// =============================================================================

/**
 * @brief 地面上的物品（掉落物）
 */
struct GroundItem {
    uint32_t ground_id = 0;       ///< 地面物品唯一ID
    ItemInstance item;            ///< 物品实例
    mir2::common::Position position;            ///< 世界位置
    uint32_t map_id = 0;          ///< 所在地图ID
    uint32_t owner_id = 0;        ///< 可拾取的角色ID（0表示任何人）
    int64_t drop_time = 0;        ///< 掉落时间戳
    int64_t expire_time = 0;      ///< 消失时间戳
    bool is_protected = false;    ///< 是否受保护（仅所有者可拾取）
};

// =============================================================================
// 背包槽位 (Inventory Slot)
// =============================================================================

/**
 * @brief 背包中的一个槽位
 */
struct InventorySlot {
    bool occupied = false;    ///< 是否被占用
    ItemInstance item;        ///< 槽位中的物品
    
    /// 清空槽位
    void clear() {
        occupied = false;
        item = ItemInstance{};
    }
    
    /// 设置槽位物品
    void set(const ItemInstance& new_item) {
        occupied = true;
        item = new_item;
    }
};

// =============================================================================
// 装备套装 (Equipment Set)
// =============================================================================

/**
 * @brief 角色穿戴的装备
 */
struct EquipmentSet {
    std::array<std::optional<ItemInstance>, static_cast<size_t>(mir2::common::EquipSlot::MAX_SLOTS)> slots;
    
    /// 获取指定槽位的物品
    std::optional<ItemInstance> get(mir2::common::EquipSlot slot) const {
        return slots[static_cast<size_t>(slot)];
    }
    
    /// 设置指定槽位的物品
    void set(mir2::common::EquipSlot slot, const ItemInstance& item) {
        slots[static_cast<size_t>(slot)] = item;
    }
    
    /// 清空指定槽位
    void clear(mir2::common::EquipSlot slot) {
        slots[static_cast<size_t>(slot)] = std::nullopt;
    }
    
    /// 检查槽位是否被占用
    bool is_occupied(mir2::common::EquipSlot slot) const {
        return slots[static_cast<size_t>(slot)].has_value();
    }
};

// JSON serialization for EquipmentSet
void to_json(nlohmann::json& j, const EquipmentSet& equipment);
void from_json(const nlohmann::json& j, EquipmentSet& equipment);

// =============================================================================
// 背包操作结果 (Inventory Operation Results)
// =============================================================================

/**
 * @brief 背包操作结果
 */
struct InventoryResult {
    bool success = false;                        ///< 是否成功
    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    int affected_slot = -1;                      ///< 受影响的槽位（-1表示无）
    
    static InventoryResult ok(int slot = -1) {
        return {true, mir2::common::ErrorCode::SUCCESS, "", slot};
    }
    
    static InventoryResult error(mir2::common::ErrorCode code, const std::string& msg = "") {
        return {false, code, msg, -1};
    }
};

/**
 * @brief 装备操作结果
 */
struct EquipResult {
    bool success = false;                        ///< 是否成功
    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    mir2::common::EquipSlot slot = mir2::common::EquipSlot::WEAPON;          ///< 装备槽位
    std::optional<ItemInstance> unequipped_item; ///< 被替换的物品（如果有）
    mir2::common::CharacterStats stat_change;                  ///< 属性变化
    
    static EquipResult ok(mir2::common::EquipSlot s, const mir2::common::CharacterStats& stats, 
                          std::optional<ItemInstance> replaced = std::nullopt) {
        return {true, mir2::common::ErrorCode::SUCCESS, "", s, replaced, stats};
    }
    
    static EquipResult error(mir2::common::ErrorCode code, const std::string& msg = "") {
        return {false, code, msg, mir2::common::EquipSlot::WEAPON, std::nullopt, {}};
    }
};

/**
 * @brief 拾取物品结果
 */
struct PickupResult {
    bool success = false;                        ///< 是否成功
    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    int inventory_slot = -1;                     ///< 放入的背包槽位
    
    static PickupResult ok(int slot) {
        return {true, mir2::common::ErrorCode::SUCCESS, "", slot};
    }
    
    static PickupResult error(mir2::common::ErrorCode code, const std::string& msg = "") {
        return {false, code, msg, -1};
    }
};

// =============================================================================
// Inventory System Interface
// =============================================================================

/// Interface for inventory management
class IInventorySystem {
public:
    virtual ~IInventorySystem() = default;
    
    // --- Item Template Management ---
    virtual void register_item_template(const ItemTemplate& tmpl) = 0;
    virtual std::optional<ItemTemplate> get_item_template(uint32_t template_id) const = 0;
    
    // --- Inventory Operations ---
    virtual InventoryResult add_item(uint32_t character_id, const ItemInstance& item) = 0;
    virtual InventoryResult remove_item(uint32_t character_id, int slot, int quantity = 1) = 0;
    virtual InventoryResult move_item(uint32_t character_id, int from_slot, int to_slot) = 0;
    virtual bool is_inventory_full(uint32_t character_id) const = 0;
    virtual int get_free_slot_count(uint32_t character_id) const = 0;
    virtual std::vector<InventorySlot> get_inventory(uint32_t character_id) const = 0;
    virtual std::optional<ItemInstance> get_item_at(uint32_t character_id, int slot) const = 0;
    
    // --- Equipment Operations ---
    virtual EquipResult equip_item(uint32_t character_id, int inventory_slot, Character& character) = 0;
    virtual EquipResult unequip_item(uint32_t character_id, mir2::common::EquipSlot slot, Character& character) = 0;
    virtual EquipmentSet get_equipment(uint32_t character_id) const = 0;
    virtual mir2::common::CharacterStats calculate_equipment_bonus(uint32_t character_id) const = 0;
    
    // --- Ground Item Operations ---
    virtual uint32_t drop_item(const ItemInstance& item, const mir2::common::Position& pos, 
                               uint32_t map_id, uint32_t owner_id = 0) = 0;
    virtual PickupResult pickup_item(uint32_t character_id, uint32_t ground_item_id, 
                                     Character& character) = 0;
    virtual std::vector<GroundItem> get_ground_items(uint32_t map_id) const = 0;
    virtual std::vector<GroundItem> get_ground_items_near(uint32_t map_id, 
                                                          const mir2::common::Position& pos, int radius) const = 0;
    
    // --- Serialization ---
    virtual std::string serialize_inventory(uint32_t character_id) const = 0;
    virtual std::string serialize_equipment(uint32_t character_id) const = 0;
    virtual void deserialize_inventory(uint32_t character_id, const std::string& json) = 0;
    virtual void deserialize_equipment(uint32_t character_id, const std::string& json) = 0;
};


// =============================================================================
// Inventory System Implementation
// =============================================================================

/// Default inventory system implementation
class InventorySystem : public IInventorySystem {
public:
    InventorySystem();
    
    // --- Item Template Management ---
    void register_item_template(const ItemTemplate& tmpl) override;
    std::optional<ItemTemplate> get_item_template(uint32_t template_id) const override;
    
    // --- Inventory Operations ---
    InventoryResult add_item(uint32_t character_id, const ItemInstance& item) override;
    InventoryResult remove_item(uint32_t character_id, int slot, int quantity = 1) override;
    InventoryResult move_item(uint32_t character_id, int from_slot, int to_slot) override;
    bool is_inventory_full(uint32_t character_id) const override;
    int get_free_slot_count(uint32_t character_id) const override;
    std::vector<InventorySlot> get_inventory(uint32_t character_id) const override;
    std::optional<ItemInstance> get_item_at(uint32_t character_id, int slot) const override;
    
    // --- Equipment Operations ---
    EquipResult equip_item(uint32_t character_id, int inventory_slot, Character& character) override;
    EquipResult unequip_item(uint32_t character_id, mir2::common::EquipSlot slot, Character& character) override;
    EquipmentSet get_equipment(uint32_t character_id) const override;
    mir2::common::CharacterStats calculate_equipment_bonus(uint32_t character_id) const override;
    
    // --- Ground Item Operations ---
    uint32_t drop_item(const ItemInstance& item, const mir2::common::Position& pos, 
                       uint32_t map_id, uint32_t owner_id = 0) override;
    PickupResult pickup_item(uint32_t character_id, uint32_t ground_item_id, 
                             Character& character) override;
    std::vector<GroundItem> get_ground_items(uint32_t map_id) const override;
    std::vector<GroundItem> get_ground_items_near(uint32_t map_id, 
                                                  const mir2::common::Position& pos, int radius) const override;
    
    // --- Serialization ---
    std::string serialize_inventory(uint32_t character_id) const override;
    std::string serialize_equipment(uint32_t character_id) const override;
    void deserialize_inventory(uint32_t character_id, const std::string& json) override;
    void deserialize_equipment(uint32_t character_id, const std::string& json) override;
    
    // --- Utility ---
    void clear_character_data(uint32_t character_id);
    void update_ground_items(int64_t current_time);  // Remove expired items
    
    // --- Configuration ---
    void set_inventory_size(int size) { inventory_size_ = size; }
    int get_inventory_size() const { return inventory_size_; }
    void set_pickup_range(int range) { pickup_range_ = range; }
    int get_pickup_range() const { return pickup_range_; }
    void set_item_protection_time(int64_t ms) { item_protection_time_ = ms; }
    void set_item_expire_time(int64_t ms) { item_expire_time_ = ms; }
    
private:
    // Item templates
    std::unordered_map<uint32_t, ItemTemplate> item_templates_;
    
    // Character inventories: character_id -> inventory slots
    std::unordered_map<uint32_t, std::vector<InventorySlot>> inventories_;
    
    // Character equipment: character_id -> equipment set
    std::unordered_map<uint32_t, EquipmentSet> equipment_;
    
    // Ground items: ground_id -> ground item
    std::unordered_map<uint32_t, GroundItem> ground_items_;
    
    // Ground items by map: map_id -> list of ground_ids
    std::unordered_map<uint32_t, std::vector<uint32_t>> ground_items_by_map_;
    
    // Configuration
    int inventory_size_ = mir2::common::constants::MAX_INVENTORY_SIZE;
    int pickup_range_ = 2;  // Tiles
    int64_t item_protection_time_ = 30000;  // 30 seconds
    int64_t item_expire_time_ = 300000;     // 5 minutes
    
    // ID generators
    uint32_t next_instance_id_ = 1;
    uint32_t next_ground_id_ = 1;
    
    // --- Helper methods ---
    std::vector<InventorySlot>& get_or_create_inventory(uint32_t character_id);
    EquipmentSet& get_or_create_equipment(uint32_t character_id);
    int find_free_slot(const std::vector<InventorySlot>& inventory) const;
    int find_stackable_slot(const std::vector<InventorySlot>& inventory, 
                            uint32_t template_id, int max_stack) const;
    bool can_equip(const ItemTemplate& tmpl, const Character& character) const;
    mir2::common::EquipSlot get_available_equip_slot(const ItemTemplate& tmpl, 
                                       const EquipmentSet& equipment) const;
    uint32_t generate_instance_id() { return next_instance_id_++; }
    uint32_t generate_ground_id() { return next_ground_id_++; }
};

// =============================================================================
// 掉落生成 (Loot Generation)
// =============================================================================

/**
 * @brief 掉落表条目
 */
struct LootEntry {
    uint32_t item_template_id = 0;  ///< 物品模板ID
    float drop_chance = 0.0f;       ///< 掉落概率（0.0到1.0）
    int min_quantity = 1;           ///< 最小数量
    int max_quantity = 1;           ///< 最大数量
};

/**
 * @brief 怪物掉落表
 */
struct LootTable {
    uint32_t monster_template_id = 0;   ///< 怪物模板ID
    std::vector<LootEntry> entries;     ///< 掉落条目列表
    int gold_min = 0;                   ///< 最小金币掉落
    int gold_max = 0;                   ///< 最大金币掉落
};

/**
 * @brief 从掉落表生成掉落物品
 * @param table 掉落表
 * @param random_func 随机数生成函数（返回0.0-1.0）
 * @return 生成的物品实例列表
 */
std::vector<ItemInstance> generate_loot(const LootTable& table, 
                                        std::function<float()> random_func);

} // namespace legend2

#endif // LEGEND2_INVENTORY_SYSTEM_H
