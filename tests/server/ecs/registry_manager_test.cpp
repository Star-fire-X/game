#include <gtest/gtest.h>

#include "ecs/registry_manager.h"
#include "ecs/components/character_components.h"

namespace {

class CounterSystem : public mir2::ecs::System {
public:
    explicit CounterSystem(int* counter)
        : System(mir2::ecs::SystemPriority::kMovement), counter_(counter) {}

    void Update(entt::registry& /*registry*/, float /*delta_time*/) override {
        if (counter_) {
            ++(*counter_);
        }
    }

private:
    int* counter_ = nullptr;
};

size_t CountEntities(const entt::registry& registry) {
    size_t count = 0;
    const auto* entity_storage = registry.storage<entt::entity>();
    if (entity_storage) {
      for (const auto entity : entity_storage->each()) {
        (void)entity;
        ++count;
      }
    }
    return count;
}

}  // namespace

TEST(RegistryManagerTest, UpdateAllUpdatesWorldSystems) {
    auto& manager = mir2::ecs::RegistryManager::Instance();
    constexpr uint32_t kMapId1 = 9001;
    constexpr uint32_t kMapId2 = 9002;

    auto* world1 = manager.CreateWorld(kMapId1);
    auto* world2 = manager.CreateWorld(kMapId2);
    ASSERT_NE(world1, nullptr);
    ASSERT_NE(world2, nullptr);

    int counter1 = 0;
    int counter2 = 0;
    world1->CreateSystem<CounterSystem>(&counter1);
    world2->CreateSystem<CounterSystem>(&counter2);

    manager.UpdateAll(0.016f);

    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 1);

    world1->ClearSystems();
    world2->ClearSystems();
}

TEST(RegistryManagerTest, CharacterMovesAcrossWorlds) {
    auto& manager = mir2::ecs::RegistryManager::Instance();
    constexpr uint32_t kMapId1 = 9101;
    constexpr uint32_t kMapId2 = 9102;
    constexpr uint32_t kCharacterId = 50001;

    auto* world1 = manager.CreateWorld(kMapId1);
    auto* world2 = manager.CreateWorld(kMapId2);
    ASSERT_NE(world1, nullptr);
    ASSERT_NE(world2, nullptr);

    auto& character_manager = manager.GetCharacterManager();
    auto entity = character_manager.GetOrCreate(kCharacterId, kMapId1);
    ASSERT_TRUE(world1->Registry().valid(entity));

    EXPECT_EQ(character_manager.TryGetMapId(kCharacterId).value_or(0), kMapId1);

    EXPECT_TRUE(character_manager.MoveToMap(kCharacterId, kMapId2, 10, 11));

    auto moved_entity = character_manager.TryGet(kCharacterId);
    ASSERT_TRUE(moved_entity.has_value());
    EXPECT_TRUE(world2->Registry().valid(*moved_entity));
    EXPECT_FALSE(world1->Registry().valid(entity));

    const auto* state = world2->Registry().try_get<mir2::ecs::CharacterStateComponent>(
        *moved_entity);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->map_id, kMapId2);
    EXPECT_EQ(state->position.x, 10);
    EXPECT_EQ(state->position.y, 11);
}

TEST(RegistryManagerTest, CollectEntityCountsReportsPerWorldCounts) {
    auto& manager = mir2::ecs::RegistryManager::Instance();
    constexpr uint32_t kMapId1 = 9103;
    constexpr uint32_t kMapId2 = 9104;

    auto* world1 = manager.CreateWorld(kMapId1);
    auto* world2 = manager.CreateWorld(kMapId2);
    ASSERT_NE(world1, nullptr);
    ASSERT_NE(world2, nullptr);

    const size_t before_world1 = CountEntities(world1->Registry());
    const size_t before_world2 = CountEntities(world2->Registry());
    const auto before = manager.CollectEntityCounts();

    (void)world1->Registry().create();
    (void)world1->Registry().create();
    (void)world2->Registry().create();

    const auto after = manager.CollectEntityCounts();

    auto find_count = [](const mir2::ecs::WorldEntityCountSnapshot& snapshot,
                         uint32_t map_id) -> size_t {
      for (const auto& [current_map_id, count] : snapshot.per_world) {
        if (current_map_id == map_id) {
          return count;
        }
      }
      return 0;
    };

    EXPECT_EQ(find_count(after, kMapId1), before_world1 + 2);
    EXPECT_EQ(find_count(after, kMapId2), before_world2 + 1);
    EXPECT_EQ(after.total, before.total + 3);
}
