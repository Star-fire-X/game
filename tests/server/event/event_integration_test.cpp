#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "game/event/event_handler.h"
#include "game/event/global_event_manager.h"
#include "game/event/timed_event_scheduler.h"

namespace {

using legend2::game::event::EventHandler;
using legend2::game::event::EventTriggerType;
using legend2::game::event::GlobalEventManager;
using legend2::game::event::GlobalEventType;
using legend2::game::event::TimedEventConfig;
using legend2::game::event::TimedEventScheduler;

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

class ScopedTimedHandler {
 public:
    ScopedTimedHandler(TimedEventScheduler& scheduler, EventHandler* handler)
        : scheduler_(scheduler), handler_(handler) {
        scheduler_.RegisterHandler(handler_);
    }

    ~ScopedTimedHandler() {
        scheduler_.UnregisterHandler(handler_);
    }

 private:
    TimedEventScheduler& scheduler_;
    EventHandler* handler_;
};

class GlobalEventBridge final : public EventHandler {
 public:
    struct Mapping {
        uint32_t timed_event_id = 0;
        GlobalEventType type = GlobalEventType::kDoubleExp;
        int32_t duration_seconds = 0;
        float rate = 1.0f;
    };

    void AddMapping(uint32_t timed_event_id,
                    GlobalEventType type,
                    int32_t duration_seconds,
                    float rate) {
        mappings_.push_back(Mapping{timed_event_id, type, duration_seconds, rate});
    }

    void OnEventTrigger(uint32_t event_id) override {
        triggered_events_.push_back(event_id);
        for (const auto& mapping : mappings_) {
            if (mapping.timed_event_id != event_id) {
                continue;
            }
            GlobalEventManager::Instance().StartEvent(
                mapping.timed_event_id, mapping.type, mapping.duration_seconds, mapping.rate);
        }
    }

    int Count(uint32_t event_id) const {
        return static_cast<int>(
            std::count(triggered_events_.begin(), triggered_events_.end(), event_id));
    }

 private:
    std::vector<Mapping> mappings_;
    std::vector<uint32_t> triggered_events_;
};

TimedEventConfig MakeIntervalConfig(uint32_t event_id, float interval_seconds) {
    TimedEventConfig config;
    config.event_id = event_id;
    config.event_name = "interval_event";
    config.trigger_type = EventTriggerType::kInterval;
    config.interval_seconds = interval_seconds;
    return config;
}

}  // namespace

TEST(EventIntegrationTest, IntervalEventStartsAndExpiresGlobalEvent) {
    GlobalEventCleanup cleanup;
    auto& scheduler = TimedEventScheduler::Instance();
    GlobalEventBridge handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    const uint32_t event_id = 3001;
    auto config = MakeIntervalConfig(event_id, 1.0f);
    handler.AddMapping(event_id, GlobalEventType::kDoubleExp, 1, 2.0f);

    scheduler.RegisterEvent(config);

    scheduler.Update(0.9f);
    EXPECT_FALSE(GlobalEventManager::Instance().IsEventActive(event_id));

    scheduler.Update(0.2f);
    EXPECT_TRUE(GlobalEventManager::Instance().IsEventActive(event_id));
    EXPECT_EQ(handler.Count(event_id), 1);
    EXPECT_FLOAT_EQ(GlobalEventManager::Instance().ApplyExpRate(10.0f), 20.0f);

    GlobalEventManager::Instance().Update(1.1f);
    EXPECT_FALSE(GlobalEventManager::Instance().IsEventActive(event_id));
}

TEST(EventIntegrationTest, MultipleTimedEventsTriggerConcurrentGlobalEvents) {
    GlobalEventCleanup cleanup;
    auto& scheduler = TimedEventScheduler::Instance();
    GlobalEventBridge handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    auto exp_config = MakeIntervalConfig(3002, 0.5f);
    auto drop_config = MakeIntervalConfig(3003, 0.5f);
    auto invasion_config = MakeIntervalConfig(3004, 0.5f);
    auto festival_config = MakeIntervalConfig(3005, 0.5f);

    handler.AddMapping(exp_config.event_id, GlobalEventType::kDoubleExp, 60, 2.0f);
    handler.AddMapping(drop_config.event_id, GlobalEventType::kDoubleDrop, 60, 3.0f);
    handler.AddMapping(invasion_config.event_id, GlobalEventType::kInvasion, 0, 1.0f);
    handler.AddMapping(festival_config.event_id, GlobalEventType::kFestival, 0, 1.0f);

    scheduler.RegisterEvent(exp_config);
    scheduler.RegisterEvent(drop_config);
    scheduler.RegisterEvent(invasion_config);
    scheduler.RegisterEvent(festival_config);

    scheduler.Update(0.5f);

    const auto active_events = GlobalEventManager::Instance().GetActiveEvents();
    EXPECT_EQ(active_events.size(), 4u);
    EXPECT_FLOAT_EQ(GlobalEventManager::Instance().ApplyExpRate(5.0f), 10.0f);
    EXPECT_FLOAT_EQ(GlobalEventManager::Instance().ApplyDropRate(1.0f), 3.0f);
}
