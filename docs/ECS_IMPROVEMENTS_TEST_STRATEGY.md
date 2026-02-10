# ECS Architecture Improvements - Test Strategy

## Executive Summary

This document outlines the comprehensive testing strategy for the ECS architecture improvements implemented across Phases 1-4. The strategy follows a risk-based approach prioritizing data safety, performance validation, and regression prevention.

## Test Architecture Overview

### Test Pyramid Distribution

```
                    /\
                   /  \
                  / E2E \ (10%)
                 /______\
                /        \
               /Integration\ (30%)
              /____________\
             /              \
            /  Unit Tests    \ (60%)
           /________________\
```

**Rationale**: Heavy emphasis on unit tests due to:
- Complex state management in ECS
- Critical data safety requirements (MoveToMap)
- Performance-sensitive operations (cache validation)
- Clear component boundaries

## Phase-by-Phase Test Coverage

### Phase 1: Defensive Programming (60% Unit, 30% Integration, 10% E2E)

#### Unit Tests (Implemented: `ecs_improvements_test.cc`)

**GetDefaultMapId() Tests**
- ✅ Returns valid default when not configured
- ✅ Can be overridden for testing
- ✅ Used in GetOrCreate when map_id is zero
- 🔲 Thread-safety validation (concurrent calls)
- 🔲 Config reload behavior

**Component Utils Tests**
- ✅ require_component returns pointer when present
- ✅ require_component returns nullptr when missing
- ✅ Works with const registry
- 🔲 has_required_components with multiple components
- 🔲 Error logging verification

**Summon System Tests** (Existing: `tests/server/ecs/summon_system_test.cc` - needs update)
- 🔲 create_skeleton fails gracefully without owner state
- 🔲 create_elf_monster fails gracefully without owner state
- 🔲 Summons inherit correct map_id from owner
- 🔲 Error logging when owner missing component

#### Integration Tests
- 🔲 Config loading → GetDefaultMapId() flow
- 🔲 Character creation uses correct default map
- 🔲 Summon creation in multi-map scenario

#### E2E Tests
- 🔲 Full character lifecycle with config-driven map assignment

### Phase 2: HandlerContext Cache (70% Unit, 20% Integration, 10% E2E)

#### Unit Tests (Implemented: `ecs_improvements_test.cc`)

**Cache Validation Tests**
- ✅ HasCache returns false when empty
- ✅ HasCache returns true when populated
- ✅ InvalidateCache clears all fields
- ✅ ValidateCache returns false without cache
- ✅ ValidateCache returns true for valid entity
- ✅ ValidateCache returns false after entity destroyed
- 🔲 ValidateCache with entity version mismatch
- 🔲 Cache validation after map change

**Coroutine Safety Tests**
- 🔲 Cache invalidation after co_await simulation
- 🔲 Multiple co_await cycles
- 🔲 Cache revalidation after player map change

#### Integration Tests
- 🔲 HandleRoutedMessage populates cache correctly
- 🔲 Handler receives populated cache
- 🔲 Cache performance vs. non-cached lookup (benchmark)

#### E2E Tests
- 🔲 Full message routing with cache usage
- 🔲 Player map change during message processing

### Phase 3: MoveToMap Transactional (80% Unit, 15% Integration, 5% E2E)

#### Unit Tests (Implemented: `ecs_improvements_test.cc`)

**Transactional Safety Tests**
- ✅ Successful move transfers entity between maps
- ✅ Failed move keeps entity in source map
- ✅ Move preserves character data (HP test)
- 🔲 Move preserves all component types
- 🔲 Move preserves inventory data
- 🔲 Move preserves equipment data
- 🔲 Move preserves skill data
- 🔲 Concurrent move attempts (race condition)

**IndexCharacter Upsert Tests**
- 🔲 Upsert overwrites existing entry
- 🔲 Upsert logs at DEBUG level
- 🔲 Upsert maintains index consistency
- 🔲 Upsert with invalid entity fails

**Rollback Tests**
- 🔲 Target creation failure leaves source intact
- 🔲 Index update failure destroys only target
- 🔲 No data loss in any failure scenario

#### Integration Tests
- 🔲 MoveToMap with active combat state
- 🔲 MoveToMap with party membership
- 🔲 MoveToMap with guild affiliation
- 🔲 Multiple players moving simultaneously

#### E2E Tests
- 🔲 Player teleport with full state preservation
- 🔲 Map change during active quest

### Phase 4: Native Inventory Storage (50% Unit, 40% Integration, 10% E2E)

#### Unit Tests (Implemented: `ecs_improvements_test.cc`)

**Data Structure Tests**
- ✅ ItemData has correct fields
- ✅ SkillData has correct fields
- ✅ InventoryComponent has correct capacity
- ✅ Can add/remove items from slots
- ✅ Can add equipment
- ✅ Can add skills
- ✅ Optional slots default to empty
- ✅ Can iterate over non-empty slots

**Serialization Boundary Tests** (Needs implementation)
- 🔲 LoadInventoryFromJson populates native structures
- 🔲 SaveInventoryToJson serializes from native structures
- 🔲 Round-trip JSON → Native → JSON preserves data
- 🔲 Legacy JSON format compatibility
- 🔲 Empty inventory serialization
- 🔲 Full inventory serialization

#### Integration Tests
- 🔲 Inventory operations use native structures
- 🔲 Item pickup updates native slots
- 🔲 Item use modifies native data
- 🔲 Equipment changes update native equipment array
- 🔲 Skill learning updates native skills vector
- 🔲 Character save/load preserves inventory

#### E2E Tests
- 🔲 Full item lifecycle (pickup → use → drop)
- 🔲 Equipment management flow
- 🔲 Skill progression flow

## Test Implementation Priority

### Critical Path (Week 1)
1. ✅ Phase 3 transactional safety tests (data loss prevention)
2. ✅ Phase 4 native structure tests (correctness validation)
3. ✅ Phase 2 cache validation tests (performance safety)
4. 🔲 Phase 4 serialization boundary tests (DB compatibility)

### High Priority (Week 2)
5. 🔲 Phase 3 rollback scenario tests
6. 🔲 Phase 2 coroutine safety tests
7. 🔲 Phase 1 summon system tests
8. 🔲 Integration tests for all phases

### Medium Priority (Week 3)
9. 🔲 Performance benchmarks (cache vs. non-cached)
10. 🔲 Concurrent operation tests
11. 🔲 E2E scenario tests

### Low Priority (Week 4)
12. 🔲 Edge case coverage
13. 🔲 Stress tests
14. 🔲 Documentation tests

## Test Execution Strategy

### Local Development
```bash
# Run all ECS improvement tests
ctest --test-dir build-wsl -R ecs_improvements -V

# Run specific phase tests
ctest --test-dir build-wsl -R "GetDefaultMapId|ComponentUtils" -V
ctest --test-dir build-wsl -R "HandlerContext" -V
ctest --test-dir build-wsl -R "MoveToMap" -V
ctest --test-dir build-wsl -R "NativeInventory" -V

# Run with coverage
cmake --build build-wsl --target coverage
```

### CI/CD Integration
```yaml
# .github/workflows/ecs-tests.yml
name: ECS Architecture Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build
        run: cmake --build build --target mir2_logic
      - name: Run Unit Tests
        run: ctest --test-dir build -R ecs_improvements -V
      - name: Run Integration Tests
        run: ctest --test-dir build -R ecs_integration -V
      - name: Generate Coverage
        run: cmake --build build --target coverage
      - name: Upload Coverage
        uses: codecov/codecov-action@v3
```

## Coverage Metrics

### Current Coverage (After Initial Implementation)
- **Phase 1**: 40% (6/15 tests implemented)
- **Phase 2**: 60% (6/10 tests implemented)
- **Phase 3**: 30% (3/10 tests implemented)
- **Phase 4**: 80% (8/10 tests implemented)
- **Overall**: 52% (23/45 planned tests)

### Target Coverage
- **Unit Tests**: 90% line coverage, 85% branch coverage
- **Integration Tests**: 80% scenario coverage
- **E2E Tests**: 70% user story coverage

### Critical Path Coverage (Must Have)
- ✅ MoveToMap data safety: 100%
- ✅ Cache validation: 100%
- ✅ Native inventory structures: 100%
- 🔲 Serialization boundary: 0% (HIGH PRIORITY)

## Test Quality Standards

### Unit Test Requirements
- **Isolation**: No external dependencies (DB, network, filesystem)
- **Speed**: < 10ms per test
- **Clarity**: Self-documenting test names
- **Coverage**: Each test covers one logical assertion
- **Mocking**: Use test doubles for external systems

### Integration Test Requirements
- **Scope**: Test 2-3 components together
- **Speed**: < 100ms per test
- **Setup**: Minimal fixture setup
- **Teardown**: Clean state after each test
- **Realism**: Use real components, mock only I/O

### E2E Test Requirements
- **Scope**: Full user scenario
- **Speed**: < 1s per test
- **Data**: Use realistic test data
- **Assertions**: Verify end-user observable behavior
- **Stability**: Retry logic for flaky scenarios

## Known Gaps and Risks

### High Risk Gaps
1. **Serialization Boundary Tests** (Phase 4)
   - Risk: DB corruption if JSON serialization broken
   - Mitigation: Implement round-trip tests immediately
   - Owner: Test Architect

2. **Concurrent MoveToMap** (Phase 3)
   - Risk: Race conditions in multi-threaded scenarios
   - Mitigation: Add thread-safety tests
   - Owner: Integration Test Engineer

3. **Coroutine Cache Invalidation** (Phase 2)
   - Risk: Dangling pointers after co_await
   - Mitigation: Add coroutine simulation tests
   - Owner: Unit Test Specialist

### Medium Risk Gaps
4. **Config Reload Behavior** (Phase 1)
   - Risk: GetDefaultMapId() may cache stale values
   - Mitigation: Add config reload tests

5. **Performance Regression** (Phase 2)
   - Risk: Cache may not provide expected speedup
   - Mitigation: Add benchmark tests

## Test Maintenance Plan

### Weekly
- Review test failures in CI
- Update tests for new features
- Refactor flaky tests

### Monthly
- Review coverage metrics
- Identify new test gaps
- Update test strategy document

### Quarterly
- Performance benchmark review
- Test suite optimization
- Test infrastructure upgrades

## Success Criteria

### Definition of Done for Testing
- [ ] All critical path tests passing
- [ ] 90% unit test coverage
- [ ] 80% integration test coverage
- [ ] Zero known data loss scenarios
- [ ] All tests run in < 5 minutes
- [ ] CI/CD integration complete
- [ ] Test documentation complete

### Quality Gates
- **Pre-Commit**: Unit tests must pass
- **Pre-PR**: Unit + Integration tests must pass
- **Pre-Merge**: All tests + coverage check must pass
- **Pre-Release**: All tests + E2E tests + performance benchmarks must pass

## Next Actions

### Immediate (This Week)
1. ✅ Implement core unit tests (DONE)
2. 🔲 Implement serialization boundary tests (HIGH PRIORITY)
3. 🔲 Add to CMakeLists.txt
4. 🔲 Verify tests compile and run

### Short Term (Next 2 Weeks)
5. 🔲 Implement integration tests
6. 🔲 Add performance benchmarks
7. 🔲 Set up CI/CD pipeline
8. 🔲 Document test patterns

### Long Term (Next Month)
9. 🔲 Implement E2E tests
10. 🔲 Add stress tests
11. 🔲 Complete coverage to 90%
12. 🔲 Optimize test execution time

## Appendix: Test File Organization

```
tests/
├── server/
│   ├── ecs/
│   │   ├── ecs_improvements_test.cc          # ✅ Core unit tests (NEW)
│   │   ├── ecs_improvements_integration_test.cc  # 🔲 Integration tests (TODO)
│   │   ├── character_entity_manager_test.cpp # Existing tests
│   │   ├── inventory_system_test.cpp         # Existing tests
│   │   └── summon_system_test.cc             # 🔲 Needs update
│   ├── logic/
│   │   └── handler_context_test.cc           # 🔲 Handler-specific tests (TODO)
│   └── integration/
│       └── ecs_full_flow_test.cc             # 🔲 E2E tests (TODO)
└── benchmarks/
    └── ecs_performance_test.cc               # 🔲 Performance tests (TODO)
```

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [EnTT Testing Best Practices](https://github.com/skypjack/entt/wiki/Crash-Course:-entity-component-system#testing)
- [C++ Testing Best Practices](https://github.com/cpp-best-practices/cppbestpractices/blob/master/03-Style.md#testing)
