#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/world.h"

namespace {

class RecordingSystem : public mir2::ecs::System {
 public:
    RecordingSystem(mir2::ecs::SystemPriority priority,
                    std::vector<int>* order,
                    int id)
        : mir2::ecs::System(priority)
        , order_(order)
        , id_(id) {}

    void Update(entt::registry& /*registry*/, float delta_time) override {
        last_delta_ = delta_time;
        update_count_ += 1;
        if (order_) {
            order_->push_back(id_);
        }
    }

    int update_count() const { return update_count_; }
    float last_delta() const { return last_delta_; }

 private:
    std::vector<int>* order_ = nullptr;
    int id_ = 0;
    int update_count_ = 0;
    float last_delta_ = 0.0f;
};

}  // namespace

TEST(WorldTest, CreateSystemAddsToCount) {
    mir2::ecs::World world;

    world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kMovement, nullptr, 1);

    EXPECT_EQ(world.GetSystemCount(), 1u);
}

TEST(WorldTest, ClearSystemsResetsCount) {
    mir2::ecs::World world;
    world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kMovement, nullptr, 1);

    world.ClearSystems();

    EXPECT_EQ(world.GetSystemCount(), 0u);
}

TEST(WorldTest, UpdateInvokesAllSystems) {
    mir2::ecs::World world;
    auto* first = world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kMovement, nullptr, 1);
    auto* second = world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kCombat, nullptr, 2);

    world.Update(0.5f);

    EXPECT_EQ(first->update_count(), 1);
    EXPECT_EQ(second->update_count(), 1);
}

TEST(WorldTest, UpdateUsesPriorityOrder) {
    mir2::ecs::World world;
    std::vector<int> order;
    world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kLevelUp, &order, 3);
    world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kMovement, &order, 1);
    world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kCombat, &order, 2);

    world.Update(0.1f);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(WorldTest, UpdatePassesDeltaTime) {
    mir2::ecs::World world;
    auto* system = world.CreateSystem<RecordingSystem>(mir2::ecs::SystemPriority::kMovement, nullptr, 1);

    world.Update(1.25f);

    EXPECT_FLOAT_EQ(system->last_delta(), 1.25f);
}
