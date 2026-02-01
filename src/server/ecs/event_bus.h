/**
 * @file event_bus.h
 * @brief EnTT 事件系统封装
 */

#ifndef MIR2_ECS_EVENT_BUS_H
#define MIR2_ECS_EVENT_BUS_H

#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace mir2::ecs {

class EventBus {
public:
    explicit EventBus(entt::registry& registry) : registry_(registry) {}

    template<typename Event>
    void Publish(const Event& event) {
        dispatcher_.trigger(event);
    }

    template<typename Event>
    void Subscribe(std::function<void(Event&)> func) {
        auto handler = std::make_unique<Handler<Event>>(std::move(func));
        auto* handler_ptr = handler.get();
        handlers_.push_back(std::move(handler));
        dispatcher_.sink<Event>().template connect<&Handler<Event>::Handle>(handler_ptr);
    }

    void FlushEvents() {
        dispatcher_.update();
    }

private:
    struct HandlerBase {
        virtual ~HandlerBase() = default;
    };

    template<typename Event>
    struct Handler : HandlerBase {
        explicit Handler(std::function<void(Event&)> handler) : handler(std::move(handler)) {}

        void Handle(Event& event) {
            handler(event);
        }

        std::function<void(Event&)> handler;
    };

    entt::registry& registry_;
    entt::dispatcher dispatcher_;
    std::vector<std::unique_ptr<HandlerBase>> handlers_;
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_EVENT_BUS_H
