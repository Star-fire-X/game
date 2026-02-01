#!/usr/bin/env python3

import json
import os
import re
import sys

GREEN = "\033[32m"
RED = "\033[31m"
RESET = "\033[0m"

THRESHOLDS_NS = [
    (re.compile(r"^BM_DamageCalculator_"), 100.0),
    (re.compile(r"^BM_RangeChecker_"), 50.0),
]

UNIT_TO_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}


def usage() -> None:
    print("Usage: python scripts/check_benchmark.py <benchmark_results.json>")


def to_ns(value: float, unit: str) -> float:
    unit_key = unit.lower()
    if unit_key not in UNIT_TO_NS:
        raise ValueError(f"Unsupported time unit: {unit}")
    return value * UNIT_TO_NS[unit_key]


def find_threshold(name: str) -> float | None:
    for pattern, threshold in THRESHOLDS_NS:
        if pattern.match(name):
            return threshold
    return None


def main() -> int:
    if len(sys.argv) != 2:
        usage()
        return 1

    path = sys.argv[1]
    if not os.path.exists(path):
        print(f"Benchmark JSON not found: {path}")
        return 1

    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)

    benchmarks = data.get("benchmarks", [])
    if not benchmarks:
        print("No benchmarks found in JSON output.")
        return 1

    results = []
    for entry in benchmarks:
        if entry.get("aggregate_name"):
            continue
        if entry.get("run_type") and entry.get("run_type") != "iteration":
            continue

        name = entry.get("name")
        if not name:
            continue

        threshold = find_threshold(name)
        if threshold is None:
            continue

        cpu_time = entry.get("cpu_time")
        time_unit = entry.get("time_unit", "ns")
        if cpu_time is None:
            print(f"Missing cpu_time for benchmark: {name}")
            return 1

        try:
            cpu_ns = to_ns(float(cpu_time), time_unit)
        except ValueError as exc:
            print(f"{name}: {exc}")
            return 1

        passed = cpu_ns <= threshold
        results.append((name, cpu_ns, threshold, passed))

    if not results:
        print("No benchmarks matched configured thresholds.")
        return 1

    name_width = max(len(name) for name, _, _, _ in results)

    print("Benchmark Performance Report")
    print("=" * 28)

    passed_count = 0
    for name, cpu_ns, threshold, passed in results:
        status = "PASS" if passed else "FAIL"
        color = GREEN if passed else RED
        if passed:
            passed_count += 1
        print(
            f"{name.ljust(name_width)}  "
            f"{cpu_ns:8.1f} ns  "
            f"[ {color}{status}{RESET} ] "
            f"(threshold: {threshold:g} ns)"
        )

    total = len(results)
    print(f"Summary: {passed_count}/{total} passed")

    return 0 if passed_count == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
