/**
 * @file inventory_system.cpp
 * @brief Legend2 背包系统实现
 * 
 * 本文件实现背包管理系统的核心功能，包括：
 * - 物品模板管理和JSON序列化
 * - 背包槽位操作（添加、移除、移动物品）
 * - 装备系统（穿戴、卸下、属性计算）
 * - 地面物品管理（掉落、拾取、过期清理）
 * - 掉落物品生成
 */

#include "inventory_system.h"
#include "legacy/character.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace legend2 {

// =============================================================================
// ItemTemplate 类实现
// =============================================================================

/**
 * @brief 获取物品对应的装备槽位
 * @return std::optional<mir2::common::EquipSlot> 可装备返回槽位，不可装备返回 nullopt
 * 
 * 根据物品类型映射到对应的装备槽位：
 * - WEAPON -> WEAPON
 * - ARMOR -> ARMOR
 * - RING -> RING_LEFT（默认左手）
 * - BRACELET -> BRACELET_LEFT（默认左手）
 */
std::optional<mir2::common::EquipSlot> ItemTemplate::get_equip_slot() const {
    switch (type) {
        case mir2::common::ItemType::WEAPON:    return mir2::common::EquipSlot::WEAPON;
        case mir2::common::ItemType::ARMOR:     return mir2::common::EquipSlot::ARMOR;
        case mir2::common::ItemType::HELMET:    return mir2::common::EquipSlot::HELMET;
        case mir2::common::ItemType::BOOTS:     return mir2::common::EquipSlot::BOOTS;
        case mir2::common::ItemType::RING:      return mir2::common::EquipSlot::RING_LEFT;  // Default to left
        case mir2::common::ItemType::NECKLACE:  return mir2::common::EquipSlot::NECKLACE;
        case mir2::common::ItemType::BRACELET:  return mir2::common::EquipSlot::BRACELET_LEFT;  // Default to left
        case mir2::common::ItemType::BELT:      return mir2::common::EquipSlot::BELT;
        default:                  return std::nullopt;
    }
}

/**
 * @brief 获取物品提供的属性加成
 * @return mir2::common::CharacterStats 属性加成结构
 * 
 * 将物品的各项加成转换为 mir2::common::CharacterStats 结构，
 * 用于装备时计算角色总属性。
 */
mir2::common::CharacterStats ItemTemplate::get_stat_bonus() const {
    mir2::common::CharacterStats bonus;
    bonus.level = 0;
    bonus.hp = 0;
    bonus.max_hp = hp_bonus;
    bonus.mp = 0;
    bonus.max_mp = mp_bonus;
    bonus.attack = attack_bonus;
    bonus.defense = defense_bonus;
    bonus.magic_attack = magic_attack_bonus;
    bonus.magic_defense = magic_defense_bonus;
    bonus.speed = speed_bonus;
    bonus.experience = 0;
    bonus.gold = 0;
    return bonus;
}

// =============================================================================
// JSON 序列化实现
// =============================================================================

/**
 * @brief 将 ItemTemplate 序列化为 JSON
 * @param j 输出的 JSON 对象
 * @param item 物品模板
 */
void to_json(nlohmann::json& j, const ItemTemplate& item) {
    j = nlohmann::json{
        {"id", item.id},
        {"name", item.name},
        {"type", static_cast<uint8_t>(item.type)},
        {"required_level", item.required_level},
        {"required_class", static_cast<uint8_t>(item.required_class)},
        {"any_class", item.any_class},
        {"attack_bonus", item.attack_bonus},
        {"defense_bonus", item.defense_bonus},
        {"hp_bonus", item.hp_bonus},
        {"mp_bonus", item.mp_bonus},
        {"magic_attack_bonus", item.magic_attack_bonus},
        {"magic_defense_bonus", item.magic_defense_bonus},
        {"speed_bonus", item.speed_bonus},
        {"heal_hp", item.heal_hp},
        {"heal_mp", item.heal_mp},
        {"max_stack", item.max_stack},
        {"max_durability", item.max_durability},
        {"sell_price", item.sell_price},
        {"buy_price", item.buy_price},
        {"icon_index", item.icon_index}
    };
}

/**
 * @brief 从 JSON 反序列化 ItemTemplate
 * @param j 输入的 JSON 对象
 * @param item 输出的物品模板
 */
void from_json(const nlohmann::json& j, ItemTemplate& item) {
    j.at("id").get_to(item.id);
    j.at("name").get_to(item.name);
    item.type = static_cast<mir2::common::ItemType>(j.at("type").get<uint8_t>());
    j.at("required_level").get_to(item.required_level);
    item.required_class = static_cast<mir2::common::CharacterClass>(j.at("required_class").get<uint8_t>());
    j.at("any_class").get_to(item.any_class);
    j.at("attack_bonus").get_to(item.attack_bonus);
    j.at("defense_bonus").get_to(item.defense_bonus);
    j.at("hp_bonus").get_to(item.hp_bonus);
    j.at("mp_bonus").get_to(item.mp_bonus);
    j.at("magic_attack_bonus").get_to(item.magic_attack_bonus);
    j.at("magic_defense_bonus").get_to(item.magic_defense_bonus);
    j.at("speed_bonus").get_to(item.speed_bonus);
    j.at("heal_hp").get_to(item.heal_hp);
    j.at("heal_mp").get_to(item.heal_mp);
    j.at("max_stack").get_to(item.max_stack);
    j.at("max_durability").get_to(item.max_durability);
    j.at("sell_price").get_to(item.sell_price);
    j.at("buy_price").get_to(item.buy_price);
    j.at("icon_index").get_to(item.icon_index);
}

/**
 * @brief 将 ItemInstance 序列化为 JSON
 * @param j 输出的 JSON 对象
 * @param item 物品实例
 */
void to_json(nlohmann::json& j, const ItemInstance& item) {
    j = nlohmann::json{
        {"instance_id", item.instance_id},
        {"template_id", item.template_id},
        {"quantity", item.quantity},
        {"durability", item.durability},
        {"enhancement_level", item.enhancement_level}
    };
}

/**
 * @brief 从 JSON 反序列化 ItemInstance
 * @param j 输入的 JSON 对象
 * @param item 输出的物品实例
 */
void from_json(const nlohmann::json& j, ItemInstance& item) {
    j.at("instance_id").get_to(item.instance_id);
    j.at("template_id").get_to(item.template_id);
    j.at("quantity").get_to(item.quantity);
    j.at("durability").get_to(item.durability);
    j.at("enhancement_level").get_to(item.enhancement_level);
}

/**
 * @brief 将 EquipmentSet 序列化为 JSON 数组
 * @param j 输出的 JSON 数组
 * @param equipment 装备集合
 * 
 * 只序列化已装备的槽位，空槽位不输出。
 */
void to_json(nlohmann::json& j, const EquipmentSet& equipment) {
    j = nlohmann::json::array();
    for (size_t i = 0; i < equipment.slots.size(); ++i) {
        if (equipment.slots[i].has_value()) {
            nlohmann::json slot_json;
            slot_json["slot"] = i;
            slot_json["item"] = equipment.slots[i].value();
            j.push_back(slot_json);
        }
    }
}

/**
 * @brief 从 JSON 数组反序列化 EquipmentSet
 * @param j 输入的 JSON 数组
 * @param equipment 输出的装备集合
 * 
 * 先清空所有槽位，再根据 JSON 数据填充。
 */
void from_json(const nlohmann::json& j, EquipmentSet& equipment) {
    // Clear all slots first
    for (auto& slot : equipment.slots) {
        slot = std::nullopt;
    }
    
    // Parse equipped items
    if (j.is_array()) {
        for (const auto& slot_json : j) {
            size_t slot_index = slot_json.at("slot").get<size_t>();
            if (slot_index < equipment.slots.size()) {
                ItemInstance item;
                slot_json.at("item").get_to(item);
                equipment.slots[slot_index] = item;
            }
        }
    }
}

// =============================================================================
// InventorySystem 类实现
// =============================================================================

/**
 * @brief 构造函数 - 使用默认设置初始化
 */
InventorySystem::InventorySystem() {
    // Initialize with default settings
}

/**
 * @brief 注册物品模板
 * @param tmpl 物品模板
 * 
 * 将物品模板添加到系统中，后续可通过 ID 查询。
 * 如果 ID 已存在，会覆盖原有模板。
 */
void InventorySystem::register_item_template(const ItemTemplate& tmpl) {
    item_templates_[tmpl.id] = tmpl;
}

/**
 * @brief 获取物品模板
 * @param template_id 模板 ID
 * @return std::optional<ItemTemplate> 找到返回模板，否则返回 nullopt
 */
std::optional<ItemTemplate> InventorySystem::get_item_template(uint32_t template_id) const {
    auto it = item_templates_.find(template_id);
    if (it != item_templates_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// 背包管理内部方法
// -----------------------------------------------------------------------------

/**
 * @brief 获取或创建角色背包
 * @param character_id 角色 ID
 * @return 背包槽位向量的引用
 * 
 * 如果角色没有背包，创建一个默认大小的空背包。
 */
std::vector<InventorySlot>& InventorySystem::get_or_create_inventory(uint32_t character_id) {
    auto it = inventories_.find(character_id);
    if (it == inventories_.end()) {
        inventories_[character_id] = std::vector<InventorySlot>(inventory_size_);
        return inventories_[character_id];
    }
    return it->second;
}

/**
 * @brief 获取或创建角色装备集
 * @param character_id 角色 ID
 * @return 装备集的引用
 */
EquipmentSet& InventorySystem::get_or_create_equipment(uint32_t character_id) {
    auto it = equipment_.find(character_id);
    if (it == equipment_.end()) {
        equipment_[character_id] = EquipmentSet{};
        return equipment_[character_id];
    }
    return it->second;
}

/**
 * @brief 查找空闲背包槽位
 * @param inventory 背包槽位向量
 * @return 空闲槽位索引，没有空位返回 -1
 */
int InventorySystem::find_free_slot(const std::vector<InventorySlot>& inventory) const {
    for (size_t i = 0; i < inventory.size(); ++i) {
        if (!inventory[i].occupied) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/**
 * @brief 查找可堆叠的槽位
 * @param inventory 背包槽位向量
 * @param template_id 物品模板 ID
 * @param max_stack 最大堆叠数量
 * @return 可堆叠的槽位索引，没有返回 -1
 * 
 * 查找已有相同物品且未达到堆叠上限的槽位。
 */
int InventorySystem::find_stackable_slot(const std::vector<InventorySlot>& inventory,
                                          uint32_t template_id, int max_stack) const {
    for (size_t i = 0; i < inventory.size(); ++i) {
        if (inventory[i].occupied && 
            inventory[i].item.template_id == template_id &&
            inventory[i].item.quantity < max_stack) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// -----------------------------------------------------------------------------
// 背包操作公共方法
// -----------------------------------------------------------------------------

/**
 * @brief 添加物品到背包
 * @param character_id 角色 ID
 * @param item 物品实例
 * @return InventoryResult 操作结果，包含受影响的槽位
 * 
 * 添加逻辑：
 * 1. 优先尝试与现有同类物品堆叠
 * 2. 堆叠满后查找空槽位
 * 3. 自动分配物品实例 ID
 */
InventoryResult InventorySystem::add_item(uint32_t character_id, const ItemInstance& item) {
    auto& inventory = get_or_create_inventory(character_id);
    
    // Get item template for stack info
    auto tmpl = get_item_template(item.template_id);
    int max_stack = tmpl ? tmpl->max_stack : 1;
    
    // Try to stack with existing items first
    if (max_stack > 1) {
        int stackable_slot = find_stackable_slot(inventory, item.template_id, max_stack);
        if (stackable_slot >= 0) {
            int space_available = max_stack - inventory[stackable_slot].item.quantity;
            int to_add = std::min(space_available, item.quantity);
            inventory[stackable_slot].item.quantity += to_add;
            
            // If we added all items, we're done
            if (to_add >= item.quantity) {
                return InventoryResult::ok(stackable_slot);
            }
            
            // Otherwise, we need to find another slot for the remainder
            ItemInstance remainder = item;
            remainder.quantity = item.quantity - to_add;
            return add_item(character_id, remainder);
        }
    }
    
    // Find a free slot
    int free_slot = find_free_slot(inventory);
    if (free_slot < 0) {
        return InventoryResult::error(mir2::common::ErrorCode::INVENTORY_FULL, "Inventory is full");
    }
    
    // Add item to free slot
    ItemInstance new_item = item;
    if (new_item.instance_id == 0) {
        new_item.instance_id = generate_instance_id();
    }
    inventory[free_slot].set(new_item);
    
    return InventoryResult::ok(free_slot);
}

/**
 * @brief 从背包移除物品
 * @param character_id 角色 ID
 * @param slot 槽位索引
 * @param quantity 移除数量
 * @return InventoryResult 操作结果
 * 
 * 如果移除后数量为 0，清空该槽位。
 */
InventoryResult InventorySystem::remove_item(uint32_t character_id, int slot, int quantity) {
    auto it = inventories_.find(character_id);
    if (it == inventories_.end()) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Character has no inventory");
    }
    
    auto& inventory = it->second;
    if (slot < 0 || slot >= static_cast<int>(inventory.size())) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Invalid slot");
    }
    
    if (!inventory[slot].occupied) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Slot is empty");
    }
    
    if (inventory[slot].item.quantity < quantity) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Not enough items in slot");
    }
    
    inventory[slot].item.quantity -= quantity;
    if (inventory[slot].item.quantity <= 0) {
        inventory[slot].clear();
    }
    
    return InventoryResult::ok(slot);
}

/**
 * @brief 移动物品到另一个槽位
 * @param character_id 角色 ID
 * @param from_slot 源槽位
 * @param to_slot 目标槽位
 * @return InventoryResult 操作结果
 * 
 * 交换两个槽位的内容，支持与空槽位交换。
 */
InventoryResult InventorySystem::move_item(uint32_t character_id, int from_slot, int to_slot) {
    auto it = inventories_.find(character_id);
    if (it == inventories_.end()) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Character has no inventory");
    }
    
    auto& inventory = it->second;
    if (from_slot < 0 || from_slot >= static_cast<int>(inventory.size()) ||
        to_slot < 0 || to_slot >= static_cast<int>(inventory.size())) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Invalid slot");
    }
    
    if (!inventory[from_slot].occupied) {
        return InventoryResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Source slot is empty");
    }
    
    // Swap items
    std::swap(inventory[from_slot], inventory[to_slot]);
    
    return InventoryResult::ok(to_slot);
}

/**
 * @brief 检查背包是否已满
 * @param character_id 角色 ID
 * @return true 如果没有空槽位
 */
bool InventorySystem::is_inventory_full(uint32_t character_id) const {
    auto it = inventories_.find(character_id);
    if (it == inventories_.end()) {
        return false;  // Empty inventory is not full
    }
    
    for (const auto& slot : it->second) {
        if (!slot.occupied) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 获取空闲槽位数量
 * @param character_id 角色 ID
 * @return 空闲槽位数
 */
int InventorySystem::get_free_slot_count(uint32_t character_id) const {
    auto it = inventories_.find(character_id);
    if (it == inventories_.end()) {
        return inventory_size_;
    }
    
    int count = 0;
    for (const auto& slot : it->second) {
        if (!slot.occupied) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief 获取背包内容副本
 * @param character_id 角色 ID
 * @return 背包槽位向量的副本
 */
std::vector<InventorySlot> InventorySystem::get_inventory(uint32_t character_id) const {
    auto it = inventories_.find(character_id);
    if (it != inventories_.end()) {
        return it->second;
    }
    return std::vector<InventorySlot>(inventory_size_);
}

/**
 * @brief 获取指定槽位的物品
 * @param character_id 角色 ID
 * @param slot 槽位索引
 * @return std::optional<ItemInstance> 物品实例，槽位为空返回 nullopt
 */
std::optional<ItemInstance> InventorySystem::get_item_at(uint32_t character_id, int slot) const {
    auto it = inventories_.find(character_id);
    if (it == inventories_.end()) {
        return std::nullopt;
    }
    
    if (slot < 0 || slot >= static_cast<int>(it->second.size())) {
        return std::nullopt;
    }
    
    if (!it->second[slot].occupied) {
        return std::nullopt;
    }
    
    return it->second[slot].item;
}


// =============================================================================
// 装备系统操作
// =============================================================================

/**
 * @brief 检查角色是否可以装备物品
 * @param tmpl 物品模板
 * @param character 角色对象
 * @return true 如果满足装备条件
 * 
 * 检查条件：
 * - 物品是否可装备
 * - 等级是否满足要求
 * - 职业是否匹配
 */
bool InventorySystem::can_equip(const ItemTemplate& tmpl, const Character& character) const {
    // Check if item is equippable
    if (!tmpl.is_equippable()) {
        return false;
    }
    
    // Check level requirement
    if (character.get_level() < tmpl.required_level) {
        return false;
    }
    
    // Check class requirement
    if (!tmpl.any_class && tmpl.required_class != character.get_class()) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取可用的装备槽位
 * @param tmpl 物品模板
 * @param equipment 当前装备集
 * @return mir2::common::EquipSlot 可用的装备槽位
 * 
 * 对于戒指和手镯，如果左侧已装备则尝试右侧。
 */
mir2::common::EquipSlot InventorySystem::get_available_equip_slot(const ItemTemplate& tmpl,
                                                     const EquipmentSet& equipment) const {
    auto base_slot = tmpl.get_equip_slot();
    if (!base_slot.has_value()) {
        return mir2::common::EquipSlot::WEAPON;  // Fallback
    }
    
    mir2::common::EquipSlot slot = base_slot.value();
    
    // For rings and bracelets, check if left slot is occupied
    if (slot == mir2::common::EquipSlot::RING_LEFT && equipment.is_occupied(mir2::common::EquipSlot::RING_LEFT)) {
        if (!equipment.is_occupied(mir2::common::EquipSlot::RING_RIGHT)) {
            return mir2::common::EquipSlot::RING_RIGHT;
        }
    }
    
    if (slot == mir2::common::EquipSlot::BRACELET_LEFT && equipment.is_occupied(mir2::common::EquipSlot::BRACELET_LEFT)) {
        if (!equipment.is_occupied(mir2::common::EquipSlot::BRACELET_RIGHT)) {
            return mir2::common::EquipSlot::BRACELET_RIGHT;
        }
    }
    
    return slot;
}

/**
 * @brief 装备物品
 * @param character_id 角色 ID
 * @param inventory_slot 背包槽位索引
 * @param character 角色对象（用于更新属性）
 * @return EquipResult 装备结果，包含属性变化
 * 
 * 装备流程：
 * 1. 验证物品和装备条件
 * 2. 计算属性变化
 * 3. 更新角色属性
 * 4. 交换背包和装备槽的物品
 */
EquipResult InventorySystem::equip_item(uint32_t character_id, int inventory_slot, 
                                         Character& character) {
    // Get inventory
    auto inv_it = inventories_.find(character_id);
    if (inv_it == inventories_.end()) {
        return EquipResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Character has no inventory");
    }
    
    auto& inventory = inv_it->second;
    if (inventory_slot < 0 || inventory_slot >= static_cast<int>(inventory.size())) {
        return EquipResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Invalid inventory slot");
    }
    
    if (!inventory[inventory_slot].occupied) {
        return EquipResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Inventory slot is empty");
    }
    
    ItemInstance item = inventory[inventory_slot].item;
    
    // Get item template
    auto tmpl = get_item_template(item.template_id);
    if (!tmpl.has_value()) {
        return EquipResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Item template not found");
    }
    
    // Check if can equip
    if (!can_equip(tmpl.value(), character)) {
        if (character.get_level() < tmpl->required_level) {
            return EquipResult::error(mir2::common::ErrorCode::LEVEL_REQUIREMENT_NOT_MET, 
                                      "Level requirement not met");
        }
        if (!tmpl->any_class && tmpl->required_class != character.get_class()) {
            return EquipResult::error(mir2::common::ErrorCode::CLASS_REQUIREMENT_NOT_MET,
                                      "Class requirement not met");
        }
        return EquipResult::error(mir2::common::ErrorCode::INVALID_EQUIPMENT_SLOT, "Cannot equip this item");
    }
    
    // Get equipment set
    auto& equipment = get_or_create_equipment(character_id);
    
    // Determine which slot to use
    mir2::common::EquipSlot target_slot = get_available_equip_slot(tmpl.value(), equipment);
    
    // Check if there's an item to unequip
    std::optional<ItemInstance> unequipped;
    mir2::common::CharacterStats old_bonus;
    if (equipment.is_occupied(target_slot)) {
        unequipped = equipment.get(target_slot);
        
        // Get old item's stats
        auto old_tmpl = get_item_template(unequipped->template_id);
        if (old_tmpl.has_value()) {
            old_bonus = old_tmpl->get_stat_bonus();
        }
    }
    
    // Calculate stat change
    mir2::common::CharacterStats new_bonus = tmpl->get_stat_bonus();
    mir2::common::CharacterStats net_change;
    net_change.max_hp = new_bonus.max_hp - old_bonus.max_hp;
    net_change.max_mp = new_bonus.max_mp - old_bonus.max_mp;
    net_change.attack = new_bonus.attack - old_bonus.attack;
    net_change.defense = new_bonus.defense - old_bonus.defense;
    net_change.magic_attack = new_bonus.magic_attack - old_bonus.magic_attack;
    net_change.magic_defense = new_bonus.magic_defense - old_bonus.magic_defense;
    net_change.speed = new_bonus.speed - old_bonus.speed;
    
    // Remove old stats and add new stats
    if (unequipped.has_value()) {
        character.remove_stats(old_bonus);
    }
    character.add_stats(new_bonus);
    
    // Update equipment
    equipment.set(target_slot, item);
    
    // Remove from inventory
    inventory[inventory_slot].clear();
    
    // If there was an unequipped item, put it in inventory
    if (unequipped.has_value()) {
        inventory[inventory_slot].set(unequipped.value());
    }
    
    return EquipResult::ok(target_slot, net_change, unequipped);
}

/**
 * @brief 卸下装备
 * @param character_id 角色 ID
 * @param slot 装备槽位
 * @param character 角色对象（用于更新属性）
 * @return EquipResult 卸下结果，包含属性变化
 * 
 * 卸下流程：
 * 1. 检查背包是否有空位
 * 2. 移除装备属性加成
 * 3. 将装备放入背包
 */
EquipResult InventorySystem::unequip_item(uint32_t character_id, mir2::common::EquipSlot slot,
                                           Character& character) {
    // Check if inventory has space
    if (is_inventory_full(character_id)) {
        return EquipResult::error(mir2::common::ErrorCode::INVENTORY_FULL, "Inventory is full");
    }
    
    // Get equipment
    auto eq_it = equipment_.find(character_id);
    if (eq_it == equipment_.end()) {
        return EquipResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "No equipment found");
    }
    
    auto& equipment = eq_it->second;
    if (!equipment.is_occupied(slot)) {
        return EquipResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Equipment slot is empty");
    }
    
    ItemInstance item = equipment.get(slot).value();
    
    // Get item template for stats
    auto tmpl = get_item_template(item.template_id);
    mir2::common::CharacterStats bonus;
    if (tmpl.has_value()) {
        bonus = tmpl->get_stat_bonus();
    }
    
    // Remove stats from character
    character.remove_stats(bonus);
    
    // Clear equipment slot
    equipment.clear(slot);
    
    // Add to inventory
    auto& inventory = get_or_create_inventory(character_id);
    int free_slot = find_free_slot(inventory);
    if (free_slot >= 0) {
        inventory[free_slot].set(item);
    }
    
    // Return negative stat change (stats removed)
    mir2::common::CharacterStats negative_change;
    negative_change.max_hp = -bonus.max_hp;
    negative_change.max_mp = -bonus.max_mp;
    negative_change.attack = -bonus.attack;
    negative_change.defense = -bonus.defense;
    negative_change.magic_attack = -bonus.magic_attack;
    negative_change.magic_defense = -bonus.magic_defense;
    negative_change.speed = -bonus.speed;
    
    return EquipResult::ok(slot, negative_change, item);
}

/**
 * @brief 获取装备集副本
 * @param character_id 角色 ID
 * @return EquipmentSet 装备集副本
 */
EquipmentSet InventorySystem::get_equipment(uint32_t character_id) const {
    auto it = equipment_.find(character_id);
    if (it != equipment_.end()) {
        return it->second;
    }
    return EquipmentSet{};
}

/**
 * @brief 计算装备总属性加成
 * @param character_id 角色 ID
 * @return mir2::common::CharacterStats 所有装备的属性加成总和
 */
mir2::common::CharacterStats InventorySystem::calculate_equipment_bonus(uint32_t character_id) const {
    mir2::common::CharacterStats total;
    total.level = 0;
    total.hp = 0;
    total.max_hp = 0;
    total.mp = 0;
    total.max_mp = 0;
    total.attack = 0;
    total.defense = 0;
    total.magic_attack = 0;
    total.magic_defense = 0;
    total.speed = 0;
    total.experience = 0;
    total.gold = 0;
    
    auto it = equipment_.find(character_id);
    if (it == equipment_.end()) {
        return total;
    }
    
    const auto& equipment = it->second;
    for (const auto& slot : equipment.slots) {
        if (slot.has_value()) {
            auto tmpl = get_item_template(slot->template_id);
            if (tmpl.has_value()) {
                mir2::common::CharacterStats bonus = tmpl->get_stat_bonus();
                total.max_hp += bonus.max_hp;
                total.max_mp += bonus.max_mp;
                total.attack += bonus.attack;
                total.defense += bonus.defense;
                total.magic_attack += bonus.magic_attack;
                total.magic_defense += bonus.magic_defense;
                total.speed += bonus.speed;
            }
        }
    }
    
    return total;
}


// =============================================================================
// 地面物品操作
// =============================================================================

/**
 * @brief 掉落物品到地面
 * @param item 物品实例
 * @param pos 掉落位置
 * @param map_id 地图 ID
 * @param owner_id 所有者 ID（用于保护期）
 * @return uint32_t 地面物品 ID
 * 
 * 创建地面物品，设置保护期和过期时间。
 */
uint32_t InventorySystem::drop_item(const ItemInstance& item, const mir2::common::Position& pos,
                                     uint32_t map_id, uint32_t owner_id) {
    auto now = std::chrono::system_clock::now();
    int64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    GroundItem ground_item;
    ground_item.ground_id = generate_ground_id();
    ground_item.item = item;
    if (ground_item.item.instance_id == 0) {
        ground_item.item.instance_id = generate_instance_id();
    }
    ground_item.position = pos;
    ground_item.map_id = map_id;
    ground_item.owner_id = owner_id;
    ground_item.drop_time = current_time;
    ground_item.expire_time = current_time + item_expire_time_;
    ground_item.is_protected = (owner_id != 0);
    
    ground_items_[ground_item.ground_id] = ground_item;
    ground_items_by_map_[map_id].push_back(ground_item.ground_id);
    
    return ground_item.ground_id;
}

/**
 * @brief 拾取地面物品
 * @param character_id 角色 ID
 * @param ground_item_id 地面物品 ID
 * @param character 角色对象（用于位置检查）
 * @return PickupResult 拾取结果
 * 
 * 拾取检查：
 * - 物品是否存在
 * - 是否在同一地图
 * - 是否在拾取范围内
 * - 保护期是否已过
 * - 背包是否有空位
 */
PickupResult InventorySystem::pickup_item(uint32_t character_id, uint32_t ground_item_id,
                                           Character& character) {
    // Find ground item
    auto it = ground_items_.find(ground_item_id);
    if (it == ground_items_.end()) {
        return PickupResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Item not found on ground");
    }
    
    GroundItem& ground_item = it->second;
    
    // Check if character is on the same map
    if (ground_item.map_id != character.get_map_id()) {
        return PickupResult::error(mir2::common::ErrorCode::ITEM_NOT_FOUND, "Item is on different map");
    }
    
    // Check pickup range
    int dx = ground_item.position.x - character.get_position().x;
    int dy = ground_item.position.y - character.get_position().y;
    int distance_sq = dx * dx + dy * dy;
    if (distance_sq > pickup_range_ * pickup_range_) {
        return PickupResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE, "Item is too far away");
    }
    
    // Check protection
    auto now = std::chrono::system_clock::now();
    int64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    if (ground_item.is_protected && 
        ground_item.owner_id != character_id &&
        current_time < ground_item.drop_time + item_protection_time_) {
        return PickupResult::error(mir2::common::ErrorCode::INVALID_ACTION, "Item is protected");
    }
    
    // Check inventory space
    if (is_inventory_full(character_id)) {
        return PickupResult::error(mir2::common::ErrorCode::INVENTORY_FULL, "Inventory is full");
    }
    
    // Add to inventory
    auto result = add_item(character_id, ground_item.item);
    if (!result.success) {
        return PickupResult::error(result.error_code, result.error_message);
    }
    
    // Remove from ground
    uint32_t map_id = ground_item.map_id;
    ground_items_.erase(it);
    
    // Remove from map index
    auto& map_items = ground_items_by_map_[map_id];
    map_items.erase(std::remove(map_items.begin(), map_items.end(), ground_item_id), 
                    map_items.end());
    
    return PickupResult::ok(result.affected_slot);
}

/**
 * @brief 获取地图上的所有地面物品
 * @param map_id 地图 ID
 * @return std::vector<GroundItem> 地面物品列表
 */
std::vector<GroundItem> InventorySystem::get_ground_items(uint32_t map_id) const {
    std::vector<GroundItem> result;
    
    auto it = ground_items_by_map_.find(map_id);
    if (it == ground_items_by_map_.end()) {
        return result;
    }
    
    for (uint32_t ground_id : it->second) {
        auto item_it = ground_items_.find(ground_id);
        if (item_it != ground_items_.end()) {
            result.push_back(item_it->second);
        }
    }
    
    return result;
}

/**
 * @brief 获取指定位置附近的地面物品
 * @param map_id 地图 ID
 * @param pos 中心位置
 * @param radius 搜索半径
 * @return std::vector<GroundItem> 范围内的地面物品
 */
std::vector<GroundItem> InventorySystem::get_ground_items_near(uint32_t map_id,
                                                               const mir2::common::Position& pos,
                                                               int radius) const {
    std::vector<GroundItem> result;
    
    auto it = ground_items_by_map_.find(map_id);
    if (it == ground_items_by_map_.end()) {
        return result;
    }
    
    int radius_sq = radius * radius;
    for (uint32_t ground_id : it->second) {
        auto item_it = ground_items_.find(ground_id);
        if (item_it != ground_items_.end()) {
            const GroundItem& item = item_it->second;
            int dx = item.position.x - pos.x;
            int dy = item.position.y - pos.y;
            if (dx * dx + dy * dy <= radius_sq) {
                result.push_back(item);
            }
        }
    }
    
    return result;
}

/**
 * @brief 更新地面物品状态
 * @param current_time 当前时间戳（毫秒）
 * 
 * 清理已过期的地面物品。
 */
void InventorySystem::update_ground_items(int64_t current_time) {
    std::vector<uint32_t> expired_items;
    
    for (const auto& [ground_id, item] : ground_items_) {
        if (current_time >= item.expire_time) {
            expired_items.push_back(ground_id);
        }
    }
    
    for (uint32_t ground_id : expired_items) {
        auto it = ground_items_.find(ground_id);
        if (it != ground_items_.end()) {
            uint32_t map_id = it->second.map_id;
            ground_items_.erase(it);
            
            auto& map_items = ground_items_by_map_[map_id];
            map_items.erase(std::remove(map_items.begin(), map_items.end(), ground_id),
                           map_items.end());
        }
    }
}

// =============================================================================
// 序列化方法
// =============================================================================

/**
 * @brief 序列化背包为 JSON 字符串
 * @param character_id 角色 ID
 * @return JSON 字符串
 */
std::string InventorySystem::serialize_inventory(uint32_t character_id) const {
    nlohmann::json j = nlohmann::json::array();
    
    auto it = inventories_.find(character_id);
    if (it != inventories_.end()) {
        for (size_t i = 0; i < it->second.size(); ++i) {
            if (it->second[i].occupied) {
                nlohmann::json slot_json;
                slot_json["slot"] = i;
                slot_json["item"] = it->second[i].item;
                j.push_back(slot_json);
            }
        }
    }
    
    return j.dump();
}

/**
 * @brief 序列化装备为 JSON 字符串
 * @param character_id 角色 ID
 * @return JSON 字符串
 */
std::string InventorySystem::serialize_equipment(uint32_t character_id) const {
    auto it = equipment_.find(character_id);
    if (it != equipment_.end()) {
        nlohmann::json j;
        to_json(j, it->second);
        return j.dump();
    }
    return "[]";
}

/**
 * @brief 从 JSON 字符串反序列化背包
 * @param character_id 角色 ID
 * @param json JSON 字符串
 */
void InventorySystem::deserialize_inventory(uint32_t character_id, const std::string& json) {
    auto& inventory = get_or_create_inventory(character_id);
    
    // Clear existing inventory
    for (auto& slot : inventory) {
        slot.clear();
    }
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        if (j.is_array()) {
            for (const auto& slot_json : j) {
                size_t slot_index = slot_json.at("slot").get<size_t>();
                if (slot_index < inventory.size()) {
                    ItemInstance item;
                    slot_json.at("item").get_to(item);
                    inventory[slot_index].set(item);
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Invalid JSON, leave inventory empty
    }
}

/**
 * @brief 从 JSON 字符串反序列化装备
 * @param character_id 角色 ID
 * @param json JSON 字符串
 */
void InventorySystem::deserialize_equipment(uint32_t character_id, const std::string& json) {
    auto& equipment = get_or_create_equipment(character_id);
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        from_json(j, equipment);
    } catch (const nlohmann::json::exception&) {
        // Invalid JSON, leave equipment empty
        equipment = EquipmentSet{};
    }
}

/**
 * @brief 清除角色的所有背包和装备数据
 * @param character_id 角色 ID
 */
void InventorySystem::clear_character_data(uint32_t character_id) {
    inventories_.erase(character_id);
    equipment_.erase(character_id);
}

// =============================================================================
// Loot Generation
// =============================================================================

std::vector<ItemInstance> generate_loot(const LootTable& table,
                                         std::function<float()> random_func) {
    std::vector<ItemInstance> drops;
    
    for (const auto& entry : table.entries) {
        float roll = random_func();
        if (roll <= entry.drop_chance) {
            ItemInstance item;
            item.template_id = entry.item_template_id;
            
            // Calculate quantity
            if (entry.max_quantity > entry.min_quantity) {
                float qty_roll = random_func();
                int range = entry.max_quantity - entry.min_quantity + 1;
                item.quantity = entry.min_quantity + 
                    static_cast<int>(qty_roll * range);
            } else {
                item.quantity = entry.min_quantity;
            }
            
            item.durability = 100;
            item.enhancement_level = 0;
            
            drops.push_back(item);
        }
    }
    
    return drops;
}

} // namespace legend2
