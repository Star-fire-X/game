#include <gtest/gtest.h>

#include <algorithm>

#include <entt/entt.hpp>

#include "ecs/components/storage_component.h"
#include "ecs/components/trade_component.h"

namespace {

TEST(EcsComponentDefaultsTest, StorageSlotsInitializeToNull) {
    mir2::ecs::StorageComponent storage;
    const bool all_null = std::all_of(
        storage.slots.begin(), storage.slots.end(),
        [](entt::entity slot) { return slot == entt::null; });
    EXPECT_TRUE(all_null);
    EXPECT_EQ(storage.FindFreeSlot(), 0);
}

TEST(EcsComponentDefaultsTest, TradeOfferedItemsInitializeToNull) {
    mir2::ecs::TradeComponent trade;
    const bool all_null = std::all_of(
        trade.offered_items.begin(), trade.offered_items.end(),
        [](entt::entity item) { return item == entt::null; });
    EXPECT_TRUE(all_null);
}

}  // namespace
