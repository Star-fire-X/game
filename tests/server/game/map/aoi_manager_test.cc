/**
 * @file aoi_manager_test.cc
 * @brief AOIManager comprehensive unit tests
 *
 * Tests the 9-grid Area of Interest management system including entity
 * enter/leave/move, view queries, callback event verification, edge cases,
 * grid boundary behavior, and concurrent access safety.
 *
 * Priority: P0 Critical (Score: 44)
 */

#include <gtest/gtest.h>

#include "game/map/aoi_manager.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace mir2::game::map {
namespace {

// ---------------------------------------------------------------------------
// Helper: captured AOI event for verification
// ---------------------------------------------------------------------------
struct CapturedEvent {
  AOIEventType type;
  uint64_t watcher_id;
  uint64_t target_id;
  int32_t x;
  int32_t y;
};

// Thread-safe event recorder used across most tests.
class EventRecorder {
 public:
  void Record(AOIEventType type, uint64_t watcher_id, uint64_t target_id,
              int32_t x, int32_t y) {
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back({type, watcher_id, target_id, x, y});
  }

  AOIManager::AOICallback MakeCallback() {
    return [this](AOIEventType type, uint64_t watcher_id, uint64_t target_id,
                  int32_t x, int32_t y) {
      Record(type, watcher_id, target_id, x, y);
    };
  }

  std::vector<CapturedEvent> Events() const {
    std::lock_guard<std::mutex> lock(mu_);
    return events_;
  }

  size_t EventCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return events_.size();
  }

  // Count events matching the given type.
  size_t CountByType(AOIEventType type) const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<size_t>(std::count_if(
        events_.begin(), events_.end(),
        [type](const CapturedEvent& e) { return e.type == type; }));
  }

  // Count events matching type and watcher.
  size_t CountByTypeAndWatcher(AOIEventType type, uint64_t watcher) const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<size_t>(std::count_if(
        events_.begin(), events_.end(), [type, watcher](const CapturedEvent& e) {
          return e.type == type && e.watcher_id == watcher;
        }));
  }

  // Check whether at least one event matches the full signature.
  bool HasEvent(AOIEventType type, uint64_t watcher_id, uint64_t target_id,
                int32_t x, int32_t y) const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::any_of(events_.begin(), events_.end(),
                       [&](const CapturedEvent& e) {
                         return e.type == type &&
                                e.watcher_id == watcher_id &&
                                e.target_id == target_id && e.x == x &&
                                e.y == y;
                       });
  }

  // Check whether at least one event matches type/watcher/target (ignoring
  // coordinates).
  bool HasEvent(AOIEventType type, uint64_t watcher_id,
                uint64_t target_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::any_of(events_.begin(), events_.end(),
                       [&](const CapturedEvent& e) {
                         return e.type == type &&
                                e.watcher_id == watcher_id &&
                                e.target_id == target_id;
                       });
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    events_.clear();
  }

 private:
  mutable std::mutex mu_;
  std::vector<CapturedEvent> events_;
};

// Helper to check if a vector contains a value.
template <typename T>
bool Contains(const std::vector<T>& vec, const T& value) {
  return std::find(vec.begin(), vec.end(), value) != vec.end();
}

// ---------------------------------------------------------------------------
// Test Fixture
// ---------------------------------------------------------------------------
class AOIManagerTest : public ::testing::Test {
 protected:
  // Default map: 200x200, grid_size=20 -> 10x10 grids
  static constexpr int32_t kMapWidth = 200;
  static constexpr int32_t kMapHeight = 200;
  static constexpr int32_t kGridSize = 20;

  void SetUp() override {
    aoi_ = std::make_unique<AOIManager>(kMapWidth, kMapHeight, kGridSize);
    aoi_->SetCallback(recorder_.MakeCallback());
  }

  void TearDown() override { aoi_.reset(); }

  std::unique_ptr<AOIManager> aoi_;
  EventRecorder recorder_;
};

// ===================================================================
// 1. Basic enter / leave / count
// ===================================================================

TEST_F(AOIManagerTest, EmptyManagerHasZeroEntities) {
  EXPECT_EQ(aoi_->EntityCount(), 0u);
}

TEST_F(AOIManagerTest, EnterIncreasesEntityCount) {
  aoi_->Enter(1, 10, 10);
  EXPECT_EQ(aoi_->EntityCount(), 1u);

  aoi_->Enter(2, 50, 50);
  EXPECT_EQ(aoi_->EntityCount(), 2u);
}

TEST_F(AOIManagerTest, LeaveDecreasesEntityCount) {
  aoi_->Enter(1, 10, 10);
  aoi_->Enter(2, 50, 50);
  EXPECT_EQ(aoi_->EntityCount(), 2u);

  aoi_->Leave(1);
  EXPECT_EQ(aoi_->EntityCount(), 1u);

  aoi_->Leave(2);
  EXPECT_EQ(aoi_->EntityCount(), 0u);
}

TEST_F(AOIManagerTest, GetEntityPositionAfterEnter) {
  aoi_->Enter(42, 75, 130);

  int32_t out_x = -1, out_y = -1;
  EXPECT_TRUE(aoi_->GetEntityPosition(42, out_x, out_y));
  EXPECT_EQ(out_x, 75);
  EXPECT_EQ(out_y, 130);
}

TEST_F(AOIManagerTest, GetEntityPositionReturnsFalseForNonExistent) {
  int32_t out_x = -1, out_y = -1;
  EXPECT_FALSE(aoi_->GetEntityPosition(999, out_x, out_y));
}

// ===================================================================
// 2. GetEntitiesInView -- same and adjacent grids visible
// ===================================================================

TEST_F(AOIManagerTest, GetEntitiesInViewSameGrid) {
  // Both entities at (5, 5) -> grid (0, 0)
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 10);

  auto result = aoi_->GetEntitiesInView(5, 5);
  EXPECT_TRUE(Contains(result, uint64_t{1}));
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

TEST_F(AOIManagerTest, GetEntitiesInViewAdjacentGrid) {
  // Entity 1 at grid (0, 0), entity 2 at grid (1, 0) -- adjacent
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 25, 5);  // grid_size=20, so 25/20 = grid 1

  auto result = aoi_->GetEntitiesInView(5, 5);
  EXPECT_TRUE(Contains(result, uint64_t{1}));
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

TEST_F(AOIManagerTest, GetEntitiesInViewDiagonalGrid) {
  // Entity 1 at grid (0, 0), entity 2 at grid (1, 1) -- diagonal neighbor
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 25, 25);

  auto result = aoi_->GetEntitiesInView(5, 5);
  EXPECT_TRUE(Contains(result, uint64_t{1}));
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

TEST_F(AOIManagerTest, GetEntitiesInViewFarGridNotVisible) {
  // Entity 1 at grid (0, 0), entity 2 at grid (5, 5) -- far apart
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 105, 105);

  auto result = aoi_->GetEntitiesInView(5, 5);
  EXPECT_TRUE(Contains(result, uint64_t{1}));
  EXPECT_FALSE(Contains(result, uint64_t{2}));
}

// ===================================================================
// 3. GetEntitiesInViewOf -- excludes self
// ===================================================================

TEST_F(AOIManagerTest, GetEntitiesInViewOfExcludesSelf) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 10);

  auto result = aoi_->GetEntitiesInViewOf(1);
  EXPECT_FALSE(Contains(result, uint64_t{1}));
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

TEST_F(AOIManagerTest, GetEntitiesInViewOfReturnsEmptyForNonExistent) {
  auto result = aoi_->GetEntitiesInViewOf(999);
  EXPECT_TRUE(result.empty());
}

TEST_F(AOIManagerTest, GetEntitiesInViewOfSingleEntity) {
  aoi_->Enter(1, 5, 5);

  auto result = aoi_->GetEntitiesInViewOf(1);
  EXPECT_TRUE(result.empty());
}

TEST_F(AOIManagerTest, GetEntitiesInViewOfMultipleNeighbors) {
  // Place 4 entities in adjacent grids
  aoi_->Enter(1, 5, 5);    // grid (0,0)
  aoi_->Enter(2, 25, 5);   // grid (1,0)
  aoi_->Enter(3, 5, 25);   // grid (0,1)
  aoi_->Enter(4, 25, 25);  // grid (1,1)

  auto result = aoi_->GetEntitiesInViewOf(1);
  EXPECT_EQ(result.size(), 3u);
  EXPECT_TRUE(Contains(result, uint64_t{2}));
  EXPECT_TRUE(Contains(result, uint64_t{3}));
  EXPECT_TRUE(Contains(result, uint64_t{4}));
}

// ===================================================================
// 4. InViewOf -- adjacent grids true, far grids false
// ===================================================================

TEST_F(AOIManagerTest, InViewOfSameGrid) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 10);
  EXPECT_TRUE(aoi_->InViewOf(1, 2));
  EXPECT_TRUE(aoi_->InViewOf(2, 1));  // symmetric
}

TEST_F(AOIManagerTest, InViewOfAdjacentGrid) {
  aoi_->Enter(1, 5, 5);    // grid (0,0)
  aoi_->Enter(2, 25, 5);   // grid (1,0)
  EXPECT_TRUE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, InViewOfDiagonalGrid) {
  aoi_->Enter(1, 5, 5);    // grid (0,0)
  aoi_->Enter(2, 25, 25);  // grid (1,1)
  EXPECT_TRUE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, InViewOfFarGridsFalse) {
  aoi_->Enter(1, 5, 5);      // grid (0,0)
  aoi_->Enter(2, 45, 5);     // grid (2,0) -- distance 2 in x
  EXPECT_FALSE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, InViewOfTwoGridsApartDiagonal) {
  aoi_->Enter(1, 5, 5);      // grid (0,0)
  aoi_->Enter(2, 45, 45);    // grid (2,2) -- distance 2 in both axes
  EXPECT_FALSE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, InViewOfNonExistentEntityReturnsFalse) {
  aoi_->Enter(1, 5, 5);
  EXPECT_FALSE(aoi_->InViewOf(1, 999));
  EXPECT_FALSE(aoi_->InViewOf(999, 1));
  EXPECT_FALSE(aoi_->InViewOf(888, 999));
}

// ===================================================================
// 5. Move within same grid -- generates Move events
// ===================================================================

TEST_F(AOIManagerTest, MoveWithinSameGridGeneratesMoveEvents) {
  aoi_->Enter(1, 5, 5);    // grid (0,0)
  aoi_->Enter(2, 10, 10);  // grid (0,0) -- same grid
  recorder_.Clear();

  // Move entity 1 within grid (0,0): from (5,5) to (15,15)
  aoi_->Move(1, 15, 15);

  // Entity 2 should receive a Move event about entity 1
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kMove, 2, 1, 15, 15));

  // Entity 1 should NOT receive a move event about itself
  EXPECT_FALSE(recorder_.HasEvent(AOIEventType::kMove, 1, 1));

  // No Enter or Leave events should fire for same-grid move
  EXPECT_EQ(recorder_.CountByType(AOIEventType::kEnter), 0u);
  EXPECT_EQ(recorder_.CountByType(AOIEventType::kLeave), 0u);
}

TEST_F(AOIManagerTest, MoveUpdatesPosition) {
  aoi_->Enter(1, 5, 5);
  aoi_->Move(1, 15, 18);

  int32_t out_x = -1, out_y = -1;
  EXPECT_TRUE(aoi_->GetEntityPosition(1, out_x, out_y));
  EXPECT_EQ(out_x, 15);
  EXPECT_EQ(out_y, 18);
}

// ===================================================================
// 6. Move across grids -- generates Enter/Leave events
// ===================================================================

TEST_F(AOIManagerTest, MoveAcrossGridsGeneratesEnterLeaveEvents) {
  // Entity 1 at grid (0,0), entity 2 at grid (0,0), entity 3 at grid (5,0)
  // Entity 3 is far from entity 1 initially.
  aoi_->Enter(1, 5, 5);    // grid (0,0)
  aoi_->Enter(2, 10, 10);  // grid (0,0)
  aoi_->Enter(3, 105, 5);  // grid (5,0) -- far away
  recorder_.Clear();

  // Move entity 1 from grid (0,0) to grid (5,0), near entity 3
  aoi_->Move(1, 105, 5);

  // Entity 3 should receive Enter event about entity 1
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 3, 1, 105, 5));
  // Entity 1 should receive Enter event about entity 3
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 1, 3));

  // Entity 2 should receive Leave event about entity 1
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kLeave, 2, 1, 105, 5));
  // Entity 1 should receive Leave event about entity 2
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kLeave, 1, 2));
}

TEST_F(AOIManagerTest, MoveAcrossGridsUpdatesViewQueries) {
  aoi_->Enter(1, 5, 5);      // grid (0,0)
  aoi_->Enter(2, 105, 105);  // grid (5,5) -- far away

  EXPECT_FALSE(aoi_->InViewOf(1, 2));

  // Move entity 1 near entity 2
  aoi_->Move(1, 100, 100);  // grid (5,5)

  EXPECT_TRUE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, MoveAcrossGridsOverlappingGridsGetMoveEvent) {
  // Place two entities in adjacent grids
  aoi_->Enter(1, 5, 5);    // grid (0,0)
  aoi_->Enter(2, 25, 5);   // grid (1,0)
  recorder_.Clear();

  // Move entity 1 from grid (0,0) to grid (1,0). Entity 2 is in the
  // overlap region (grid (1,0) is adjacent to both old and new grids),
  // so entity 2 should receive a kMove event.
  aoi_->Move(1, 25, 5);

  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kMove, 2, 1, 25, 5));
}

// ===================================================================
// 7. Callback verification
// ===================================================================

TEST_F(AOIManagerTest, EnterCallbackFiringBidirectional) {
  // When entity 2 enters near entity 1, both should see each other.
  aoi_->Enter(1, 5, 5);
  recorder_.Clear();

  aoi_->Enter(2, 10, 10);

  // Entity 1 sees entity 2 enter
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 1, 2, 10, 10));
  // Entity 2 sees entity 1 (already present)
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 2, 1, 5, 5));
}

TEST_F(AOIManagerTest, LeaveCallbackFiringUnidirectional) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 10);
  recorder_.Clear();

  aoi_->Leave(2);

  // Entity 1 receives leave notification about entity 2
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kLeave, 1, 2));

  // The leaving entity (2) does NOT receive events about watchers leaving
  // its view -- that is the implementation behavior: Leave only notifies
  // watchers, not the leaving entity.
  EXPECT_EQ(recorder_.CountByTypeAndWatcher(AOIEventType::kLeave, 2), 0u);
}

TEST_F(AOIManagerTest, NoCallbackWhenCallbackNotSet) {
  // Create a fresh AOIManager without callback
  AOIManager aoi_no_cb(kMapWidth, kMapHeight, kGridSize);

  // These should not crash even without a callback
  aoi_no_cb.Enter(1, 5, 5);
  aoi_no_cb.Enter(2, 10, 10);
  aoi_no_cb.Move(1, 15, 15);
  aoi_no_cb.Leave(1);
  aoi_no_cb.Leave(2);
}

TEST_F(AOIManagerTest, CallbackReceivesCorrectCoordinates) {
  aoi_->Enter(1, 33, 44);
  recorder_.Clear();

  aoi_->Enter(2, 37, 48);

  // Watcher 1 sees target 2 at (37, 48)
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 1, 2, 37, 48));
  // Watcher 2 sees target 1 at (33, 44)
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 2, 1, 33, 44));
}

TEST_F(AOIManagerTest, MultipleEntitiesEnterSameGridAllNotified) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 5);
  recorder_.Clear();

  aoi_->Enter(3, 15, 5);

  // Entity 3 enters. Both entity 1 and entity 2 should see entity 3.
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 1, 3, 15, 5));
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 2, 3, 15, 5));

  // Entity 3 should see both entity 1 and entity 2.
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 3, 1));
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kEnter, 3, 2));
}

// ===================================================================
// 8. Edge cases
// ===================================================================

TEST_F(AOIManagerTest, EnterSameEntityTwiceIgnored) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(1, 50, 50);  // duplicate -- should be ignored

  EXPECT_EQ(aoi_->EntityCount(), 1u);

  // Position should remain at original (5, 5)
  int32_t out_x = -1, out_y = -1;
  EXPECT_TRUE(aoi_->GetEntityPosition(1, out_x, out_y));
  EXPECT_EQ(out_x, 5);
  EXPECT_EQ(out_y, 5);
}

TEST_F(AOIManagerTest, LeaveNonExistentEntityIsNoOp) {
  aoi_->Leave(999);
  EXPECT_EQ(aoi_->EntityCount(), 0u);
  EXPECT_EQ(recorder_.EventCount(), 0u);
}

TEST_F(AOIManagerTest, MoveNonExistentEntityIsNoOp) {
  aoi_->Move(999, 50, 50);
  EXPECT_EQ(aoi_->EntityCount(), 0u);
  EXPECT_EQ(recorder_.EventCount(), 0u);
}

TEST_F(AOIManagerTest, LeaveAfterLeaveIsNoOp) {
  aoi_->Enter(1, 5, 5);
  aoi_->Leave(1);
  recorder_.Clear();

  aoi_->Leave(1);
  EXPECT_EQ(recorder_.EventCount(), 0u);
  EXPECT_EQ(aoi_->EntityCount(), 0u);
}

TEST_F(AOIManagerTest, MoveAfterLeaveIsNoOp) {
  aoi_->Enter(1, 5, 5);
  aoi_->Leave(1);
  recorder_.Clear();

  aoi_->Move(1, 50, 50);
  EXPECT_EQ(recorder_.EventCount(), 0u);
}

TEST_F(AOIManagerTest, EntityIdZero) {
  // Entity ID 0 should work like any other ID.
  aoi_->Enter(0, 10, 10);
  EXPECT_EQ(aoi_->EntityCount(), 1u);

  int32_t out_x = -1, out_y = -1;
  EXPECT_TRUE(aoi_->GetEntityPosition(0, out_x, out_y));
  EXPECT_EQ(out_x, 10);
  EXPECT_EQ(out_y, 10);

  aoi_->Leave(0);
  EXPECT_EQ(aoi_->EntityCount(), 0u);
}

// ===================================================================
// 9. Clear empties everything
// ===================================================================

TEST_F(AOIManagerTest, ClearRemovesAllEntities) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 25, 25);
  aoi_->Enter(3, 50, 50);
  EXPECT_EQ(aoi_->EntityCount(), 3u);

  aoi_->Clear();
  EXPECT_EQ(aoi_->EntityCount(), 0u);

  // Queries should return empty
  auto result = aoi_->GetEntitiesInView(5, 5);
  EXPECT_TRUE(result.empty());

  int32_t out_x = -1, out_y = -1;
  EXPECT_FALSE(aoi_->GetEntityPosition(1, out_x, out_y));
  EXPECT_FALSE(aoi_->GetEntityPosition(2, out_x, out_y));
  EXPECT_FALSE(aoi_->GetEntityPosition(3, out_x, out_y));
}

TEST_F(AOIManagerTest, ClearThenReenter) {
  aoi_->Enter(1, 5, 5);
  aoi_->Clear();
  recorder_.Clear();

  aoi_->Enter(1, 50, 50);
  EXPECT_EQ(aoi_->EntityCount(), 1u);

  int32_t out_x = -1, out_y = -1;
  EXPECT_TRUE(aoi_->GetEntityPosition(1, out_x, out_y));
  EXPECT_EQ(out_x, 50);
  EXPECT_EQ(out_y, 50);
}

// ===================================================================
// 10. Grid boundary behavior
// ===================================================================

TEST_F(AOIManagerTest, EntitiesAtGridBoundaryAreInSameGrid) {
  // grid_size = 20. Positions 0..19 map to grid 0, 20..39 to grid 1.
  aoi_->Enter(1, 0, 0);    // grid (0,0)
  aoi_->Enter(2, 19, 19);  // grid (0,0) -- same grid

  EXPECT_TRUE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, EntitiesAtGridBoundaryAcrossGridsAdjacent) {
  // Position 19 -> grid 0, position 20 -> grid 1, adjacent grids.
  aoi_->Enter(1, 19, 19);  // grid (0,0)
  aoi_->Enter(2, 20, 20);  // grid (1,1) -- diagonally adjacent

  EXPECT_TRUE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, EntitiesAtExactGridEdge) {
  // Position at exact multiple of grid_size.
  aoi_->Enter(1, 20, 20);  // grid (1,1)
  aoi_->Enter(2, 40, 40);  // grid (2,2) -- diagonally adjacent to (1,1)

  EXPECT_TRUE(aoi_->InViewOf(1, 2));
}

TEST_F(AOIManagerTest, EntitiesAtMapCorners) {
  // Top-left corner grid (0,0) and entities at (0,0)
  aoi_->Enter(1, 0, 0);

  // Far corner: (199, 199) -> grid (9, 9)
  aoi_->Enter(2, 199, 199);

  EXPECT_FALSE(aoi_->InViewOf(1, 2));

  // But entities should each be queryable.
  auto view1 = aoi_->GetEntitiesInViewOf(1);
  EXPECT_FALSE(Contains(view1, uint64_t{2}));

  auto view2 = aoi_->GetEntitiesInViewOf(2);
  EXPECT_FALSE(Contains(view2, uint64_t{1}));
}

TEST_F(AOIManagerTest, EntityAtOriginGridBoundary) {
  // Entity at (0, 0) is in grid (0, 0). FillSurroundingGrids should
  // only produce grids with non-negative indices, so no crash or
  // out-of-bounds.
  aoi_->Enter(1, 0, 0);
  aoi_->Enter(2, 5, 5);

  auto result = aoi_->GetEntitiesInViewOf(1);
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

TEST_F(AOIManagerTest, EntityAtMaxBoundaryGrid) {
  // Entity near the far edge. grid_count_x = 10, so max grid index = 9.
  // Position 199 -> grid 9. Surrounding grids should clip at grid_count.
  aoi_->Enter(1, 199, 199);  // grid (9, 9)
  aoi_->Enter(2, 190, 190);  // grid (9, 9) -- same grid

  EXPECT_TRUE(aoi_->InViewOf(1, 2));

  auto result = aoi_->GetEntitiesInViewOf(1);
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

// ===================================================================
// 11. Large map -- entities far apart not visible
// ===================================================================

TEST_F(AOIManagerTest, LargeMapEntitiesFarApart) {
  AOIManager large_aoi(10000, 10000, 20);
  EventRecorder large_recorder;
  large_aoi.SetCallback(large_recorder.MakeCallback());

  large_aoi.Enter(1, 0, 0);
  large_aoi.Enter(2, 5000, 5000);
  large_aoi.Enter(3, 9999, 9999);

  EXPECT_FALSE(large_aoi.InViewOf(1, 2));
  EXPECT_FALSE(large_aoi.InViewOf(1, 3));
  EXPECT_FALSE(large_aoi.InViewOf(2, 3));

  // No enter events should have fired since all are far apart.
  EXPECT_EQ(large_recorder.CountByType(AOIEventType::kEnter), 0u);
}

TEST_F(AOIManagerTest, LargeMapAdjacentStillVisible) {
  AOIManager large_aoi(10000, 10000, 20);

  large_aoi.Enter(1, 4990, 4990);  // grid (249, 249)
  large_aoi.Enter(2, 5010, 5010);  // grid (250, 250) -- adjacent diagonal

  EXPECT_TRUE(large_aoi.InViewOf(1, 2));
}

TEST_F(AOIManagerTest, LargeMapManyEntitiesInSameGrid) {
  AOIManager large_aoi(10000, 10000, 20);

  // Place 100 entities in the same grid
  for (uint64_t i = 1; i <= 100; ++i) {
    large_aoi.Enter(i, 50, 50);
  }
  EXPECT_EQ(large_aoi.EntityCount(), 100u);

  auto result = large_aoi.GetEntitiesInViewOf(1);
  EXPECT_EQ(result.size(), 99u);  // excludes self
}

// ===================================================================
// 12. Concurrent access test with multiple threads
// ===================================================================

TEST_F(AOIManagerTest, ConcurrentEnterLeave) {
  // Use a separate AOIManager to avoid fixture interference.
  AOIManager concurrent_aoi(1000, 1000, 20);
  std::atomic<uint64_t> total_entered{0};

  constexpr int kThreadCount = 4;
  constexpr int kEntitiesPerThread = 250;

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  // Each thread enters a disjoint set of entity IDs.
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&concurrent_aoi, &total_entered, t]() {
      uint64_t base = static_cast<uint64_t>(t) * kEntitiesPerThread + 1;
      for (uint64_t i = 0; i < kEntitiesPerThread; ++i) {
        uint64_t id = base + i;
        int32_t x = static_cast<int32_t>((id * 7) % 1000);
        int32_t y = static_cast<int32_t>((id * 13) % 1000);
        concurrent_aoi.Enter(id, x, y);
        total_entered.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(concurrent_aoi.EntityCount(),
            static_cast<size_t>(kThreadCount * kEntitiesPerThread));

  // Now leave all entities concurrently
  threads.clear();
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&concurrent_aoi, t]() {
      uint64_t base = static_cast<uint64_t>(t) * kEntitiesPerThread + 1;
      for (uint64_t i = 0; i < kEntitiesPerThread; ++i) {
        concurrent_aoi.Leave(base + i);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(concurrent_aoi.EntityCount(), 0u);
}

TEST_F(AOIManagerTest, ConcurrentMoveAndQuery) {
  // Populate some entities first.
  AOIManager concurrent_aoi(1000, 1000, 20);
  constexpr int kEntityCount = 100;

  for (uint64_t i = 1; i <= kEntityCount; ++i) {
    concurrent_aoi.Enter(i, static_cast<int32_t>(i * 3 % 1000),
                         static_cast<int32_t>(i * 7 % 1000));
  }

  std::atomic<bool> stop{false};
  constexpr int kMoverThreads = 2;
  constexpr int kQueryThreads = 2;

  std::vector<std::thread> threads;

  // Mover threads: continuously move entities
  for (int t = 0; t < kMoverThreads; ++t) {
    threads.emplace_back([&concurrent_aoi, &stop, t]() {
      int iteration = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        for (uint64_t i = 1; i <= kEntityCount; ++i) {
          int32_t new_x =
              static_cast<int32_t>((i * 3 + iteration * 17 + t) % 1000);
          int32_t new_y =
              static_cast<int32_t>((i * 7 + iteration * 23 + t) % 1000);
          concurrent_aoi.Move(i, new_x, new_y);
        }
        ++iteration;
        if (iteration > 50) break;
      }
    });
  }

  // Query threads: continuously query views
  for (int t = 0; t < kQueryThreads; ++t) {
    threads.emplace_back([&concurrent_aoi, &stop, t]() {
      int iteration = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        for (uint64_t i = 1; i <= kEntityCount; ++i) {
          auto view = concurrent_aoi.GetEntitiesInViewOf(i);
          // Just access the result to ensure no data race.
          (void)view.size();

          bool in_view = concurrent_aoi.InViewOf(i, (i % kEntityCount) + 1);
          (void)in_view;
        }
        ++iteration;
        if (iteration > 50) break;
      }
    });
  }

  // Let threads run for a short while
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop.store(true, std::memory_order_relaxed);

  for (auto& thr : threads) {
    thr.join();
  }

  // All entities should still be present (no corruption).
  EXPECT_EQ(concurrent_aoi.EntityCount(), static_cast<size_t>(kEntityCount));
}

TEST_F(AOIManagerTest, ConcurrentEnterAndClear) {
  // Stress test: some threads enter, another thread clears.
  AOIManager concurrent_aoi(500, 500, 20);
  std::atomic<bool> stop{false};

  std::thread enter_thread([&]() {
    for (uint64_t i = 1; i <= 500 && !stop.load(std::memory_order_relaxed);
         ++i) {
      concurrent_aoi.Enter(
          i, static_cast<int32_t>(i % 500), static_cast<int32_t>(i % 500));
    }
  });

  std::thread clear_thread([&]() {
    for (int round = 0;
         round < 10 && !stop.load(std::memory_order_relaxed); ++round) {
      concurrent_aoi.Clear();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  enter_thread.join();
  clear_thread.join();

  // After both threads finish, the state should be consistent.
  // EntityCount should be valid (not corrupted).
  size_t count = concurrent_aoi.EntityCount();
  // Cannot predict exact count due to interleaving, but it must not exceed 500.
  EXPECT_LE(count, 500u);
}

// ===================================================================
// Additional: Move event semantics detail tests
// ===================================================================

TEST_F(AOIManagerTest, MoveWithinSameGridDoesNotChangeEntityCount) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 10);
  EXPECT_EQ(aoi_->EntityCount(), 2u);

  aoi_->Move(1, 15, 15);
  EXPECT_EQ(aoi_->EntityCount(), 2u);
}

TEST_F(AOIManagerTest, MoveAcrossGridsDoesNotChangeEntityCount) {
  aoi_->Enter(1, 5, 5);
  EXPECT_EQ(aoi_->EntityCount(), 1u);

  aoi_->Move(1, 105, 105);
  EXPECT_EQ(aoi_->EntityCount(), 1u);

  int32_t out_x = -1, out_y = -1;
  EXPECT_TRUE(aoi_->GetEntityPosition(1, out_x, out_y));
  EXPECT_EQ(out_x, 105);
  EXPECT_EQ(out_y, 105);
}

TEST_F(AOIManagerTest, CrossGridMoveEntitiesInOverlapGetMoveNotEnterLeave) {
  // Setup: three entities. Entity A and B are in overlapping view of entity C's
  // old and new positions. Moving C should produce kMove for the overlap, not
  // kEnter or kLeave.
  //
  // Grid layout (grid_size=20):
  //   grid(0,0): entity A at (5,5)
  //   grid(1,0): entity B at (25,5)
  //   grid(2,0): (empty)
  //
  // Entity C starts at grid(1,0) at (30,5). It moves to grid(2,0) at (45,5).
  // Entity B at grid(1,0) is in the old 9-grid AND the new 9-grid, so B
  // should receive kMove.
  // Entity A at grid(0,0) is in the old 9-grid but NOT the new 9-grid
  // (grid(2,0) surrounding is {1,0},{2,0},{3,0},{1,1},{2,1},{3,1}... grid(0,0)
  // is only in old), so A should receive kLeave.

  aoi_->Enter(100, 5, 5);   // A: grid (0,0)
  aoi_->Enter(200, 25, 5);  // B: grid (1,0)
  aoi_->Enter(300, 30, 5);  // C: grid (1,0)
  recorder_.Clear();

  aoi_->Move(300, 45, 5);  // C moves to grid (2,0)

  // B (grid 1,0) is in the overlap: should get kMove
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kMove, 200, 300, 45, 5));

  // A (grid 0,0) is only in old surrounding: should get kLeave
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kLeave, 100, 300, 45, 5));
}

// ===================================================================
// Grid size edge cases
// ===================================================================

TEST_F(AOIManagerTest, SmallGridSizeOneByOne) {
  // Grid size = 1 means every tile is its own grid. 9-grid still applies,
  // so entities within distance 1 in grid coordinates are visible.
  AOIManager tiny_aoi(10, 10, 1);

  tiny_aoi.Enter(1, 0, 0);  // grid (0,0)
  tiny_aoi.Enter(2, 1, 1);  // grid (1,1) -- diagonally adjacent

  EXPECT_TRUE(tiny_aoi.InViewOf(1, 2));

  tiny_aoi.Enter(3, 2, 2);  // grid (2,2) -- distance 2 from grid (0,0)
  EXPECT_FALSE(tiny_aoi.InViewOf(1, 3));
  EXPECT_TRUE(tiny_aoi.InViewOf(2, 3));  // distance 1
}

TEST_F(AOIManagerTest, LargeGridSizeCoversEntireMap) {
  // If grid_size >= map dimensions, there is exactly 1 grid.
  // All entities are in grid (0,0) and visible to each other.
  AOIManager single_grid(100, 100, 200);

  single_grid.Enter(1, 0, 0);
  single_grid.Enter(2, 99, 99);

  EXPECT_TRUE(single_grid.InViewOf(1, 2));

  auto result = single_grid.GetEntitiesInViewOf(1);
  EXPECT_EQ(result.size(), 1u);
  EXPECT_TRUE(Contains(result, uint64_t{2}));
}

// ===================================================================
// Enter event count verification for multiple nearby entities
// ===================================================================

TEST_F(AOIManagerTest, EnterWithThreeEntitiesInSameGridProducesCorrectEventCount) {
  // Enter entity 1 alone: no events.
  aoi_->Enter(1, 5, 5);
  EXPECT_EQ(recorder_.EventCount(), 0u);

  // Enter entity 2 near entity 1: 2 events (1 sees 2, 2 sees 1).
  aoi_->Enter(2, 10, 5);
  EXPECT_EQ(recorder_.EventCount(), 2u);

  // Enter entity 3 near both: 4 more events (1 sees 3, 3 sees 1, 2 sees 3, 3 sees 2).
  aoi_->Enter(3, 15, 5);
  EXPECT_EQ(recorder_.EventCount(), 6u);

  // All events should be kEnter.
  EXPECT_EQ(recorder_.CountByType(AOIEventType::kEnter), 6u);
}

// ===================================================================
// Leave event count verification
// ===================================================================

TEST_F(AOIManagerTest, LeaveWithThreeEntitiesProducesCorrectEventCount) {
  aoi_->Enter(1, 5, 5);
  aoi_->Enter(2, 10, 5);
  aoi_->Enter(3, 15, 5);
  recorder_.Clear();

  // Entity 3 leaves. Entity 1 and entity 2 are notified (2 leave events).
  // The leaving entity (3) does NOT get events.
  aoi_->Leave(3);
  EXPECT_EQ(recorder_.CountByType(AOIEventType::kLeave), 2u);
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kLeave, 1, 3));
  EXPECT_TRUE(recorder_.HasEvent(AOIEventType::kLeave, 2, 3));
}

// ===================================================================
// GetEntitiesInView includes self (by position query, not entity query)
// ===================================================================

TEST_F(AOIManagerTest, GetEntitiesInViewIncludesEntityAtPosition) {
  aoi_->Enter(1, 5, 5);

  auto result = aoi_->GetEntitiesInView(5, 5);
  EXPECT_TRUE(Contains(result, uint64_t{1}));
}

// ===================================================================
// Asymmetric map dimensions
// ===================================================================

TEST_F(AOIManagerTest, AsymmetricMapDimensions) {
  // Narrow but tall map: width=40, height=200, grid_size=20
  // -> 2 grids wide, 10 grids tall
  AOIManager narrow_aoi(40, 200, 20);

  narrow_aoi.Enter(1, 5, 5);     // grid (0, 0)
  narrow_aoi.Enter(2, 25, 5);    // grid (1, 0) -- adjacent
  narrow_aoi.Enter(3, 5, 195);   // grid (0, 9) -- far away vertically

  EXPECT_TRUE(narrow_aoi.InViewOf(1, 2));
  EXPECT_FALSE(narrow_aoi.InViewOf(1, 3));
}

}  // namespace
}  // namespace mir2::game::map
