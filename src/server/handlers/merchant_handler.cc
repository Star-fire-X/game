/**
 * @file merchant_handler.cc
 * @brief NPC merchant buy/sell handler.
 */

#include "handlers/merchant_handler.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>

#include <yaml-cpp/yaml.h>

#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/systems/inventory_system.h"

namespace mir2::handlers {

namespace {

constexpr float kDefaultBuyRate = 1.0f;
constexpr float kDefaultSellRate = 0.5f;

template <typename T>
bool TryReadScalar(const YAML::Node& node, const char* key, T* out) {
    if (!out) {
        return false;
    }
    const YAML::Node value = node[key];
    if (!value) {
        return false;
    }
    try {
        *out = value.as<T>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

template <typename T>
bool TryReadFromKeys(const YAML::Node& node,
                     const std::initializer_list<const char*>& keys,
                     T* out) {
    for (const auto* key : keys) {
        if (TryReadScalar(node, key, out)) {
            return true;
        }
    }
    return false;
}

float NormalizeRate(float value, float fallback) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return fallback;
    }
    return value;
}

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

const ShopItem* FindItemInShop(const ShopConfig& shop, uint32_t item_id) {
    auto it = std::find_if(shop.items.begin(), shop.items.end(),
                           [item_id](const ShopItem& item) { return item.item_id == item_id; });
    if (it == shop.items.end()) {
        return nullptr;
    }
    return &(*it);
}

std::unordered_map<uint32_t, uint32_t>& OpenShopMap() {
    static std::unordered_map<uint32_t, uint32_t> map;
    return map;
}

}  // namespace

MerchantHandler::MerchantHandler(entt::registry& registry, ecs::EventBus& event_bus)
    : registry_(registry), event_bus_(event_bus) {
    event_bus_.Subscribe<ecs::events::NpcOpenMerchantEvent>(
        [this](const ecs::events::NpcOpenMerchantEvent& event) {
            if (!registry_.valid(event.player) || event.store_id == 0) {
                return;
            }
            OpenShopMap()[static_cast<uint32_t>(entt::to_integral(event.player))] = event.store_id;
        });
}

void MerchantHandler::LoadShops(const std::string& config_path) {
    shops_.clear();

    try {
        if (config_path.empty() || !std::filesystem::exists(config_path)) {
            return;
        }

        YAML::Node root = YAML::LoadFile(config_path);
        YAML::Node shops_node = root["shops"];
        if (!shops_node) {
            shops_node = root["stores"];
        }
        if (!shops_node) {
            shops_node = root["merchants"];
        }
        if (!shops_node) {
            shops_node = root;
        }
        if (!shops_node) {
            return;
        }

        auto parse_shop_node = [this](const YAML::Node& shop_node,
                                      uint32_t default_store_id) {
            if (!shop_node || !shop_node.IsMap()) {
                return;
            }

            ShopConfig shop;
            shop.buy_rate = kDefaultBuyRate;
            shop.sell_rate = kDefaultSellRate;

            uint32_t store_id = 0;
            if (!TryReadFromKeys(shop_node, {"store_id", "shop_id", "id"}, &store_id)) {
                store_id = default_store_id;
            }
            if (store_id == 0) {
                return;
            }
            shop.store_id = store_id;

            TryReadScalar(shop_node, "name", &shop.name);

            float buy_rate = shop.buy_rate;
            if (TryReadFromKeys(shop_node, {"buy_rate", "buyRate", "buy_rate_pct"}, &buy_rate)) {
                shop.buy_rate = NormalizeRate(buy_rate, kDefaultBuyRate);
            }
            float sell_rate = shop.sell_rate;
            if (TryReadFromKeys(shop_node, {"sell_rate", "sellRate", "sell_rate_pct"}, &sell_rate)) {
                shop.sell_rate = NormalizeRate(sell_rate, kDefaultSellRate);
            }

            YAML::Node items_node = shop_node["items"];
            if (!items_node) {
                items_node = shop_node["goods"];
            }
            if (!items_node) {
                items_node = shop_node["products"];
            }

            if (items_node && items_node.IsSequence()) {
                for (const auto& item_node : items_node) {
                    if (!item_node || !item_node.IsMap()) {
                        continue;
                    }

                    ShopItem item;
                    if (!TryReadFromKeys(item_node, {"item_id", "id", "item"}, &item.item_id)) {
                        continue;
                    }

                    int price = 0;
                    if (!TryReadFromKeys(item_node, {"price", "buy_price", "cost"}, &price)) {
                        continue;
                    }
                    item.price = price;

                    int stock = item.stock;
                    if (TryReadFromKeys(item_node, {"stock", "count", "qty"}, &stock)) {
                        item.stock = stock <= 0 ? -1 : stock;
                    }

                    if (item.item_id == 0 || item.price < 0) {
                        continue;
                    }

                    shop.items.push_back(item);
                }
            }

            shops_[shop.store_id] = std::move(shop);
        };

        if (shops_node.IsSequence()) {
            for (const auto& shop_node : shops_node) {
                parse_shop_node(shop_node, 0);
            }
        } else if (shops_node.IsMap()) {
            for (const auto& entry : shops_node) {
                uint32_t store_id = 0;
                try {
                    if (entry.first && entry.first.IsScalar()) {
                        store_id = entry.first.as<uint32_t>();
                    }
                } catch (const std::exception&) {
                    store_id = 0;
                }
                parse_shop_node(entry.second, store_id);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Merchant shop load failed: " << ex.what() << std::endl;
    }
}

bool MerchantHandler::BuyItem(entt::entity player,
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

    return true;
}

bool MerchantHandler::SellItem(entt::entity player, entt::entity item, int count) {
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

    ShopConfig* selected_shop = nullptr;
    ShopItem* selected_item = nullptr;
    int64_t unit_price = -1;

    const uint32_t player_key = static_cast<uint32_t>(entt::to_integral(player));
    auto& open_shop_map = OpenShopMap();
    auto open_it = open_shop_map.find(player_key);
    if (open_it != open_shop_map.end()) {
        auto shop_it = shops_.find(open_it->second);
        if (shop_it != shops_.end()) {
            ShopItem* shop_item = FindItemInShop(shop_it->second, item_component->item_id);
            if (shop_item) {
                const int64_t candidate = ComputeUnitPrice(shop_item->price,
                                                           shop_it->second.sell_rate);
                if (candidate >= 0) {
                    selected_shop = &shop_it->second;
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
                selected_shop = &shop;
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

    if (!ecs::InventorySystem::UseItem(registry_, player, item, count, &event_bus_)) {
        return false;
    }

    attributes->gold += static_cast<int>(total_price);
    ecs::dirty_tracker::mark_attributes_dirty(registry_, player);

    if (selected_item->stock >= 0) {
        selected_item->stock += count;
    }

    return true;
}

const ShopConfig* MerchantHandler::GetShop(uint32_t store_id) const {
    auto it = shops_.find(store_id);
    if (it == shops_.end()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace mir2::handlers
