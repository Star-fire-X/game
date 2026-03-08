/**
 * @file merchant_service.cc
 * @brief NPC merchant buy/sell handler.
 */

#include "logic/services/merchant_service.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/inventory_system.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::logic {

namespace {

constexpr const char* kMetricSaveCriticalCallsTotal =
    "logic.ecs.save_critical.merchant.calls_total";
constexpr const char* kMetricSaveCriticalFailTotal =
    "logic.ecs.save_critical.merchant.fail_total";

int64_t ComputeUnitPrice(int base_price, float rate) {
    if (base_price < 0) {
        return -1;
    }
    if (!std::isfinite(rate) || rate < 0.0f) {
        return -1;
    }
    const double scaled = static_cast<double>(base_price) * static_cast<double>(rate);
    if (scaled > static_cast<double>(std::numeric_limits<int>::max())) {
        return -1;
    }
    return static_cast<int64_t>(scaled);
}

int64_t ComputeTotalPrice(int base_price, float rate, int count) {
    if (count <= 0) {
        return -1;
    }
    const int64_t unit = ComputeUnitPrice(base_price, rate);
    if (unit < 0) {
        return -1;
    }
    const int64_t total = unit * static_cast<int64_t>(count);
    if (total > std::numeric_limits<int>::max()) {
        return -1;
    }
    return total;
}

ShopItem* FindItemInShop(ShopConfig& shop, uint32_t item_id) {
    auto it = std::find_if(shop.items.begin(), shop.items.end(),
                           [item_id](const ShopItem& item) { return item.item_id == item_id; });
    if (it == shop.items.end()) {
        return nullptr;
    }
    return &(*it);
}

void PersistCriticalCharacter(entt::registry& registry,
                              entt::entity player,
                              const char* context) {
    if (!registry.valid(player)) {
        return;
    }

    const auto* identity = registry.try_get<ecs::CharacterIdentityComponent>(player);
    if (!identity || identity->id == 0) {
        return;
    }

    monitor::Metrics::Instance().IncrementCounter(kMetricSaveCriticalCallsTotal);
    auto& character_manager = ecs::RegistryManager::Instance().GetCharacterManager();
    auto save_result = character_manager.SaveCritical(identity->id);
    if (save_result == ecs::CharacterEntityManager::SaveResult::kEntityNotFound) {
        character_manager.RebuildIndex();
        save_result = character_manager.SaveCritical(identity->id);
    }
    if (save_result != ecs::CharacterEntityManager::SaveResult::kSuccess) {
        monitor::Metrics::Instance().IncrementCounter(kMetricSaveCriticalFailTotal);
        SYSLOG_WARN("MerchantService {} SaveCritical failed character_id={} result={}",
                    context,
                    identity->id,
                    static_cast<int>(save_result));
    }
}

}  // namespace

MerchantService::MerchantService(entt::registry& registry, ecs::EventBus& event_bus)
    : registry_(registry), event_bus_(event_bus) {
    open_merchant_subscription_ = event_bus_.SubscribeScoped<ecs::events::NpcOpenMerchantEvent>(
        [this](const ecs::events::NpcOpenMerchantEvent& event) {
            if (!registry_.valid(event.player) || event.store_id == 0) {
                return;
            }
            const auto* identity = registry_.try_get<ecs::CharacterIdentityComponent>(event.player);
            if (!identity || identity->id == 0) {
                return;
            }
            open_shop_by_player_id_[identity->id] = event.store_id;
        });
}

void MerchantService::ReplaceAllShops(std::unordered_map<uint32_t, ShopConfig> shops) {
    shops_ = std::move(shops);
    open_shop_by_player_id_.clear();
}

bool MerchantService::BuyItem(entt::entity player,
                              uint32_t store_id,
                              uint32_t item_id,
                              int count) {
    if (count <= 0 || item_id == 0 || !registry_.valid(player)) {
        return false;
    }

    auto* attributes = registry_.try_get<ecs::CharacterAttributesComponent>(player);
    if (!attributes) {
        return false;
    }

    auto shop_it = shops_.find(store_id);
    if (shop_it == shops_.end()) {
        return false;
    }

    ShopConfig& shop = shop_it->second;
    ShopItem* item = FindItemInShop(shop, item_id);
    if (!item) {
        return false;
    }

    if (item->stock >= 0 && item->stock < count) {
        return false;
    }

    const int64_t total_price = ComputeTotalPrice(item->price, shop.buy_rate, count);
    if (total_price < 0) {
        return false;
    }
    if (attributes->gold < total_price) {
        return false;
    }

    auto added = ecs::InventorySystem::AddItem(registry_, player, item_id, count, &event_bus_);
    if (!added) {
        return false;
    }

    attributes->gold -= static_cast<int>(total_price);
    ecs::dirty_tracker::mark_attributes_dirty(registry_, player);

    if (item->stock >= 0) {
        item->stock -= count;
    }

    PersistCriticalCharacter(registry_, player, "BuyItem");

    return true;
}

bool MerchantService::SellItem(entt::entity player, entt::entity item, int count) {
    if (count <= 0 || !registry_.valid(player) || !registry_.valid(item)) {
        return false;
    }

    auto* attributes = registry_.try_get<ecs::CharacterAttributesComponent>(player);
    if (!attributes) {
        return false;
    }

    auto* item_component = registry_.try_get<ecs::ItemComponent>(item);
    auto* owner = registry_.try_get<ecs::InventoryOwnerComponent>(item);
    if (!item_component || !owner) {
        return false;
    }
    if (owner->owner != player) {
        return false;
    }
    if (item_component->count < count) {
        return false;
    }

    ShopItem* selected_item = nullptr;
    int64_t unit_price = -1;

    const auto* identity = registry_.try_get<ecs::CharacterIdentityComponent>(player);
    auto open_it = open_shop_by_player_id_.end();
    if (identity && identity->id != 0) {
        open_it = open_shop_by_player_id_.find(identity->id);
    }
    if (open_it != open_shop_by_player_id_.end()) {
        auto shop_it = shops_.find(open_it->second);
        if (shop_it != shops_.end()) {
            ShopItem* shop_item = FindItemInShop(shop_it->second, item_component->item_id);
            if (shop_item) {
                const int64_t candidate = ComputeUnitPrice(shop_item->price,
                                                           shop_it->second.sell_rate);
                if (candidate >= 0) {
                    selected_item = shop_item;
                    unit_price = candidate;
                }
            }
        }
    }

    if (!selected_item) {
        for (auto& [store_id, shop] : shops_) {
            (void)store_id;
            ShopItem* shop_item = FindItemInShop(shop, item_component->item_id);
            if (!shop_item) {
                continue;
            }
            const int64_t candidate = ComputeUnitPrice(shop_item->price, shop.sell_rate);
            if (candidate < 0) {
                continue;
            }
            if (candidate > unit_price) {
                selected_item = shop_item;
                unit_price = candidate;
            }
        }
    }

    if (!selected_item || unit_price < 0) {
        return false;
    }

    const int64_t total_price = unit_price * static_cast<int64_t>(count);
    if (total_price < 0 || total_price > std::numeric_limits<int>::max()) {
        return false;
    }
    if (attributes->gold > std::numeric_limits<int>::max() - total_price) {
        return false;
    }
    if (selected_item->stock >= 0 &&
        selected_item->stock > std::numeric_limits<int>::max() - count) {
        SYSLOG_WARN("MerchantService::SellItem stock overflow (item_id={}, stock={}, count={})",
                    selected_item->item_id, selected_item->stock, count);
        return false;
    }

    if (!ecs::InventorySystem::UseItem(registry_, player, item, count, &event_bus_)) {
        return false;
    }

    attributes->gold += static_cast<int>(total_price);
    ecs::dirty_tracker::mark_attributes_dirty(registry_, player);

    if (selected_item->stock >= 0) {
        selected_item->stock += count;
    }

    PersistCriticalCharacter(registry_, player, "SellItem");

    return true;
}

const ShopConfig* MerchantService::GetShop(uint32_t store_id) const {
    auto it = shops_.find(store_id);
    if (it == shops_.end()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace mir2::logic
