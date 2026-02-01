/**
 * @file monster_drop_system.cc
 * @brief 怪物掉落系统实现
 */

#include "ecs/systems/monster_drop_system.h"
#include "ecs/components/item_component.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/character_components.h"
#include "ecs/events/inventory_events.h"
#include "ecs/events/skill_events.h"
#include "ecs/event_bus.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>

#include <yaml-cpp/yaml.h>

namespace mir2::ecs {

MonsterDropSystem::MonsterDropSystem() = default;

MonsterDropSystem::MonsterDropSystem(entt::registry& registry, EventBus& event_bus)
    : registry_(&registry),
      event_bus_(&event_bus) {}

MonsterDropSystem::~MonsterDropSystem() = default;

void MonsterDropSystem::LoadDropTables(const std::string& config_path) {
    // 解析YAML掉落表配置
    drop_tables_.clear();

    try {
        if (config_path.empty() || !std::filesystem::exists(config_path)) {
            return;
        }

        YAML::Node root = YAML::LoadFile(config_path);
        YAML::Node tables_node = root["drop_tables"];
        if (!tables_node) {
            tables_node = root["tables"];
        }
        if (!tables_node) {
            tables_node = root["monsters"];
        }
        if (!tables_node) {
            tables_node = root;
        }
        if (!tables_node || !tables_node.IsSequence()) {
            return;
        }

        for (const auto& table_node : tables_node) {
            if (!table_node || !table_node.IsMap()) {
                continue;
            }

            game::entity::MonsterDropTable table;
            if (table_node["monster_template_id"]) {
                table.monster_template_id = table_node["monster_template_id"].as<uint32_t>();
            } else if (table_node["template_id"]) {
                table.monster_template_id = table_node["template_id"].as<uint32_t>();
            } else if (table_node["monster_id"]) {
                table.monster_template_id = table_node["monster_id"].as<uint32_t>();
            } else if (table_node["id"]) {
                table.monster_template_id = table_node["id"].as<uint32_t>();
            }

            if (table.monster_template_id == 0) {
                continue;
            }

            YAML::Node items_node = table_node["items"] ? table_node["items"] : table_node["drops"];
            if (items_node && items_node.IsSequence()) {
                for (const auto& item_node : items_node) {
                    if (!item_node || !item_node.IsMap()) {
                        continue;
                    }

                    game::entity::DropItem item;
                    if (item_node["item_id"]) {
                        item.item_id = item_node["item_id"].as<uint32_t>();
                    } else if (item_node["item_template_id"]) {
                        item.item_id = item_node["item_template_id"].as<uint32_t>();
                    } else if (item_node["id"]) {
                        item.item_id = item_node["id"].as<uint32_t>();
                    }

                    if (item.item_id == 0) {
                        continue;
                    }

                    if (item_node["drop_rate"]) {
                        item.drop_rate = item_node["drop_rate"].as<float>();
                    } else if (item_node["rate"]) {
                        item.drop_rate = item_node["rate"].as<float>();
                    }
                    item.drop_rate = std::clamp(item.drop_rate, 0.0f, 1.0f);

                    if (item_node["count"]) {
                        const int count = item_node["count"].as<int>();
                        item.min_count = count;
                        item.max_count = count;
                    } else {
                        if (item_node["min_count"]) {
                            item.min_count = item_node["min_count"].as<int>();
                        } else if (item_node["min"]) {
                            item.min_count = item_node["min"].as<int>();
                        }
                        if (item_node["max_count"]) {
                            item.max_count = item_node["max_count"].as<int>();
                        } else if (item_node["max"]) {
                            item.max_count = item_node["max"].as<int>();
                        }
                    }

                    if (item_node["rarity"]) {
                        item.rarity = item_node["rarity"].as<int>();
                    }
                    if (item_node["boss_bonus"]) {
                        item.boss_bonus = item_node["boss_bonus"].as<float>();
                    }

                    table.items.push_back(item);
                }
            }

            drop_tables_[table.monster_template_id] = std::move(table);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Drop table load failed: " << ex.what() << std::endl;
    }
}

void MonsterDropSystem::OnMonsterDeath(entt::entity monster, entt::entity killer) {
    (void)killer;
    if (!registry_ || !registry_->valid(monster)) {
        return;
    }

    auto* identity = registry_->try_get<MonsterIdentityComponent>(monster);
    if (!identity || identity->monster_template_id == 0) {
        return;
    }

    auto* state = registry_->try_get<CharacterStateComponent>(monster);
    if (!state) {
        return;
    }

    auto it = drop_tables_.find(identity->monster_template_id);
    if (it == drop_tables_.end()) {
        return;
    }

    // 缓存地图ID以便创建掉落实体
    cached_loot_map_id_ = state->map_id;

    const auto drops = SelectDropItems(it->second);
    if (drops.empty()) {
        return;
    }

    const int32_t x = state->position.x;
    const int32_t y = state->position.y;

    for (const auto& item : drops) {
        CreateLootEntity(*registry_, item, x, y);
    }
}

std::vector<game::entity::DropItem> MonsterDropSystem::SelectDropItems(
    const game::entity::MonsterDropTable& table) {
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    std::vector<game::entity::DropItem> result;
    for (const auto& item : table.items) {
        if (dis(gen) < item.drop_rate) {
            result.push_back(item);
        }
    }
    return result;
}

void MonsterDropSystem::CreateLootEntity(entt::registry& registry,
                                         const game::entity::DropItem& item,
                                         int32_t x, int32_t y) {
    if (item.item_id == 0) {
        return;
    }

    // 随机数量用于生成掉落堆叠
    static thread_local std::mt19937 gen(std::random_device{}());
    const int min_count = std::max(1, item.min_count);
    const int max_count = std::max(min_count, item.max_count);
    std::uniform_int_distribution<int> count_dis(min_count, max_count);
    const int count = count_dis(gen);

    if (count <= 0) {
        return;
    }

    entt::entity loot = registry.create();

    auto& item_component = registry.emplace<ItemComponent>(loot);
    item_component.item_id = item.item_id;
    item_component.count = count;

    // 标记为地面物品（无所属）
    auto& owner = registry.emplace<InventoryOwnerComponent>(loot);
    owner.owner = entt::null;
    owner.slot_index = -1;

    auto& state = registry.emplace<CharacterStateComponent>(loot);
    state.map_id = cached_loot_map_id_;
    state.position.x = x;
    state.position.y = y;

    if (event_bus_) {
        events::ItemDroppedEvent event;
        event.character = entt::null;
        event.item = loot;
        event.item_id = item_component.item_id;
        event.count = item_component.count;
        event.slot_index = -1;
        event_bus_->Publish(event);
    }
}

void MonsterDropSystem::SubscribeToDeathEvents() {
    if (!event_bus_) {
        return;
    }

    event_bus_->Subscribe<events::EntityDeathEvent>(
        [this](const events::EntityDeathEvent& event) {
            OnMonsterDeath(event.entity, event.killer);
        });
}

}  // namespace mir2::ecs
