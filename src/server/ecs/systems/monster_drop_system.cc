/**
 * @file monster_drop_system.cc
 * @brief 怪物掉落系统实现
 */

#include "ecs/systems/monster_drop_system.h"
#include "ecs/components/item_component.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/character_components.h"
#include "ecs/components/party_component.h"
#include "ecs/events/combat_events.h"
#include "ecs/events/inventory_events.h"
#include "ecs/event_bus.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <cmath>

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

void MonsterDropSystem::ReplaceAllDropTables(
    const std::unordered_map<uint32_t, game::entity::MonsterDropTable>& tables) {
    drop_tables_.clear();
    drop_tables_ = tables;
}

void MonsterDropSystem::OnMonsterDeath(entt::entity monster, entt::entity killer) {
    DamageContributors empty_contributors;
    OnMonsterDeath(monster, killer, empty_contributors);
}

void MonsterDropSystem::OnMonsterDeath(entt::entity monster, entt::entity killer,
                                        const DamageContributors& damage_contributors) {
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

    const auto* ai = registry_->try_get<MonsterAIComponent>(monster);
    const bool is_boss = ai && ai->ai_type == MonsterAIType::kBossCowKing;

    auto it = drop_tables_.find(identity->monster_template_id);
    if (it == drop_tables_.end()) {
        return;
    }

    cached_loot_map_id_ = state->map_id;
    const auto drops = is_boss ? SelectDropItemsForBoss(it->second)
                               : SelectDropItems(it->second);
    if (drops.empty()) {
        return;
    }

    const int32_t x = state->position.x;
    const int32_t y = state->position.y;

    // 确定掉落归属
    LootMode loot_mode = LootMode::FREE_FOR_ALL;
    int32_t loot_range = 15;

    // 检查击杀者是否在小队中
    auto* party_member = registry_->try_get<PartyMemberComponent>(killer);
    if (party_member && party_member->party_id != 0) {
        // 查找小队实体
        auto party_view = registry_->view<PartyComponent>();
        for (auto [entity, party] : party_view.each()) {
            if (party.party_id == party_member->party_id) {
                loot_mode = party.loot_mode;
                loot_range = party.loot_range;
                break;
            }
        }
    }

    // 获取范围内的小队成员
    auto candidates = GetPartyMembersInRange(killer, x, y, loot_range);
    if (candidates.empty()) {
        candidates.push_back(killer);
    }

    for (const auto& item : drops) {
        entt::entity owner = SelectLootOwner(candidates, damage_contributors, loot_mode);
        CreateLootEntity(*registry_, item, x, y, owner);
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

std::vector<game::entity::DropItem> MonsterDropSystem::SelectDropItemsForBoss(
    const game::entity::MonsterDropTable& table) {

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    std::vector<game::entity::DropItem> result;
    if (table.items.empty()) {
        return result;
    }

    std::vector<float> adjusted_rates;
    adjusted_rates.reserve(table.items.size());

    for (const auto& item : table.items) {
        float rate = item.drop_rate;
        if (item.boss_bonus != 0.0f) {
            rate *= (1.0f + item.boss_bonus);
        }
        if (item.rarity >= 3) {
            rate += 0.2f;
        }
        rate = std::clamp(rate, 0.0f, 1.0f);
        adjusted_rates.push_back(rate);

        if (dis(gen) < rate) {
            result.push_back(item);
        }
    }

    if (result.empty()) {
        float total_weight = 0.0f;
        for (float rate : adjusted_rates) {
            total_weight += rate;
        }

        size_t index = 0;
        if (total_weight > 0.0f) {
            std::uniform_real_distribution<float> pick_dis(0.0f, total_weight);
            const float pick = pick_dis(gen);
            float accumulated = 0.0f;
            for (size_t i = 0; i < adjusted_rates.size(); ++i) {
                accumulated += adjusted_rates[i];
                if (pick <= accumulated) {
                    index = i;
                    break;
                }
            }
        } else {
            std::uniform_int_distribution<size_t> index_dis(0, table.items.size() - 1);
            index = index_dis(gen);
        }
        result.push_back(table.items[index]);
    }

    return result;
}

void MonsterDropSystem::CreateLootEntity(entt::registry& registry,
                                         const game::entity::DropItem& item,
                                         int32_t x, int32_t y,
                                         entt::entity owner) {
    if (item.item_id == 0) {
        return;
    }

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

    auto& inv_owner = registry.emplace<InventoryOwnerComponent>(loot);
    inv_owner.owner = owner;  // 设置掉落归属
    inv_owner.slot_index = -1;

    auto& state = registry.emplace<CharacterStateComponent>(loot);
    state.map_id = cached_loot_map_id_;
    state.position.x = x;
    state.position.y = y;

    if (event_bus_) {
        events::ItemDroppedEvent event;
        event.character = owner;
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

    death_subscription_ = event_bus_->SubscribeScoped<events::EntityDeathEvent>(
        [this](const events::EntityDeathEvent& event) {
            OnMonsterDeath(event.entity, event.killer);
        });
}

std::vector<entt::entity> MonsterDropSystem::GetPartyMembersInRange(
    entt::entity player, int32_t x, int32_t y, int32_t range) {

    std::vector<entt::entity> result;
    if (!registry_ || !registry_->valid(player)) {
        return result;
    }

    auto* party_member = registry_->try_get<PartyMemberComponent>(player);
    if (!party_member || party_member->party_id == 0) {
        result.push_back(player);
        return result;
    }

    // 查找同小队成员
    auto view = registry_->view<PartyMemberComponent, CharacterStateComponent>();
    for (auto [entity, pm, state] : view.each()) {
        if (pm.party_id != party_member->party_id) {
            continue;
        }
        // 检查距离
        int32_t dx = std::abs(state.position.x - x);
        int32_t dy = std::abs(state.position.y - y);
        if (dx <= range && dy <= range) {
            result.push_back(entity);
        }
    }

    return result;
}

entt::entity MonsterDropSystem::SelectLootOwner(
    const std::vector<entt::entity>& candidates,
    const DamageContributors& damage_contributors,
    LootMode mode) {

    if (candidates.empty()) {
        return entt::null;
    }

    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local size_t round_robin_index = 0;

    switch (mode) {
        case LootMode::FREE_FOR_ALL:
            return entt::null;  // 无归属，任何人可拾取

        case LootMode::ROUND_ROBIN: {
            round_robin_index = (round_robin_index + 1) % candidates.size();
            return candidates[round_robin_index];
        }

        case LootMode::LEADER_ASSIGN:
            return candidates.front();  // 假设第一个是队长

        case LootMode::DAMAGE_BASED: {
            if (damage_contributors.empty()) {
                return candidates.front();
            }
            // 找伤害最高的候选者
            entt::entity best = entt::null;
            int32_t max_damage = 0;
            for (auto e : candidates) {
                auto it = damage_contributors.find(e);
                if (it != damage_contributors.end() && it->second > max_damage) {
                    max_damage = it->second;
                    best = e;
                }
            }
            return best != entt::null ? best : candidates.front();
        }

        default:
            return candidates.front();
    }
}

}  // namespace mir2::ecs
