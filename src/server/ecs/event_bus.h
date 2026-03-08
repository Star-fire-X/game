/**
 * @file event_bus.h
 * @brief EnTT 事件系统封装
 */

#ifndef MIR2_ECS_EVENT_BUS_H_
#define MIR2_ECS_EVENT_BUS_H_

#include <entt/entt.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace mir2::ecs {

class EventBus {
 public:
  class Subscription {
   public:
    Subscription() = default;
    explicit Subscription(std::function<void()> unsubscribe)
        : unsubscribe_(std::move(unsubscribe)) {}

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : unsubscribe_(std::move(other.unsubscribe_)) {}

    Subscription& operator=(Subscription&& other) noexcept {
      if (this != &other) {
        Reset();
        unsubscribe_ = std::move(other.unsubscribe_);
      }
      return *this;
    }

    ~Subscription() { Reset(); }

    void Reset() {
      if (!unsubscribe_) {
        return;
      }
      auto unsubscribe = std::move(unsubscribe_);
      unsubscribe();
    }

    explicit operator bool() const {
      return static_cast<bool>(unsubscribe_);
    }

   private:
    std::function<void()> unsubscribe_;
  };

  explicit EventBus(entt::registry& registry)
      : registry_(registry),
        alive_token_(std::make_shared<int>(0)) {}

  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;
  EventBus(EventBus&&) = delete;
  EventBus& operator=(EventBus&&) = delete;

  entt::registry& Registry() { return registry_; }
  const entt::registry& Registry() const { return registry_; }

  template<typename Event>
  void PublishImmediate(const Event& event) {
    dispatcher_.trigger(event);
  }

  template<typename Event>
  void PublishDeferred(const Event& event) {
    dispatcher_.enqueue<Event>(event);
  }

  template<typename Event>
  void Publish(const Event& event) {
    PublishImmediate(event);
  }

  template<typename Event, typename Func>
  void Subscribe(Func&& func) {
    std::function<void(Event&)> callback = std::forward<Func>(func);
    RegisterHandler<Event>(std::move(callback));
  }

  template<typename Event, typename Func>
  Subscription SubscribeScoped(Func&& func) {
    std::function<void(Event&)> callback = std::forward<Func>(func);
    auto* handler_ptr = RegisterHandler<Event>(std::move(callback));

    std::weak_ptr<int> weak_alive = alive_token_;
    return Subscription([this, weak_alive, handler_ptr]() {
      if (weak_alive.expired()) {
        return;
      }
      dispatcher_.sink<Event>().template disconnect<&Handler<Event>::Handle>(handler_ptr);
      handler_ptr->connected = false;
      cleanup_disconnected_handlers_ = true;
      CleanupHandlersIfNeeded();
    });
  }

  void FlushEvents() {
    dispatcher_.update();
    CleanupHandlersIfNeeded();
  }

 private:
  struct HandlerBase {
    virtual ~HandlerBase() = default;
    bool connected = true;
  };

  template<typename Event>
  struct Handler : HandlerBase {
    explicit Handler(std::function<void(Event&)> handler)
        : handler(std::move(handler)) {}

    void Handle(Event& event) {
      if (this->connected) {
        handler(event);
      }
    }

    std::function<void(Event&)> handler;
  };

  template<typename Event>
  Handler<Event>* RegisterHandler(std::function<void(Event&)> callback) {
    auto handler = std::make_unique<Handler<Event>>(std::move(callback));
    auto* handler_ptr = handler.get();
    handlers_.push_back(std::move(handler));
    dispatcher_.sink<Event>().template connect<&Handler<Event>::Handle>(handler_ptr);
    return handler_ptr;
  }

  void CleanupHandlersIfNeeded() {
    if (!cleanup_disconnected_handlers_) {
      return;
    }
    handlers_.erase(
        std::remove_if(handlers_.begin(), handlers_.end(),
                       [](const auto& handler) { return !handler->connected; }),
        handlers_.end());
    cleanup_disconnected_handlers_ = false;
  }

  entt::registry& registry_;
  entt::dispatcher dispatcher_;
  std::vector<std::unique_ptr<HandlerBase>> handlers_;
  std::shared_ptr<int> alive_token_;
  bool cleanup_disconnected_handlers_ = false;
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_EVENT_BUS_H_
