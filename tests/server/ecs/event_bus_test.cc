#include <gtest/gtest.h>

#include <string>
#include <type_traits>

#include <entt/entt.hpp>

#include "ecs/event_bus.h"

namespace {

using mir2::ecs::EventBus;

struct TestEventA { int value = 0; };
struct TestEventB { std::string text; };

}  // namespace

TEST(EventBusTest, SubscribeAndPublishDeliversEvent) {
    entt::registry registry;
    EventBus bus(registry);

    int received = -1;
    bus.Subscribe<TestEventA>([&](TestEventA& e) {
        received = e.value;
    });

    bus.Publish(TestEventA{42});
    EXPECT_EQ(received, 42);
}

TEST(EventBusTest, MultipleSubscribersReceiveSameEvent) {
    entt::registry registry;
    EventBus bus(registry);

    int count_a = 0;
    int count_b = 0;
    bus.Subscribe<TestEventA>([&](TestEventA&) { ++count_a; });
    bus.Subscribe<TestEventA>([&](TestEventA&) { ++count_b; });

    bus.Publish(TestEventA{1});
    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
}

TEST(EventBusTest, DifferentEventTypesDoNotCrossDeliver) {
    entt::registry registry;
    EventBus bus(registry);

    bool a_called = false;
    bus.Subscribe<TestEventA>([&](TestEventA&) { a_called = true; });

    bus.Publish(TestEventB{"hello"});
    EXPECT_FALSE(a_called);
}

TEST(EventBusTest, RegistryAccessorsReturnSameRegistry) {
    entt::registry registry;
    EventBus bus(registry);

    EXPECT_EQ(&bus.Registry(), &registry);

    const EventBus& const_bus = bus;
    EXPECT_EQ(&const_bus.Registry(), &registry);
}

TEST(EventBusTest, PublishWithNoSubscribersDoesNotCrash) {
    entt::registry registry;
    EventBus bus(registry);

    EXPECT_NO_FATAL_FAILURE(bus.Publish(TestEventA{99}));
    EXPECT_NO_FATAL_FAILURE(bus.Publish(TestEventB{"orphan"}));
}

TEST(EventBusTest, MultiplePublishCallsDeliverMultipleTimes) {
    entt::registry registry;
    EventBus bus(registry);

    int call_count = 0;
    bus.Subscribe<TestEventA>([&](TestEventA&) { ++call_count; });

    bus.Publish(TestEventA{1});
    bus.Publish(TestEventA{2});
    EXPECT_EQ(call_count, 2);
}

TEST(EventBusTest, DeferredPublishRequiresFlush) {
    entt::registry registry;
    EventBus bus(registry);

    int received = 0;
    bus.Subscribe<TestEventA>([&](TestEventA& event) {
        received = event.value;
    });

    bus.PublishDeferred(TestEventA{7});
    EXPECT_EQ(received, 0);

    bus.FlushEvents();
    EXPECT_EQ(received, 7);
}

TEST(EventBusTest, SubscriptionUnsubscribesOnScopeExit) {
    entt::registry registry;
    EventBus bus(registry);

    int call_count = 0;
    {
        auto subscription = bus.SubscribeScoped<TestEventA>([&](TestEventA&) {
            ++call_count;
        });
        bus.Publish(TestEventA{1});
        EXPECT_EQ(call_count, 1);
    }

    bus.Publish(TestEventA{2});
    EXPECT_EQ(call_count, 1);
}

TEST(EventBusTest, EventBusIsNotMoveConstructibleOrAssignable) {
    EXPECT_FALSE(std::is_move_constructible_v<EventBus>);
    EXPECT_FALSE(std::is_move_assignable_v<EventBus>);
}
