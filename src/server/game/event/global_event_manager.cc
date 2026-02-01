/**
 * @file global_event_manager.cc
 * @brief 全局事件管理器实现
 */

#include "game/event/global_event_manager.h"

#include "ecs/event_bus.h"
#include "log/logger.h"

namespace legend2::game::event {

namespace {

float NormalizeRate(float rate) {
    return rate > 0.0f ? rate : 1.0f;
}

}  // namespace

void GlobalEventManager::StartEvent(uint32_t event_id, GlobalEventType type,
                                    int32_t duration_sec, float rate) {
    if (event_id == 0) {
        SYSLOG_WARN("GlobalEventManager: start event with empty event_id");
    }

    GlobalEventConfig config;
    config.event_id = event_id;
    config.type = type;
    config.duration_seconds = duration_sec;
    config.rate = NormalizeRate(rate);

    ActiveEventState state;
    state.config = config;
    state.remaining_seconds = duration_sec > 0 ? static_cast<float>(duration_sec) : 0.0f;

    mir2::ecs::EventBus* event_bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_events_[event_id] = state;
        event_bus = event_bus_;
    }

    if (event_bus) {
        event_bus->Publish(GlobalEventStartedEvent{config});
    }
}

void GlobalEventManager::StopEvent(uint32_t event_id) {
    GlobalEventConfig config;
    mir2::ecs::EventBus* event_bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_events_.find(event_id);
        if (it == active_events_.end()) {
            return;
        }
        config = it->second.config;
        active_events_.erase(it);
        event_bus = event_bus_;
    }

    if (event_bus) {
        event_bus->Publish(GlobalEventEndedEvent{config});
    }
}

bool GlobalEventManager::IsEventActive(uint32_t event_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_events_.find(event_id) != active_events_.end();
}

float GlobalEventManager::ApplyExpRate(float base_exp) const {
    float multiplier = 1.0f;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [event_id, state] : active_events_) {
        (void)event_id;
        if (state.config.type != GlobalEventType::kDoubleExp) {
            continue;
        }
        multiplier *= NormalizeRate(state.config.rate);
    }
    return base_exp * multiplier;
}

float GlobalEventManager::ApplyDropRate(float base_rate) const {
    float multiplier = 1.0f;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [event_id, state] : active_events_) {
        (void)event_id;
        if (state.config.type != GlobalEventType::kDoubleDrop) {
            continue;
        }
        multiplier *= NormalizeRate(state.config.rate);
    }
    return base_rate * multiplier;
}

void GlobalEventManager::Update(float delta_time) {
    if (delta_time <= 0.0f) {
        return;
    }

    std::vector<GlobalEventConfig> ended_events;
    mir2::ecs::EventBus* event_bus = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        event_bus = event_bus_;
        for (auto it = active_events_.begin(); it != active_events_.end();) {
            auto& state = it->second;
            if (state.config.duration_seconds > 0) {
                state.remaining_seconds -= delta_time;
                if (state.remaining_seconds <= 0.0f) {
                    ended_events.push_back(state.config);
                    it = active_events_.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    if (event_bus) {
        for (const auto& config : ended_events) {
            event_bus->Publish(GlobalEventEndedEvent{config});
        }
    }
}

std::vector<GlobalEventConfig> GlobalEventManager::GetActiveEvents() const {
    std::vector<GlobalEventConfig> result;
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(active_events_.size());
    for (const auto& [event_id, state] : active_events_) {
        (void)event_id;
        result.push_back(state.config);
    }
    return result;
}

void GlobalEventManager::SetEventBus(mir2::ecs::EventBus* event_bus) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_bus_ = event_bus;
}

}  // namespace legend2::game::event
