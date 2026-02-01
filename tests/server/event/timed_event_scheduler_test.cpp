#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <vector>

#include "game/event/event_handler.h"
#include "game/event/timed_event_scheduler.h"

namespace {

using legend2::game::event::EventHandler;
using legend2::game::event::EventTriggerType;
using legend2::game::event::TimedEventConfig;
using legend2::game::event::TimedEventScheduler;

class TestTimedEventHandler final : public EventHandler {
 public:
    void OnEventTrigger(uint32_t event_id) override {
        triggered_events_.push_back(event_id);
    }

    int Count(uint32_t event_id) const {
        return static_cast<int>(
            std::count(triggered_events_.begin(), triggered_events_.end(), event_id));
    }

 private:
    std::vector<uint32_t> triggered_events_;
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

std::tm GetLocalTimeNow() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now_time_t);
#else
    localtime_r(&now_time_t, &local_time);
#endif
    return local_time;
}

TimedEventConfig MakeBaseConfig(uint32_t event_id, EventTriggerType type) {
    TimedEventConfig config;
    config.event_id = event_id;
    config.event_name = "test_event";
    config.trigger_type = type;
    return config;
}

TimedEventConfig SnapConfigToNow(TimedEventConfig config, const std::tm& now) {
    config.hour = static_cast<uint32_t>(now.tm_hour);
    config.minute = static_cast<uint32_t>(now.tm_min);
    if (config.trigger_type == EventTriggerType::kWeekly) {
        config.day_of_week = static_cast<uint32_t>(now.tm_wday);
    }
    if (config.trigger_type == EventTriggerType::kMonthly) {
        config.day_of_month = static_cast<uint32_t>(now.tm_mday);
    }
    return config;
}

void EnsureTriggeredNow(TimedEventScheduler& scheduler,
                        TimedEventConfig config,
                        TestTimedEventHandler& handler,
                        uint32_t event_id) {
    scheduler.RegisterEvent(config);
    scheduler.Update(0.0f);
    if (handler.Count(event_id) == 0) {
        config = SnapConfigToNow(config, GetLocalTimeNow());
        scheduler.RegisterEvent(config);
        scheduler.Update(0.0f);
    }
}

}  // namespace

TEST(TimedEventSchedulerTest, DailyEventTriggersAtSpecifiedHour) {
    auto& scheduler = TimedEventScheduler::Instance();
    TestTimedEventHandler handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    auto now = GetLocalTimeNow();
    auto config = MakeBaseConfig(1001, EventTriggerType::kDaily);
    config = SnapConfigToNow(config, now);

    EnsureTriggeredNow(scheduler, config, handler, config.event_id);
    EXPECT_EQ(handler.Count(config.event_id), 1);
}

TEST(TimedEventSchedulerTest, WeeklyEventTriggersOnSpecifiedWeekday) {
    auto& scheduler = TimedEventScheduler::Instance();
    TestTimedEventHandler handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    auto now = GetLocalTimeNow();
    auto config = MakeBaseConfig(1002, EventTriggerType::kWeekly);
    config = SnapConfigToNow(config, now);

    EnsureTriggeredNow(scheduler, config, handler, config.event_id);
    EXPECT_EQ(handler.Count(config.event_id), 1);
}

TEST(TimedEventSchedulerTest, MonthlyEventTriggersOnSpecifiedDay) {
    auto& scheduler = TimedEventScheduler::Instance();
    TestTimedEventHandler handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    auto now = GetLocalTimeNow();
    auto config = MakeBaseConfig(1003, EventTriggerType::kMonthly);
    config = SnapConfigToNow(config, now);

    EnsureTriggeredNow(scheduler, config, handler, config.event_id);
    EXPECT_EQ(handler.Count(config.event_id), 1);
}

TEST(TimedEventSchedulerTest, IntervalEventTriggersByAccumulatedDelta) {
    auto& scheduler = TimedEventScheduler::Instance();
    TestTimedEventHandler handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    auto config = MakeBaseConfig(1004, EventTriggerType::kInterval);
    config.interval_seconds = 1.0f;

    scheduler.RegisterEvent(config);

    scheduler.Update(0.4f);
    EXPECT_EQ(handler.Count(config.event_id), 0);

    scheduler.Update(0.7f);
    EXPECT_EQ(handler.Count(config.event_id), 1);

    scheduler.Update(2.2f);
    EXPECT_EQ(handler.Count(config.event_id), 3);

    scheduler.Update(0.5f);
    EXPECT_EQ(handler.Count(config.event_id), 3);
}

TEST(TimedEventSchedulerTest, TriggerEventInvokesHandler) {
    auto& scheduler = TimedEventScheduler::Instance();
    TestTimedEventHandler handler;
    ScopedTimedHandler scoped_handler(scheduler, &handler);

    auto now = GetLocalTimeNow();
    auto config = MakeBaseConfig(1005, EventTriggerType::kDaily);
    config = SnapConfigToNow(config, now);

    scheduler.RegisterEvent(config);
    scheduler.TriggerEvent(config.event_id);

    EXPECT_EQ(handler.Count(config.event_id), 1);
}
