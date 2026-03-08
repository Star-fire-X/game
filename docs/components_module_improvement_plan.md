# Components Module Improvement Plan

Date: 2026-02-11
Scope: `src/server/ecs/components/` (20 headers) + `src/server/ecs/dirty_tracker.h` + `src/server/ecs/components/attribute_component.h` + `src/server/ecs/components/transform_component.h`.

## 1) Goals
- Reduce schema drift between runtime entities and inventory persistence.
- Improve safety/robustness of hot-path component helpers.
- Reduce avoidable allocations and linear scans in frequently-used paths.
- Provide a clean deprecation path for compatibility aliases and legacy flags.

## 2) Confirmed Findings (from code)
- Duplicate item representations (`ItemData` vs `ItemComponent`) with divergent fields.
- Duplicate skill representations (`SkillData`, `SkillComponent`, `LearnedSkill`) with inconsistent types.
- `DirtyComponent::inventory_dirty` is always set alongside finer-grained flags in `dirty_tracker` helpers.
- `StorageComponent` slots default-initialize to zero, unlike other slot components that explicitly fill `entt::null`.
- `MonsterAggroComponent::AddHatred` lacks bounds checking; large damage can overflow `int32_t`.
- `TradeComponent::offered_gold` is signed; trade system rejects negatives but the type invites misuse.
- Linear membership checks in `GuildComponent::IsMember` and `PartyComponent::IsMember`.
- `EffectListComponent::get_effects_by_category` allocates a vector on every call.
- PK component naming typo (`pk_hiter_list`, `add_hiter`, `cleanup_expired_hiters`).
- Compatibility alias headers have no deprecation markers.

## 3) Improvement Plan (Phased)

### P0 (Safety + Correctness, Low Risk)
1) Initialize `StorageComponent::slots` with `entt::null`.
   - Aligns behavior with equipment slots; avoids reliance on zero == null.
   - Effort: Low. Risk: Low. Impact: Prevents sentinel mismatch bugs.

2) Add bounds and non-positive guards in `MonsterAggroComponent::AddHatred`.
   - Reject `damage <= 0`, clamp `hatred` and total hatred to a max value.
   - Effort: Low. Risk: Low. Impact: Eliminates overflow paths.

3) Add explicit input validation in `TradeComponent` usage.
   - Keep `int` for now, but ensure all entry points validate `>= 0` and upper bounds.
   - Effort: Low. Risk: Low. Impact: Prevents invalid offers if new call sites appear.

### P1 (Schema Unification, Medium Risk)
4) Unify item instance schema.
   - Option A: Move all fields into a single `ItemData` (in `character_components.h`) and `using ItemComponent = ItemData`.
   - Option B: Keep `ItemComponent` as canonical, and use it in inventory persistence with `std::optional<ItemComponent>`.
   - Update migration/conversion in `inventory_migration.cc` accordingly.
   - Effort: Medium. Risk: Medium (serialization/migration). Impact: Removes drift bugs.

5) Consolidate skill representations.
   - Choose a single learned-skill struct (e.g., rename `LearnedSkill` to `SkillData` or vice versa) with consistent type sizes.
   - Ensure `SkillComponent` (entity component) either reuses the same struct or is removed if redundant.
   - Effort: Medium. Risk: Medium. Impact: Reduced conversion complexity.

6) Deprecate compatibility aliases.
   - Add `[[deprecated]]` to `AttributeComponent` and `TransformComponent` typedefs.
   - Effort: Low. Risk: Low. Impact: Guides migration.

### P2 (Performance + Maintainability)
7) Replace membership linear scans with `unordered_set` caches.
   - Keep `std::vector` for stable iteration, add `member_set` for O(1) lookup.
   - Ensure updates on add/remove member operations in systems.
   - Effort: Low-Medium. Risk: Medium (consistency). Impact: Hot-path lookup improvement.

8) Replace `get_effects_by_category` vector-return with callback iteration.
   - Provide `for_each_effect(category, fn)` or `span`-like view.
   - Effort: Low. Risk: Low. Impact: Removes per-call allocation.

9) Consider `SkillCooldownComponent::cleanup_expired` shrink policy.
   - Optional: call `rehash()` after large deletions or track high-water mark.
   - Effort: Low. Risk: Low. Impact: Memory stability in long sessions.

### P3 (Design + SRP Cleanup)
10) Split `CharacterAttributesComponent` into combat vs economy vs meta.
    - Example: `CombatAttributesComponent`, `EconomyComponent` (gold), `LifecycleComponent` (pk/life attrib).
    - Effort: Medium. Risk: Medium-High (touches many systems). Impact: Better cache locality.

11) Rename PK “hiter” typos.
    - Use a migration-compatible approach (keep old names during transition or macro alias).
    - Effort: Low. Risk: Low. Impact: Readability.

## 4) Optimization Proposals
- Item/skill schema unification (P1) reduces conversions and branching in inventory and persistence layers.
- `GuildComponent`/`PartyComponent` membership caching (P2) removes repeated linear scans in message routing and combat checks.
- Effect iteration via callback (P2) removes heap allocation in tick loops.
- Optional memory hygiene: `SkillCooldownComponent` rehash when size drops significantly.

## 5) Testing & Validation
- Unit tests for item/skill conversion consistency (inventory migration).
- New regression tests:
  - Aggro overflow clamps to max.
  - Storage slots initialize to `entt::null`.
  - Membership cache remains consistent with vector order.
- Performance microbench:
  - Effect iteration (before/after allocation).
  - Guild membership lookup.

## 6) Rollout & Compatibility
- Introduce schema changes behind a feature flag or versioned migration.
- Add temporary adapters for old persistence formats if required.
- Deprecate alias headers with compiler warnings; remove after one release cycle.

## 7) Open Decisions
- Canonical item schema location: `ItemData` (inventory) vs `ItemComponent` (entity).
- Skill representation: keep `SkillComponent` entity-based or use `LearnedSkill` only.
- Gold type: move to `int64_t`/`uint64_t` now or enforce caps and migrate later.
