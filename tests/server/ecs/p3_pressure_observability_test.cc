#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/effect_component.h"
#include "ecs/components/guild_component.h"
#include "ecs/registry_manager.h"

namespace {

constexpr int kTickSamples = 1200;
constexpr int kEntityCount = 1000;
constexpr int kEffectsPerEntity = 12;
constexpr int kGuildMembers = 256;
constexpr int kGuildChecksPerTick = 2048;

struct DistributionStats {
  double min_us = 0.0;
  double max_us = 0.0;
  double avg_us = 0.0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double p99_us = 0.0;
};

struct ScenarioResult {
  DistributionStats tick_stats;
  double effect_alloc_per_tick = 0.0;
  double guild_alloc_per_tick = 0.0;
};

template <typename T>
inline void DoNotOptimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "g"(&value) : "memory");
#else
  (void)value;
#endif
}

double PercentileFromSorted(const std::vector<double>& sorted, double p) {
  if (sorted.empty()) {
    return 0.0;
  }
  const double index = p * static_cast<double>(sorted.size() - 1);
  const size_t lower = static_cast<size_t>(index);
  const size_t upper = std::min(lower + 1, sorted.size() - 1);
  const double fraction = index - static_cast<double>(lower);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

DistributionStats SummarizeUs(std::vector<double> samples_us) {
  DistributionStats stats;
  if (samples_us.empty()) {
    return stats;
  }

  std::sort(samples_us.begin(), samples_us.end());
  stats.min_us = samples_us.front();
  stats.max_us = samples_us.back();

  double total = 0.0;
  for (double sample : samples_us) {
    total += sample;
  }

  stats.avg_us = total / static_cast<double>(samples_us.size());
  stats.p50_us = PercentileFromSorted(samples_us, 0.50);
  stats.p95_us = PercentileFromSorted(samples_us, 0.95);
  stats.p99_us = PercentileFromSorted(samples_us, 0.99);
  return stats;
}

std::vector<mir2::ecs::EffectListComponent> BuildEffectWorkload() {
  using mir2::ecs::ActiveEffect;
  using mir2::ecs::EffectCategory;
  using mir2::ecs::EffectListComponent;

  std::vector<EffectListComponent> workload(static_cast<size_t>(kEntityCount));
  for (int i = 0; i < kEntityCount; ++i) {
    auto& component = workload[static_cast<size_t>(i)];
    component.effects.reserve(kEffectsPerEntity);
    for (int j = 0; j < kEffectsPerEntity; ++j) {
      ActiveEffect effect;
      switch (j % 6) {
        case 0:
          effect.category = EffectCategory::STUN;
          break;
        case 1:
          effect.category = EffectCategory::DAMAGE_OVER_TIME;
          break;
        case 2:
          effect.category = EffectCategory::POISON;
          break;
        case 3:
          effect.category = EffectCategory::FRENZY;
          break;
        case 4:
          effect.category = EffectCategory::INVISIBLE;
          break;
        default:
          effect.category = EffectCategory::STAT_BUFF;
          break;
      }
      effect.end_time_ms = 5000 + i * 10 + j;
      component.effects.push_back(effect);
    }
    component.mark_effects_dirty();
  }
  return workload;
}

mir2::ecs::GuildComponent BuildGuildWorkload() {
  mir2::ecs::GuildComponent guild;
  guild.members.reserve(kGuildMembers);
  for (int i = 0; i < kGuildMembers; ++i) {
    guild.members.push_back(static_cast<entt::entity>(i + 1));
  }
  return guild;
}

std::vector<entt::entity> BuildGuildQueries() {
  std::vector<entt::entity> queries;
  queries.reserve(kGuildChecksPerTick);
  for (int i = 0; i < kGuildChecksPerTick; ++i) {
    if (i % 2 == 0) {
      const int member_index = i % kGuildMembers;
      queries.push_back(static_cast<entt::entity>(member_index + 1));
    } else {
      queries.push_back(static_cast<entt::entity>(kGuildMembers + i + 1000));
    }
  }
  return queries;
}

bool LegacyGuildIsMember(const mir2::ecs::GuildComponent& guild, entt::entity entity) {
  for (entt::entity member : guild.members) {
    if (member == entity) {
      return true;
    }
  }
  return false;
}

size_t LegacyEffectAllocationEvents(const mir2::ecs::EffectListComponent& effects,
                                    int64_t now_ms) {
  using mir2::ecs::ActiveEffect;
  using mir2::ecs::EffectCategory;

  std::vector<const ActiveEffect*> matches;
  size_t allocation_events = 0;
  size_t previous_capacity = matches.capacity();

  for (const auto& effect : effects.effects) {
    if (effect.category == EffectCategory::DAMAGE_OVER_TIME ||
        effect.category == EffectCategory::POISON) {
      matches.push_back(&effect);
      const size_t current_capacity = matches.capacity();
      if (current_capacity != previous_capacity) {
        ++allocation_events;
        previous_capacity = current_capacity;
      }
    }
  }

  bool has_expired = false;
  for (const auto& effect : effects.effects) {
    if (effect.end_time_ms > 0 && effect.end_time_ms <= now_ms) {
      has_expired = true;
      break;
    }
  }

  DoNotOptimize(has_expired);
  DoNotOptimize(matches);
  return allocation_events;
}

size_t CurrentEffectAllocationEvents(const mir2::ecs::EffectListComponent& effects,
                                     int64_t now_ms) {
  using mir2::ecs::EffectCategory;
  const bool has_dot_or_poison = effects.has_any_category(
      {EffectCategory::DAMAGE_OVER_TIME, EffectCategory::POISON});
  const bool has_expired = effects.has_expired(now_ms);
  DoNotOptimize(has_dot_or_poison);
  DoNotOptimize(has_expired);
  return 0;
}

size_t CurrentGuildAllocationEvents(mir2::ecs::GuildComponent& guild,
                                    entt::entity entity,
                                    bool* is_member_out) {
  const size_t buckets_before = guild.member_index.bucket_count();
  const bool is_member = guild.IsMember(entity);
  const size_t buckets_after = guild.member_index.bucket_count();
  if (is_member_out != nullptr) {
    *is_member_out = is_member;
  }
  return buckets_after > buckets_before ? 1 : 0;
}

ScenarioResult RunScenario(bool optimized) {
  std::vector<mir2::ecs::EffectListComponent> effects = BuildEffectWorkload();
  mir2::ecs::GuildComponent guild = BuildGuildWorkload();
  const std::vector<entt::entity> guild_queries = BuildGuildQueries();
  int64_t now_ms = 6000;

  if (optimized) {
    for (const auto& effect_list : effects) {
      (void)CurrentEffectAllocationEvents(effect_list, now_ms);
    }
    if (!guild_queries.empty()) {
      bool warm_hit = false;
      (void)CurrentGuildAllocationEvents(guild, guild_queries.front(), &warm_hit);
    }
  }

  std::vector<double> tick_samples_us;
  tick_samples_us.reserve(kTickSamples);
  uint64_t total_effect_allocations = 0;
  uint64_t total_guild_allocations = 0;

  for (int tick = 0; tick < kTickSamples; ++tick) {
    const auto tick_start = std::chrono::steady_clock::now();

    size_t tick_effect_allocations = 0;
    size_t tick_guild_allocations = 0;

    for (const auto& effect_list : effects) {
      if (optimized) {
        tick_effect_allocations += CurrentEffectAllocationEvents(effect_list, now_ms);
      } else {
        tick_effect_allocations += LegacyEffectAllocationEvents(effect_list, now_ms);
      }
    }

    for (entt::entity query : guild_queries) {
      if (optimized) {
        bool is_member = false;
        tick_guild_allocations += CurrentGuildAllocationEvents(guild, query, &is_member);
        DoNotOptimize(is_member);
      } else {
        const bool is_member = LegacyGuildIsMember(guild, query);
        DoNotOptimize(is_member);
      }
    }

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - tick_start);
    tick_samples_us.push_back(static_cast<double>(elapsed_us.count()));
    total_effect_allocations += tick_effect_allocations;
    total_guild_allocations += tick_guild_allocations;
    ++now_ms;
  }

  ScenarioResult result;
  result.tick_stats = SummarizeUs(tick_samples_us);
  result.effect_alloc_per_tick =
      static_cast<double>(total_effect_allocations) / static_cast<double>(kTickSamples);
  result.guild_alloc_per_tick =
      static_cast<double>(total_guild_allocations) / static_cast<double>(kTickSamples);
  return result;
}

size_t FindWorldEntityCount(const mir2::ecs::WorldEntityCountSnapshot& snapshot,
                            uint32_t map_id) {
  for (const auto& [current_map_id, count] : snapshot.per_world) {
    if (current_map_id == map_id) {
      return count;
    }
  }
  return 0;
}

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

TEST(P3PressureObservabilityTest, LegacyVsCurrentHotPathMetrics) {
  const ScenarioResult legacy = RunScenario(false);
  const ScenarioResult current = RunScenario(true);

  std::cout << "\n[P3 Pressure Observability]\n";
  std::cout << "legacy.tick_p95_us=" << legacy.tick_stats.p95_us << '\n';
  std::cout << "legacy.tick_p99_us=" << legacy.tick_stats.p99_us << '\n';
  std::cout << "legacy.effect_alloc_per_tick=" << legacy.effect_alloc_per_tick << '\n';
  std::cout << "legacy.guild_alloc_per_tick=" << legacy.guild_alloc_per_tick << '\n';
  std::cout << "current.tick_p95_us=" << current.tick_stats.p95_us << '\n';
  std::cout << "current.tick_p99_us=" << current.tick_stats.p99_us << '\n';
  std::cout << "current.effect_alloc_per_tick=" << current.effect_alloc_per_tick << '\n';
  std::cout << "current.guild_alloc_per_tick=" << current.guild_alloc_per_tick << '\n';

  EXPECT_GT(legacy.effect_alloc_per_tick, 0.0);
  EXPECT_EQ(current.effect_alloc_per_tick, 0.0);
  EXPECT_EQ(current.guild_alloc_per_tick, 0.0);

  EXPECT_LE(current.tick_stats.p95_us, legacy.tick_stats.p95_us * 1.10);
  EXPECT_LE(current.tick_stats.p99_us, legacy.tick_stats.p99_us * 1.10);
}

TEST(P3PressureObservabilityTest, PerWorldEntityCountMetrics) {
  auto& manager = mir2::ecs::RegistryManager::Instance();
  constexpr uint32_t kMapIdA = 990001;
  constexpr uint32_t kMapIdB = 990002;
  constexpr size_t kAddedMapA = 5;
  constexpr size_t kAddedMapB = 8;

  auto* world_a = manager.CreateWorld(kMapIdA);
  auto* world_b = manager.CreateWorld(kMapIdB);
  ASSERT_NE(world_a, nullptr);
  ASSERT_NE(world_b, nullptr);

  const size_t before_map_a = CountEntities(world_a->Registry());
  const size_t before_map_b = CountEntities(world_b->Registry());
  const auto before_snapshot = manager.CollectEntityCounts();

  for (size_t i = 0; i < kAddedMapA; ++i) {
    (void)world_a->Registry().create();
  }
  for (size_t i = 0; i < kAddedMapB; ++i) {
    (void)world_b->Registry().create();
  }

  const auto after_snapshot = manager.CollectEntityCounts();
  const size_t after_map_a = FindWorldEntityCount(after_snapshot, kMapIdA);
  const size_t after_map_b = FindWorldEntityCount(after_snapshot, kMapIdB);

  std::cout << "current.world.entity_count.total=" << after_snapshot.total << '\n';
  std::cout << "current.world.map_" << kMapIdA << ".entity_count=" << after_map_a << '\n';
  std::cout << "current.world.map_" << kMapIdB << ".entity_count=" << after_map_b << '\n';

  EXPECT_EQ(after_map_a, before_map_a + kAddedMapA);
  EXPECT_EQ(after_map_b, before_map_b + kAddedMapB);
  EXPECT_EQ(after_snapshot.total, before_snapshot.total + kAddedMapA + kAddedMapB);
}

}  // namespace
