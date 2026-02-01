#include "ecs/systems/trade_system.h"

#include <algorithm>
#include <array>
#include <vector>

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/trade_events.h"

namespace mir2::ecs {

namespace {

constexpr int kMaxInventorySlots = mir2::common::constants::MAX_INVENTORY_SIZE;

bool is_valid_inventory_slot(int slot_index) {
    return slot_index >= 0 && slot_index < kMaxInventorySlots;
}

void reset_trade_component(TradeComponent& trade) {
    trade.partner = entt::null;
    trade.state = TradeState::kNone;
    trade.offered_items.fill(entt::null);
    trade.offered_gold = 0;
    trade.confirmed = false;
}

bool is_trade_active(const TradeComponent& trade) {
    return trade.state == TradeState::kTrading || trade.state == TradeState::kConfirmed;
}

void reset_confirmations(TradeComponent& trade, TradeComponent& partner_trade) {
    trade.confirmed = false;
    partner_trade.confirmed = false;

    if (trade.state == TradeState::kConfirmed) {
        trade.state = TradeState::kTrading;
    }
    if (partner_trade.state == TradeState::kConfirmed) {
        partner_trade.state = TradeState::kTrading;
    }
}

std::vector<int> collect_free_inventory_slots(entt::registry& registry,
                                              entt::entity character) {
    std::array<bool, static_cast<std::size_t>(kMaxInventorySlots)> occupied{};
    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner != character) {
            continue;
        }
        if (is_valid_inventory_slot(owner.slot_index)) {
            occupied[static_cast<std::size_t>(owner.slot_index)] = true;
        }
    }

    std::vector<int> free_slots;
    free_slots.reserve(kMaxInventorySlots);
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        if (!occupied[static_cast<std::size_t>(i)]) {
            free_slots.push_back(i);
        }
    }

    return free_slots;
}

bool collect_offered_items(entt::registry& registry,
                           entt::entity trader,
                           const TradeComponent& trade,
                           std::vector<entt::entity>& items,
                           std::vector<int>& slots) {
    items.clear();
    slots.clear();

    for (const auto& item : trade.offered_items) {
        if (item == entt::null) {
            continue;
        }
        if (!registry.valid(item)) {
            return false;
        }

        if (std::find(items.begin(), items.end(), item) != items.end()) {
            return false;
        }

        auto* owner = registry.try_get<InventoryOwnerComponent>(item);
        if (!owner || owner->owner != trader) {
            return false;
        }
        if (!is_valid_inventory_slot(owner->slot_index)) {
            return false;
        }

        items.push_back(item);
        slots.push_back(owner->slot_index);
    }

    return true;
}

void append_freed_slots(std::vector<int>& free_slots,
                        const std::vector<int>& freed_slots) {
    free_slots.insert(free_slots.end(), freed_slots.begin(), freed_slots.end());
    std::sort(free_slots.begin(), free_slots.end());
    free_slots.erase(std::unique(free_slots.begin(), free_slots.end()),
                     free_slots.end());
}

}  // namespace

TradeSystem::TradeSystem()
    : System(SystemPriority::kInventory) {}

void TradeSystem::Update(entt::registry& /*registry*/, float /*delta_time*/) {
    // TODO: 后续可以加入超时/断线自动取消交易逻辑。
}

bool TradeSystem::RequestTrade(entt::registry& registry,
                               entt::entity requester,
                               entt::entity target,
                               EventBus* event_bus) {
    if (!registry.valid(requester) || !registry.valid(target)) {
        return false;
    }
    if (requester == target) {
        return false;
    }

    auto* requester_trade = registry.try_get<TradeComponent>(requester);
    if (requester_trade && requester_trade->state != TradeState::kNone) {
        return false;
    }
    auto* target_trade = registry.try_get<TradeComponent>(target);
    if (target_trade && target_trade->state != TradeState::kNone) {
        return false;
    }

    auto& requester_component = registry.get_or_emplace<TradeComponent>(requester);
    auto& target_component = registry.get_or_emplace<TradeComponent>(target);

    reset_trade_component(requester_component);
    reset_trade_component(target_component);

    requester_component.partner = target;
    requester_component.state = TradeState::kPending;
    target_component.partner = requester;
    target_component.state = TradeState::kPending;

    if (event_bus) {
        events::TradeRequestEvent event;
        event.requester = requester;
        event.target = target;
        event_bus->Publish(event);
    }

    return true;
}

bool TradeSystem::AcceptTrade(entt::registry& registry,
                              entt::entity target,
                              EventBus* event_bus) {
    if (!registry.valid(target)) {
        return false;
    }

    auto* target_trade = registry.try_get<TradeComponent>(target);
    if (!target_trade || target_trade->state != TradeState::kPending) {
        return false;
    }

    entt::entity requester = target_trade->partner;
    if (requester == entt::null || !registry.valid(requester)) {
        return false;
    }

    auto* requester_trade = registry.try_get<TradeComponent>(requester);
    if (!requester_trade || requester_trade->partner != target) {
        return false;
    }
    if (requester_trade->state != TradeState::kPending) {
        return false;
    }

    target_trade->state = TradeState::kTrading;
    requester_trade->state = TradeState::kTrading;
    target_trade->confirmed = false;
    requester_trade->confirmed = false;
    target_trade->offered_items.fill(entt::null);
    requester_trade->offered_items.fill(entt::null);
    target_trade->offered_gold = 0;
    requester_trade->offered_gold = 0;

    if (event_bus) {
        events::TradeAcceptedEvent event;
        event.requester = requester;
        event.target = target;
        event_bus->Publish(event);
    }

    return true;
}

bool TradeSystem::DeclineTrade(entt::registry& registry,
                               entt::entity target,
                               EventBus* event_bus) {
    if (!registry.valid(target)) {
        return false;
    }

    auto* target_trade = registry.try_get<TradeComponent>(target);
    if (!target_trade || target_trade->state != TradeState::kPending) {
        return false;
    }

    entt::entity requester = target_trade->partner;

    if (event_bus) {
        events::TradeDeclinedEvent event;
        event.requester = requester;
        event.target = target;
        event_bus->Publish(event);
    }

    reset_trade_component(*target_trade);

    if (requester != entt::null && registry.valid(requester)) {
        auto* requester_trade = registry.try_get<TradeComponent>(requester);
        if (requester_trade && requester_trade->partner == target) {
            reset_trade_component(*requester_trade);
        }
    }

    return true;
}

bool TradeSystem::AddTradeItem(entt::registry& registry,
                               entt::entity trader,
                               entt::entity item,
                               EventBus* event_bus) {
    if (!registry.valid(trader) || !registry.valid(item)) {
        return false;
    }

    auto* trade = registry.try_get<TradeComponent>(trader);
    if (!trade || !is_trade_active(*trade)) {
        return false;
    }

    entt::entity partner = trade->partner;
    auto* partner_trade = registry.try_get<TradeComponent>(partner);
    if (!partner_trade || partner_trade->partner != trader || !is_trade_active(*partner_trade)) {
        return false;
    }

    auto* owner = registry.try_get<InventoryOwnerComponent>(item);
    auto* item_component = registry.try_get<ItemComponent>(item);
    if (!owner || !item_component) {
        return false;
    }
    if (owner->owner != trader) {
        return false;
    }
    if (!is_valid_inventory_slot(owner->slot_index)) {
        return false;
    }

    for (const auto& offered_item : trade->offered_items) {
        if (offered_item == item) {
            return false;
        }
    }

    for (int i = 0; i < kMaxTradeSlots; ++i) {
        if (trade->offered_items[static_cast<std::size_t>(i)] == entt::null) {
            trade->offered_items[static_cast<std::size_t>(i)] = item;
            reset_confirmations(*trade, *partner_trade);

            if (event_bus) {
                events::TradeItemAddedEvent event;
                event.trader = trader;
                event.partner = partner;
                event.item = item;
                event.item_id = item_component->item_id;
                event.count = item_component->count;
                event.trade_slot_index = i;
                event_bus->Publish(event);
            }

            return true;
        }
    }

    return false;
}

bool TradeSystem::RemoveTradeItem(entt::registry& registry,
                                  entt::entity trader,
                                  entt::entity item) {
    if (!registry.valid(trader)) {
        return false;
    }

    auto* trade = registry.try_get<TradeComponent>(trader);
    if (!trade || !is_trade_active(*trade)) {
        return false;
    }

    bool removed = false;
    for (auto& offered_item : trade->offered_items) {
        if (offered_item == item) {
            offered_item = entt::null;
            removed = true;
            break;
        }
    }

    if (!removed) {
        return false;
    }

    entt::entity partner = trade->partner;
    auto* partner_trade = registry.try_get<TradeComponent>(partner);
    if (partner_trade && partner_trade->partner == trader) {
        reset_confirmations(*trade, *partner_trade);
    } else {
        trade->confirmed = false;
        if (trade->state == TradeState::kConfirmed) {
            trade->state = TradeState::kTrading;
        }
    }

    return true;
}

bool TradeSystem::SetTradeGold(entt::registry& registry,
                               entt::entity trader,
                               int gold) {
    if (!registry.valid(trader)) {
        return false;
    }
    if (gold < 0) {
        return false;
    }

    auto* trade = registry.try_get<TradeComponent>(trader);
    if (!trade || !is_trade_active(*trade)) {
        return false;
    }

    entt::entity partner = trade->partner;
    auto* partner_trade = registry.try_get<TradeComponent>(partner);
    if (!partner_trade || partner_trade->partner != trader || !is_trade_active(*partner_trade)) {
        return false;
    }

    auto* attributes = registry.try_get<CharacterAttributesComponent>(trader);
    if (!attributes || attributes->gold < gold) {
        return false;
    }

    if (trade->offered_gold == gold) {
        return true;
    }

    trade->offered_gold = gold;
    reset_confirmations(*trade, *partner_trade);

    return true;
}

bool TradeSystem::ConfirmTrade(entt::registry& registry,
                               entt::entity trader,
                               EventBus* event_bus) {
    if (!registry.valid(trader)) {
        return false;
    }

    auto* trade = registry.try_get<TradeComponent>(trader);
    if (!trade || !is_trade_active(*trade)) {
        return false;
    }

    entt::entity partner = trade->partner;
    auto* partner_trade = registry.try_get<TradeComponent>(partner);
    if (!partner_trade || partner_trade->partner != trader || !is_trade_active(*partner_trade)) {
        return false;
    }

    trade->confirmed = true;
    trade->state = TradeState::kConfirmed;

    if (event_bus) {
        events::TradeConfirmedEvent event;
        event.trader = trader;
        event.partner = partner;
        event_bus->Publish(event);
    }

    if (partner_trade->confirmed && partner_trade->state == TradeState::kConfirmed) {
        if (!ExecuteTrade(registry, trader, partner, event_bus)) {
            reset_confirmations(*trade, *partner_trade);
            return false;
        }
    }

    return true;
}

bool TradeSystem::ExecuteTrade(entt::registry& registry,
                               entt::entity trader_a,
                               entt::entity trader_b,
                               EventBus* event_bus) {
    if (!registry.valid(trader_a) || !registry.valid(trader_b)) {
        return false;
    }

    auto* trade_a = registry.try_get<TradeComponent>(trader_a);
    auto* trade_b = registry.try_get<TradeComponent>(trader_b);
    if (!trade_a || !trade_b) {
        return false;
    }
    if (trade_a->partner != trader_b || trade_b->partner != trader_a) {
        return false;
    }
    if (!trade_a->confirmed || !trade_b->confirmed) {
        return false;
    }
    if (trade_a->state != TradeState::kConfirmed || trade_b->state != TradeState::kConfirmed) {
        return false;
    }

    auto* attributes_a = registry.try_get<CharacterAttributesComponent>(trader_a);
    auto* attributes_b = registry.try_get<CharacterAttributesComponent>(trader_b);
    if (!attributes_a || !attributes_b) {
        return false;
    }
    if (trade_a->offered_gold < 0 || trade_b->offered_gold < 0) {
        return false;
    }
    if (attributes_a->gold < trade_a->offered_gold ||
        attributes_b->gold < trade_b->offered_gold) {
        return false;
    }

    std::vector<entt::entity> items_a;
    std::vector<entt::entity> items_b;
    std::vector<int> slots_a;
    std::vector<int> slots_b;
    if (!collect_offered_items(registry, trader_a, *trade_a, items_a, slots_a)) {
        return false;
    }
    if (!collect_offered_items(registry, trader_b, *trade_b, items_b, slots_b)) {
        return false;
    }

    std::vector<int> free_slots_a = collect_free_inventory_slots(registry, trader_a);
    std::vector<int> free_slots_b = collect_free_inventory_slots(registry, trader_b);
    append_freed_slots(free_slots_a, slots_a);
    append_freed_slots(free_slots_b, slots_b);

    if (free_slots_a.size() < items_b.size() || free_slots_b.size() < items_a.size()) {
        return false;
    }

    const int gold_from_a = trade_a->offered_gold;
    const int gold_from_b = trade_b->offered_gold;

    for (std::size_t i = 0; i < items_a.size(); ++i) {
        auto* owner = registry.try_get<InventoryOwnerComponent>(items_a[i]);
        if (!owner) {
            return false;
        }
        owner->owner = trader_b;
        owner->slot_index = free_slots_b[i];
    }

    for (std::size_t i = 0; i < items_b.size(); ++i) {
        auto* owner = registry.try_get<InventoryOwnerComponent>(items_b[i]);
        if (!owner) {
            return false;
        }
        owner->owner = trader_a;
        owner->slot_index = free_slots_a[i];
    }

    if (gold_from_a > 0) {
        attributes_a->gold -= gold_from_a;
        attributes_b->gold += gold_from_a;
    }
    if (gold_from_b > 0) {
        attributes_b->gold -= gold_from_b;
        attributes_a->gold += gold_from_b;
    }

    if (!items_a.empty() || !items_b.empty()) {
        dirty_tracker::mark_items_dirty(registry, trader_a);
        dirty_tracker::mark_items_dirty(registry, trader_b);
    }
    if (gold_from_a != 0 || gold_from_b != 0) {
        dirty_tracker::mark_attributes_dirty(registry, trader_a);
        dirty_tracker::mark_attributes_dirty(registry, trader_b);
    }

    reset_trade_component(*trade_a);
    reset_trade_component(*trade_b);

    if (event_bus) {
        events::TradeCompletedEvent event;
        event.trader_a = trader_a;
        event.trader_b = trader_b;
        event.gold_from_a = gold_from_a;
        event.gold_from_b = gold_from_b;
        event_bus->Publish(event);
    }

    return true;
}

bool TradeSystem::CancelTrade(entt::registry& registry,
                              entt::entity trader,
                              EventBus* event_bus) {
    if (!registry.valid(trader)) {
        return false;
    }

    auto* trade = registry.try_get<TradeComponent>(trader);
    if (!trade || trade->state == TradeState::kNone) {
        return false;
    }

    const entt::entity partner = trade->partner;

    reset_trade_component(*trade);

    if (partner != entt::null && registry.valid(partner)) {
        auto* partner_trade = registry.try_get<TradeComponent>(partner);
        if (partner_trade && partner_trade->partner == trader) {
            reset_trade_component(*partner_trade);
        }
    }

    if (event_bus) {
        events::TradeCancelledEvent event;
        event.trader = trader;
        event.partner = partner;
        event_bus->Publish(event);
    }

    return true;
}

}  // namespace mir2::ecs
