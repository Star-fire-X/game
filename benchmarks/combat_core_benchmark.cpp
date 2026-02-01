#include <benchmark/benchmark.h>

#include <chrono>

#include "server/combat/combat_core.h"

namespace {

constexpr double k_damage_target_ns = 100.0;
constexpr double k_range_target_ns = 50.0;

using clock_type = std::chrono::steady_clock;

template <typename Fn>
void run_benchmark(benchmark::State& state, Fn&& fn, double target_ns) {
    const auto start = clock_type::now();
    for (auto _ : state) {
        auto result = fn();
        benchmark::DoNotOptimize(result);
    }
    const auto end = clock_type::now();

    const std::chrono::duration<double> elapsed = end - start;
    const double iterations = static_cast<double>(state.iterations());
    if (iterations > 0.0) {
        const double avg_ns = (elapsed.count() / iterations) * 1e9;
        state.SetLabel(avg_ns <= target_ns ? "PASS" : "FAIL");
    }
}

}  // namespace

static void BM_DamageCalculator_Calculate(benchmark::State& state) {
    legend2::CombatConfig config;
    legend2::combat::CombatRandom random(123);

    legend2::combat::DamageInput input;
    input.attack = 100;
    input.defense = 25;
    input.critical_chance = 0.0f;
    input.miss_chance = 0.0f;

    run_benchmark(state, [&]() {
        const int raw_damage = input.attack - input.defense;
        const auto rolls = random.roll_damage(raw_damage, config);
        return legend2::combat::DamageCalculator::calculate(input, config, rolls);
    }, k_damage_target_ns);
}

static void BM_DamageCalculator_WithCritical(benchmark::State& state) {
    legend2::CombatConfig config;
    legend2::combat::CombatRandom random(456);

    legend2::combat::DamageInput input;
    input.attack = 120;
    input.defense = 30;
    input.critical_chance = 1.0f;
    input.miss_chance = 0.0f;

    run_benchmark(state, [&]() {
        const int raw_damage = input.attack - input.defense;
        const auto rolls = random.roll_damage(raw_damage, config);
        return legend2::combat::DamageCalculator::calculate(input, config, rolls);
    }, k_damage_target_ns);
}

static void BM_DamageCalculator_WithMiss(benchmark::State& state) {
    legend2::CombatConfig config;
    legend2::combat::CombatRandom random(789);

    legend2::combat::DamageInput input;
    input.attack = 90;
    input.defense = 40;
    input.critical_chance = 0.0f;
    input.miss_chance = 1.0f;

    run_benchmark(state, [&]() {
        const int raw_damage = input.attack - input.defense;
        const auto rolls = random.roll_damage(raw_damage, config);
        return legend2::combat::DamageCalculator::calculate(input, config, rolls);
    }, k_damage_target_ns);
}

static void BM_RangeChecker_InRange(benchmark::State& state) {
    const legend2::Position attacker{10, 10};
    const legend2::Position target{11, 10};
    const int range = 2;

    run_benchmark(state, [&]() {
        return legend2::combat::RangeChecker::is_in_range(attacker, target, range);
    }, k_range_target_ns);
}

static void BM_RangeChecker_OutOfRange(benchmark::State& state) {
    const legend2::Position attacker{0, 0};
    const legend2::Position target{5, 5};
    const int range = 3;

    run_benchmark(state, [&]() {
        return legend2::combat::RangeChecker::is_in_range(attacker, target, range);
    }, k_range_target_ns);
}

static void BM_RangeChecker_Boundary(benchmark::State& state) {
    const legend2::Position attacker{0, 0};
    const legend2::Position target{3, 4};
    const int range = 5;

    run_benchmark(state, [&]() {
        return legend2::combat::RangeChecker::is_in_range(attacker, target, range);
    }, k_range_target_ns);
}

BENCHMARK(BM_DamageCalculator_Calculate)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_DamageCalculator_WithCritical)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_DamageCalculator_WithMiss)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RangeChecker_InRange)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RangeChecker_OutOfRange)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RangeChecker_Boundary)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
