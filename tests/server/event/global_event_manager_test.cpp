#include <gtest/gtest.h>

#include <vector>

#include "game/event/global_event_manager.h"

namespace {

using legend2::game::event::GlobalEventConfig;
using legend2::game::event::GlobalEventManager;
using legend2::game::event::GlobalEventType;

class GlobalEventCleanup {
 public:
    GlobalEventCleanup() { Clear(); }
    ~GlobalEventCleanup() { Clear(); }

 private:
    static void Clear() {
        auto& manager = GlobalEventManager::Instance();
        manager.SetEventBus(nullptr);
        const auto active_events = manager.GetActiveEvents();
        for (const auto& config : active_events) {
            manager.StopEvent(config.event_id);
        }
    }
};

}  // namespace

TEST(GlobalEventManagerTest, StartAndStopEvent) {
    GlobalEventCleanup cleanup;
    auto& manager = GlobalEventManager::Instance();

    const uint32_t event_id = 2001;
    manager.StartEvent(event_id, GlobalEventType::kInvasion, 0, 1.0f);
    EXPECT_TRUE(manager.IsEventActive(event_id));

    manager.StopEvent(event_id);
    EXPECT_FALSE(manager.IsEventActive(event_id));
}

TEST(GlobalEventManagerTest, DoubleExpAppliesCorrectMultiplier) {
    GlobalEventCleanup cleanup;
    auto& manager = GlobalEventManager::Instance();

    manager.StartEvent(2002, GlobalEventType::kDoubleExp, 3600, 2.0f);
    manager.StartEvent(2003, GlobalEventType::kDoubleExp, 3600, 1.5f);
    manager.StartEvent(2004, GlobalEventType::kFestival, 3600, 5.0f);

    EXPECT_FLOAT_EQ(manager.ApplyExpRate(100.0f), 300.0f);
}

TEST(GlobalEventManagerTest, DoubleDropAppliesCorrectMultiplier) {
    GlobalEventCleanup cleanup;
    auto& manager = GlobalEventManager::Instance();

    manager.StartEvent(2005, GlobalEventType::kDoubleDrop, 3600, 2.5f);
    manager.StartEvent(2006, GlobalEventType::kInvasion, 3600, 3.0f);

    EXPECT_FLOAT_EQ(manager.ApplyDropRate(1.0f), 2.5f);
}

TEST(GlobalEventManagerTest, EventExpiresAutomatically) {
    GlobalEventCleanup cleanup;
    auto& manager = GlobalEventManager::Instance();

    const uint32_t event_id = 2007;
    manager.StartEvent(event_id, GlobalEventType::kDoubleExp, 1, 2.0f);
    EXPECT_TRUE(manager.IsEventActive(event_id));

    manager.Update(0.5f);
    EXPECT_TRUE(manager.IsEventActive(event_id));

    manager.Update(0.6f);
    EXPECT_FALSE(manager.IsEventActive(event_id));
}

TEST(GlobalEventManagerTest, MultipleEventsRunConcurrently) {
    GlobalEventCleanup cleanup;
    auto& manager = GlobalEventManager::Instance();

    manager.StartEvent(2008, GlobalEventType::kDoubleExp, 3600, 2.0f);
    manager.StartEvent(2009, GlobalEventType::kDoubleDrop, 3600, 3.0f);
    manager.StartEvent(2010, GlobalEventType::kInvasion, 0, 1.0f);
    manager.StartEvent(2011, GlobalEventType::kFestival, 0, 1.0f);

    const auto active_events = manager.GetActiveEvents();
    EXPECT_EQ(active_events.size(), 4u);
    EXPECT_FLOAT_EQ(manager.ApplyExpRate(50.0f), 100.0f);
    EXPECT_FLOAT_EQ(manager.ApplyDropRate(1.0f), 3.0f);
}
