# Map System Module - Revised Final Code Review Report

## 1. Classification Summary

| Classification | Count | Notes |
|---|---:|---|
| Confirmed | 7 | Core conclusions verified in current codebase |
| Partially Valid | 7 | Problem direction is right, but scope/severity/cause needs correction |
| Not Valid | 0 | No original item is fully invalid |

Scope baseline: original 14 findings (`2.1` - `2.14`).

Additional correction: 3 sub-claims in original text are invalid (see Section 4).

---

## 2. Confirmed

### 2.1 `[P0]` MapInstance staged locking introduces intermediate inconsistent state

Confirmed. `MapInstance::AddEntity` / `RemoveEntity` / `UpdateEntityPosition` release and reacquire `mutex_` around AOI calls, leaving observable intermediate windows.

Evidence:
- `src/server/game/map/map_instance.cc:263`
- `src/server/game/map/map_instance.cc:282`
- `src/server/game/map/map_instance.cc:285`
- `src/server/game/map/map_instance.cc:295`
- `src/server/game/map/map_instance.cc:322`

### 2.4 `[P1]` ChunkManager is placeholder and not integrated

Confirmed. Implementation remains framework-level TODO, and no runtime integration call sites were found.

Evidence:
- `src/server/game/map/chunk_manager.h:3`
- `src/server/game/map/chunk_manager.cc:16`
- `src/server/game/map/chunk_manager.cc:23`

### 2.7 `[P2]` Duplicate walkability storage (dual source of truth)

Confirmed. Both `walkability_` and `tile_data_->walkable` exist and must be synchronized manually.

Evidence:
- `src/server/game/map/map_instance.h:383`
- `src/server/game/map/map_instance.h:384`
- `src/server/game/map/map_instance.cc:103`
- `src/server/game/map/map_instance.cc:105`

### 2.9 `[P2]` Client MapSystem has duplicated API surface

Confirmed. snake_case and PascalCase wrappers are both exposed broadly.

Evidence:
- `src/client/game/map/map_system.h:125`
- `src/client/game/map/map_system.h:126`
- `src/client/game/map/map_system.h:133`
- `src/client/game/map/map_system.h:134`

### 2.10 `[P2]` GateManager keeps both string and numeric map-index paths

Confirmed. Parallel index structures are maintained for identical trigger semantics.

Evidence:
- `src/server/game/map/gate_manager.h:38`
- `src/server/game/map/gate_manager.h:45`
- `src/server/game/map/gate_manager.h:95`
- `src/server/game/map/gate_manager.h:97`

### 2.11 `[P2]` ScrollTeleport dungeon random point loads map file from disk per use

Confirmed. `UseDungeonScroll` resolves walkability via file loading path (`MapLoader`) on each call.

Evidence:
- `src/server/game/map/scroll_teleport.cc:61`
- `src/server/game/map/scroll_teleport.cc:160`

### 2.13 `[P3]` MapLoader format detection uses hardcoded magic constants without version mapping docs

Confirmed.

Evidence:
- `src/server/game/map/map_loader.cc:28`
- `src/server/game/map/map_loader.cc:56`

---

## 3. Partially Valid

### 2.2 `[P0->P1]` SceneManager deadlock risk while holding manager lock into map mutation

Partially valid. Locking pattern is risky (`SceneManager` lock held while invoking map mutation path), but deterministic reentry deadlock chain is not directly proven from currently wired callbacks.

Evidence:
- `src/server/game/map/scene_manager.cc:228`
- `src/server/game/map/scene_manager.cc:246`
- `src/server/game/map/map_instance.cc:282`

### 2.3 `[P1->P2]` MovementSystem::Update no-op

Partially valid. The no-op itself is true; the impact claim "server trusts client movement" is not true for current logic flow.

Movement validation actually exists in handler path:
- `src/server/logic/handlers/movement/movement_handler.cc:241`
- `src/server/logic/handlers/movement/movement_handler.cc:244`
- `src/server/handlers/movement/movement_validator.cc:47`
- `src/server/handlers/movement/movement_validator.cc:85`

### 2.5 `[P1->P2]` PathfindingHelper no-checker overload ignores obstacles

Partially valid. Implementation is obstacle-agnostic, but current codebase does not show active server-side call sites using that overload.

Evidence:
- `src/server/ecs/systems/pathfinding_helper.cc:36`
- `src/server/ecs/systems/pathfinding_helper.cc:41`

### 2.6 `[P1->P2]` MapEventManager uses O(n) scans

Partially valid. Complexity concern is correct, but this manager appears minimally integrated in current runtime paths.

Evidence:
- `src/server/game/map/map_event_manager.cc:72`
- `src/server/game/map/map_event_manager.cc:103`

### 2.8 `[P2]` MapInstance lock granularity may cause contention

Partially valid. Hot-path shared locking is real, but no profiling evidence in current report to prove this as active bottleneck.

Evidence:
- `src/server/game/map/map_instance.cc:62`
- `src/server/game/map/map_instance.cc:142`
- `src/server/handlers/movement/movement_validator.cc:85`

### 2.12 `[P3]` safe_zone_index_ reuses AOIManager (potentially heavyweight)

Partially valid. Could be overdesigned for small safe-zone counts, but cost/benefit depends on actual zone counts and query frequency.

Evidence:
- `src/server/game/map/map_instance.cc:433`
- `src/server/game/map/map_instance.cc:435`

### 2.14 `[P2]` test coverage gaps

Partially valid. Some concurrency-edge coverage remains weak, but several claimed "missing tests" already exist in repository.

Existing tests:
- `tests/server/map/scene_manager_reload_test.cpp:12`
- `tests/server/map/area_event_processor_test.cpp:50`
- `tests/server/map/area_event_processor_test.cpp:230`
- `tests/server/map/cross_server_teleport_test.cpp:11`

---

## 4. Not Valid (Sub-Claims)

These are not full-item invalidations; they are invalid statements within original findings.

1. "Server currently trusts client-reported positions without validation."  
Not valid for current flow. Validation and anti-cheat checks are present before state update.

2. "SceneManager::ReloadMap with entities has no test."  
Not valid. Test exists.

3. "AreaEventProcessor tick dispatch / CrossServerTeleport have no tests."  
Not valid. Both test files exist and are runnable.

---

## 5. Execution Evidence

Executed on `2026-02-09`:

```bash
./build-wsl/bin/legend2_tests --gtest_filter='SceneManagerReloadTest.*:CrossServerTeleportTest.*:MapInstanceWalkTest.*:AreaEventProcessorTest.*:ScrollTeleportTest.*:MapEventManagerTest.*'
```

Result:
- 19 tests
- 6 test suites
- all passed

