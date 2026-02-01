/**
 * @file timed_event_scheduler.cc
 * @brief 定时事件调度器实现
 */

#include "game/event/timed_event_scheduler.h"

#include "game/event/event_handler.h"
#include "log/logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace legend2::game::event {

namespace {

struct DateStamp {
    int year = 0;
    int month = 0;
    int day = 0;
};

DateStamp MakeDateStamp(const std::tm& time_info) {
    return DateStamp{time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday};
}

bool SameDate(const DateStamp& lhs, const DateStamp& rhs) {
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day;
}

std::tm ToLocalTime(std::time_t time_value) {
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time_value);
#else
    localtime_r(&time_value, &local_time);
#endif
    return local_time;
}

bool IsTimeReached(const TimedEventConfig& config, const std::tm& now) {
    if (static_cast<uint32_t>(now.tm_hour) > config.hour) {
        return true;
    }
    if (static_cast<uint32_t>(now.tm_hour) == config.hour &&
        static_cast<uint32_t>(now.tm_min) >= config.minute) {
        return true;
    }
    return false;
}

}  // namespace

bool TimedEventScheduler::IsTimeValid(const TimedEventConfig& config) {
    return config.hour < 24 && config.minute < 60;
}

void TimedEventScheduler::RegisterEvent(const TimedEventConfig& config) {
    if (config.event_id == 0) {
        SYSLOG_WARN("TimedEventScheduler: Register event with empty event_id");
    }

    if (config.trigger_type == EventTriggerType::kInterval && config.interval_seconds <= 0.0f) {
        SYSLOG_WARN("TimedEventScheduler: event_id={} interval_seconds invalid", config.event_id);
    }

    if (config.trigger_type != EventTriggerType::kInterval && !IsTimeValid(config)) {
        SYSLOG_WARN("TimedEventScheduler: event_id={} time invalid (hour={}, minute={})",
                    config.event_id, config.hour, config.minute);
    }

    if (config.trigger_type == EventTriggerType::kWeekly && config.day_of_week > 6) {
        SYSLOG_WARN("TimedEventScheduler: event_id={} day_of_week invalid ({})",
                    config.event_id, config.day_of_week);
    }

    if (config.trigger_type == EventTriggerType::kMonthly &&
        (config.day_of_month == 0 || config.day_of_month > 31)) {
        SYSLOG_WARN("TimedEventScheduler: event_id={} day_of_month invalid ({})",
                    config.event_id, config.day_of_month);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = events_.find(config.event_id);
    if (it == events_.end()) {
        events_.emplace(config.event_id, TimedEventState{config});
        return;
    }

    bool trigger_type_changed = it->second.config.trigger_type != config.trigger_type;
    it->second.config = config;
    if (trigger_type_changed) {
        it->second.interval_accumulator = 0.0f;
        it->second.last_trigger_year = 0;
        it->second.last_trigger_month = 0;
        it->second.last_trigger_day = 0;
    }
}

void TimedEventScheduler::Update(float delta_time) {
    std::vector<uint32_t> due_events;

    const auto now = std::chrono::system_clock::now();
    const auto now_time_t = std::chrono::system_clock::to_time_t(now);
    const std::tm now_tm = ToLocalTime(now_time_t);
    const DateStamp today = MakeDateStamp(now_tm);

    const auto update_last_trigger = [&today](TimedEventState& state) {
        state.last_trigger_year = today.year;
        state.last_trigger_month = today.month;
        state.last_trigger_day = today.day;
    };
    const auto has_triggered_today = [&today](const TimedEventState& state) {
        DateStamp last{state.last_trigger_year, state.last_trigger_month, state.last_trigger_day};
        return SameDate(last, today);
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [event_id, state] : events_) {
            const auto& config = state.config;
            switch (config.trigger_type) {
                case EventTriggerType::kInterval: {
                    if (config.interval_seconds <= 0.0f) {
                        break;
                    }
                    if (delta_time > 0.0f) {
                        state.interval_accumulator += delta_time;
                    }
                    while (state.interval_accumulator >= config.interval_seconds) {
                        state.interval_accumulator -= config.interval_seconds;
                        due_events.push_back(event_id);
                        update_last_trigger(state);
                    }
                    break;
                }
                case EventTriggerType::kDaily: {
                    if (!IsTimeValid(config)) {
                        break;
                    }
                    if (IsTimeReached(config, now_tm) && !has_triggered_today(state)) {
                        update_last_trigger(state);
                        due_events.push_back(event_id);
                    }
                    break;
                }
                case EventTriggerType::kWeekly: {
                    if (!IsTimeValid(config) || config.day_of_week > 6) {
                        break;
                    }
                    if (static_cast<uint32_t>(now_tm.tm_wday) != config.day_of_week) {
                        break;
                    }
                    if (IsTimeReached(config, now_tm) && !has_triggered_today(state)) {
                        update_last_trigger(state);
                        due_events.push_back(event_id);
                    }
                    break;
                }
                case EventTriggerType::kMonthly: {
                    if (!IsTimeValid(config) || config.day_of_month == 0 ||
                        config.day_of_month > 31) {
                        break;
                    }
                    if (static_cast<uint32_t>(now_tm.tm_mday) != config.day_of_month) {
                        break;
                    }
                    if (IsTimeReached(config, now_tm) && !has_triggered_today(state)) {
                        update_last_trigger(state);
                        due_events.push_back(event_id);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    for (uint32_t event_id : due_events) {
        DispatchEvent(event_id);
    }
}

void TimedEventScheduler::TriggerEvent(uint32_t event_id) {
    const auto now = std::chrono::system_clock::now();
    const auto now_time_t = std::chrono::system_clock::to_time_t(now);
    const std::tm now_tm = ToLocalTime(now_time_t);
    const DateStamp today = MakeDateStamp(now_tm);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = events_.find(event_id);
        if (it == events_.end()) {
            SYSLOG_WARN("TimedEventScheduler: event_id={} not registered", event_id);
            return;
        }

        if (it->second.config.trigger_type == EventTriggerType::kInterval) {
            it->second.interval_accumulator = 0.0f;
        } else {
            it->second.last_trigger_year = today.year;
            it->second.last_trigger_month = today.month;
            it->second.last_trigger_day = today.day;
        }
    }

    DispatchEvent(event_id);
}

std::vector<TimedEventConfig> TimedEventScheduler::GetAllEvents() const {
    std::vector<TimedEventConfig> result;
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(events_.size());
    for (const auto& [event_id, state] : events_) {
        result.push_back(state.config);
    }
    return result;
}

void TimedEventScheduler::RegisterHandler(EventHandler* handler) {
    if (!handler) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(handlers_.begin(), handlers_.end(), handler);
    if (it == handlers_.end()) {
        handlers_.push_back(handler);
    }
}

void TimedEventScheduler::UnregisterHandler(EventHandler* handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(std::remove(handlers_.begin(), handlers_.end(), handler), handlers_.end());
}

void TimedEventScheduler::DispatchEvent(uint32_t event_id) {
    std::vector<EventHandler*> handlers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers = handlers_;
    }

    for (auto* handler : handlers) {
        if (handler) {
            handler->OnEventTrigger(event_id);
        }
    }
}

}  // namespace legend2::game::event
