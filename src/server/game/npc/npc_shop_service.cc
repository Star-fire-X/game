/**
 * @file npc_shop_service.cc
 * @brief NPC shop service implementation.
 */

#include "game/npc/npc_shop_service.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "common/item_constants.h"
#include "data/item_template.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/components/npc_shop_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/inventory_system.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::game::npc {

namespace {

constexpr float kDefaultSellRate = 0.5f;
constexpr const char* kMetricSaveCriticalCallsTotal =
    "logic.ecs.save_critical.npc_shop.calls_total";
constexpr const char* kMetricSaveCriticalFailTotal =
    "logic.ecs.save_critical.npc_shop.fail_total";

ecs::ShopItem* FindShopItem(ecs::NpcShopComponent& shop, uint32_t item_id) {
  auto it = std::find_if(shop.items.begin(), shop.items.end(),
                         [item_id](const ecs::ShopItem& entry) {
                           return entry.item_id == item_id;
                         });
  if (it == shop.items.end()) {
    return nullptr;
  }
  return &(*it);
}

const ecs::ShopItem* FindShopItem(const ecs::NpcShopComponent& shop,
                                 uint32_t item_id) {
  auto it = std::find_if(shop.items.begin(), shop.items.end(),
                         [item_id](const ecs::ShopItem& entry) {
                           return entry.item_id == item_id;
                         });
  if (it == shop.items.end()) {
    return nullptr;
  }
  return &(*it);
}

int ResolveBasePrice(const ecs::ShopItem* shop_item, uint32_t item_id) {
  if (shop_item) {
    if (shop_item->price < 0) {
      return -1;
    }
    if (shop_item->price > 0) {
      return shop_item->price;
    }
  }
  const auto* tmpl = data::ItemTemplateManager::Instance().GetTemplate(item_id);
  if (!tmpl) {
    return -1;
  }
  return tmpl->price;
}

float NormalizeTaxRate(float tax_rate) {
  if (!std::isfinite(tax_rate)) {
    return 0.0f;
  }
  if (tax_rate < 0.0f) {
    return mir2::common::trade::kCastleTaxRate;
  }
  return std::min(tax_rate, 1.0f);
}

int64_t ComputeTotalPrice(int base_price, int count, double multiplier) {
  if (base_price < 0 || count <= 0) {
    return -1;
  }
  if (!std::isfinite(multiplier) || multiplier < 0.0) {
    return -1;
  }
  const double total = static_cast<double>(base_price) *
                       static_cast<double>(count) * multiplier;
  if (!std::isfinite(total) || total < 0.0) {
    return -1;
  }
  if (total > static_cast<double>(std::numeric_limits<int>::max())) {
    return -1;
  }
  constexpr double kCastEpsilon = 1e-6;
  return static_cast<int64_t>(total + kCastEpsilon);
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
    SYSLOG_WARN("NpcShopService {} SaveCritical failed character_id={} result={}",
                context,
                identity->id,
                static_cast<int>(save_result));
  }
}

}  // namespace

bool NpcShopService::BuyItem(entt::registry& registry, entt::entity player,
                             entt::entity npc, uint32_t item_id, int count) {
  if (!registry.valid(player) || !registry.valid(npc)) {
    return false;
  }
  if (item_id == 0 || count <= 0) {
    return false;
  }

  auto* shop = registry.try_get<ecs::NpcShopComponent>(npc);
  if (!shop) {
    return false;
  }

  auto* attributes = registry.try_get<ecs::CharacterAttributesComponent>(player);
  if (!attributes) {
    return false;
  }

  ecs::ShopItem* shop_item = FindShopItem(*shop, item_id);
  if (!shop_item) {
    return false;
  }

  if (shop_item->stock >= 0 && shop_item->stock < count) {
    return false;
  }

  const int total_price = CalculateBuyPrice(*shop, item_id, count);
  if (total_price < 0) {
    return false;
  }
  if (attributes->gold < total_price) {
    return false;
  }

  auto added = ecs::InventorySystem::AddItem(registry, player, item_id, count);
  if (!added) {
    return false;
  }

  attributes->gold -= total_price;
  ecs::dirty_tracker::mark_attributes_dirty(registry, player);

  if (shop_item->stock >= 0) {
    shop_item->stock -= count;
  }

  PersistCriticalCharacter(registry, player, "BuyItem");

  return true;
}

bool NpcShopService::SellItem(entt::registry& registry, entt::entity player,
                              entt::entity npc, entt::entity item) {
  if (!registry.valid(player) || !registry.valid(npc) || !registry.valid(item)) {
    return false;
  }

  auto* shop = registry.try_get<ecs::NpcShopComponent>(npc);
  if (!shop || !shop->buy_back) {
    return false;
  }

  auto* attributes = registry.try_get<ecs::CharacterAttributesComponent>(player);
  if (!attributes) {
    return false;
  }

  auto* item_component = registry.try_get<ecs::ItemComponent>(item);
  auto* owner = registry.try_get<ecs::InventoryOwnerComponent>(item);
  if (!item_component || !owner) {
    return false;
  }
  if (owner->owner != player || owner->slot_index < 0) {
    return false;
  }
  if (item_component->item_id == 0 || item_component->count <= 0) {
    return false;
  }

  if (item_component->max_durability >=
          mir2::common::durability::kMinSellDurability &&
      item_component->durability <
          mir2::common::durability::kMinSellDurability) {
    return false;
  }

  const uint32_t item_id = item_component->item_id;
  const int count = item_component->count;

  const int total_price = CalculateSellPrice(*shop, item_id, count);
  if (total_price < 0) {
    return false;
  }
  if (attributes->gold > std::numeric_limits<int>::max() - total_price) {
    return false;
  }

  if (!ecs::InventorySystem::UseItem(registry, player, item, count)) {
    return false;
  }

  attributes->gold += total_price;
  ecs::dirty_tracker::mark_attributes_dirty(registry, player);

  if (auto* shop_item = FindShopItem(*shop, item_id)) {
    if (shop_item->stock >= 0) {
      shop_item->stock += count;
    }
  }

  PersistCriticalCharacter(registry, player, "SellItem");

  return true;
}

int NpcShopService::CalculateBuyPrice(const ecs::NpcShopComponent& shop,
                                      uint32_t item_id, int count) {
  if (item_id == 0 || count <= 0) {
    return -1;
  }

  const auto* shop_item = FindShopItem(shop, item_id);
  if (!shop_item) {
    return -1;
  }

  const int base_price = ResolveBasePrice(shop_item, item_id);
  if (base_price < 0) {
    return -1;
  }

  const float tax_rate = NormalizeTaxRate(shop.tax_rate);
  const double multiplier = 1.0 + static_cast<double>(tax_rate);
  const int64_t total = ComputeTotalPrice(base_price, count, multiplier);
  if (total < 0) {
    return -1;
  }

  return static_cast<int>(total);
}

int NpcShopService::CalculateSellPrice(const ecs::NpcShopComponent& shop,
                                       uint32_t item_id, int count) {
  if (item_id == 0 || count <= 0) {
    return -1;
  }

  const auto* shop_item = FindShopItem(shop, item_id);
  const int base_price = ResolveBasePrice(shop_item, item_id);
  if (base_price < 0) {
    return -1;
  }

  const float tax_rate = NormalizeTaxRate(shop.tax_rate);
  const double tax_multiplier = std::max(0.0, 1.0 - static_cast<double>(tax_rate));
  const double multiplier = static_cast<double>(kDefaultSellRate) * tax_multiplier;
  const int64_t total = ComputeTotalPrice(base_price, count, multiplier);
  if (total < 0) {
    return -1;
  }

  return static_cast<int>(total);
}

}  // namespace mir2::game::npc
