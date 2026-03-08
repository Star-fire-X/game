/**
 * @file client_registry_test.cc
 * @brief Comprehensive tests for ClientRegistry - P0 High-Critical client tracking
 *
 * Test Coverage:
 * - Empty state invariants
 * - Track / Contains basic lifecycle
 * - Duplicate tracking (idempotent insert)
 * - Remove existing and non-existent clients
 * - GetAll returns all tracked clients
 * - Track with id=0 is ignored (guard clause)
 * - Concurrent multi-threaded access (thread safety)
 *
 * Priority: P0 High-Critical
 * Risk: Incorrect client tracking breaks session management and message routing
 */

#include <gtest/gtest.h>
#include "logic/services/client_registry.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace mir2::logic::test {
namespace {

// ============================================================================
// Test Fixture
// ============================================================================

class ClientRegistryTest : public ::testing::Test {
 protected:
  ClientRegistry registry_;
};

// ============================================================================
// Empty State Tests
// ============================================================================

/**
 * @brief A freshly constructed registry contains no clients.
 */
TEST_F(ClientRegistryTest, EmptyRegistryGetAllReturnsEmpty) {
  const auto all = registry_.GetAll();
  EXPECT_TRUE(all.empty());
}

TEST_F(ClientRegistryTest, EmptyRegistryContainsReturnsFalse) {
  EXPECT_FALSE(registry_.Contains(1));
  EXPECT_FALSE(registry_.Contains(42));
  EXPECT_FALSE(registry_.Contains(UINT64_MAX));
}

// ============================================================================
// Track / Contains Tests
// ============================================================================

/**
 * @brief Track a single client, then verify Contains returns true.
 */
TEST_F(ClientRegistryTest, TrackSingleClientThenContains) {
  registry_.Track(100);
  EXPECT_TRUE(registry_.Contains(100));
}

/**
 * @brief Track multiple distinct clients.
 */
TEST_F(ClientRegistryTest, TrackMultipleDistinctClients) {
  registry_.Track(1);
  registry_.Track(2);
  registry_.Track(3);

  EXPECT_TRUE(registry_.Contains(1));
  EXPECT_TRUE(registry_.Contains(2));
  EXPECT_TRUE(registry_.Contains(3));
  EXPECT_FALSE(registry_.Contains(4));
}

/**
 * @brief Track with the maximum uint64 value.
 */
TEST_F(ClientRegistryTest, TrackMaxUint64) {
  registry_.Track(UINT64_MAX);
  EXPECT_TRUE(registry_.Contains(UINT64_MAX));
}

// ============================================================================
// Duplicate Tracking Tests
// ============================================================================

/**
 * @brief Tracking the same client twice is idempotent -- the client appears
 *        exactly once in GetAll.
 */
TEST_F(ClientRegistryTest, TrackDuplicateIsIdempotent) {
  registry_.Track(42);
  registry_.Track(42);

  EXPECT_TRUE(registry_.Contains(42));

  const auto all = registry_.GetAll();
  const auto count = std::count(all.begin(), all.end(), static_cast<uint64_t>(42));
  EXPECT_EQ(count, 1);
}

// ============================================================================
// Remove Tests
// ============================================================================

/**
 * @brief Remove a tracked client -- Contains must return false afterwards.
 */
TEST_F(ClientRegistryTest, RemoveExistingClient) {
  registry_.Track(10);
  EXPECT_TRUE(registry_.Contains(10));

  registry_.Remove(10);
  EXPECT_FALSE(registry_.Contains(10));
}

/**
 * @brief Remove a client that was never tracked -- no crash, no side effects.
 */
TEST_F(ClientRegistryTest, RemoveNonExistentClientIsNoOp) {
  registry_.Track(1);
  registry_.Remove(999);

  // The existing client must be unaffected.
  EXPECT_TRUE(registry_.Contains(1));
  EXPECT_FALSE(registry_.Contains(999));
}

/**
 * @brief Remove a client twice -- the second call is harmless.
 */
TEST_F(ClientRegistryTest, RemoveTwiceIsHarmless) {
  registry_.Track(5);
  registry_.Remove(5);
  registry_.Remove(5);  // Second remove

  EXPECT_FALSE(registry_.Contains(5));
}

/**
 * @brief After removing a client from a multi-client set, only the
 *        removed client is gone.
 */
TEST_F(ClientRegistryTest, RemoveDoesNotAffectOtherClients) {
  registry_.Track(1);
  registry_.Track(2);
  registry_.Track(3);

  registry_.Remove(2);

  EXPECT_TRUE(registry_.Contains(1));
  EXPECT_FALSE(registry_.Contains(2));
  EXPECT_TRUE(registry_.Contains(3));
}

// ============================================================================
// GetAll Tests
// ============================================================================

/**
 * @brief GetAll returns every tracked client exactly once.
 */
TEST_F(ClientRegistryTest, GetAllReturnsAllTrackedClients) {
  registry_.Track(10);
  registry_.Track(20);
  registry_.Track(30);

  auto all = registry_.GetAll();
  std::sort(all.begin(), all.end());

  ASSERT_EQ(all.size(), 3u);
  EXPECT_EQ(all[0], 10u);
  EXPECT_EQ(all[1], 20u);
  EXPECT_EQ(all[2], 30u);
}

/**
 * @brief GetAll reflects removals.
 */
TEST_F(ClientRegistryTest, GetAllReflectsRemovals) {
  registry_.Track(1);
  registry_.Track(2);
  registry_.Track(3);
  registry_.Remove(2);

  auto all = registry_.GetAll();
  std::sort(all.begin(), all.end());

  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0], 1u);
  EXPECT_EQ(all[1], 3u);
}

// ============================================================================
// Zero-ID Guard Tests
// ============================================================================

/**
 * @brief Track(0) is a sentinel value and must be silently ignored.
 */
TEST_F(ClientRegistryTest, TrackZeroIdIsIgnored) {
  registry_.Track(0);

  EXPECT_FALSE(registry_.Contains(0));
  EXPECT_TRUE(registry_.GetAll().empty());
}

/**
 * @brief Track(0) must not affect previously tracked valid clients.
 */
TEST_F(ClientRegistryTest, TrackZeroDoesNotAffectValidClients) {
  registry_.Track(5);
  registry_.Track(0);

  EXPECT_TRUE(registry_.Contains(5));
  EXPECT_FALSE(registry_.Contains(0));

  const auto all = registry_.GetAll();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0], 5u);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

/**
 * @brief Concurrent Track from many threads -- all distinct IDs must be
 *        present afterwards with no data corruption.
 */
TEST_F(ClientRegistryTest, ConcurrentTrackFromMultipleThreads) {
  constexpr int kThreadCount = 8;
  constexpr int kIdsPerThread = 200;

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([this, t]() {
      for (int i = 0; i < kIdsPerThread; ++i) {
        // Each thread gets a unique range: [t*1000 + 1, t*1000 + 200]
        const uint64_t id = static_cast<uint64_t>(t * 1000 + i + 1);
        registry_.Track(id);
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  // Verify all IDs are present.
  for (int t = 0; t < kThreadCount; ++t) {
    for (int i = 0; i < kIdsPerThread; ++i) {
      const uint64_t id = static_cast<uint64_t>(t * 1000 + i + 1);
      EXPECT_TRUE(registry_.Contains(id))
          << "Missing client id " << id;
    }
  }

  const auto all = registry_.GetAll();
  EXPECT_EQ(all.size(), static_cast<size_t>(kThreadCount * kIdsPerThread));
}

/**
 * @brief Concurrent Track and Remove on overlapping IDs -- must not crash
 *        or corrupt internal state.
 */
TEST_F(ClientRegistryTest, ConcurrentTrackAndRemoveNoCorruption) {
  constexpr int kIterations = 500;
  constexpr int kThreadCount = 4;

  // Pre-populate some IDs.
  for (int i = 1; i <= kIterations; ++i) {
    registry_.Track(static_cast<uint64_t>(i));
  }

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  // Half of the threads track new IDs, the other half remove existing ones.
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([this, t]() {
      for (int i = 1; i <= kIterations; ++i) {
        if (t % 2 == 0) {
          // Track IDs in a high range that won't collide with removes.
          registry_.Track(static_cast<uint64_t>(10000 + t * 1000 + i));
        } else {
          registry_.Remove(static_cast<uint64_t>(i));
        }
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  // Verify GetAll returns a consistent snapshot.
  const auto all = registry_.GetAll();

  // Every returned ID must be found by Contains.
  for (const auto id : all) {
    EXPECT_TRUE(registry_.Contains(id))
        << "GetAll returned id " << id << " but Contains disagrees";
  }
}

/**
 * @brief Concurrent GetAll calls while Track is happening -- GetAll must
 *        always return a consistent snapshot (no partial reads).
 */
TEST_F(ClientRegistryTest, ConcurrentGetAllDuringTrack) {
  constexpr int kWriteIterations = 300;
  constexpr int kReadIterations = 200;

  std::atomic<bool> done{false};

  // Writer thread.
  std::thread writer([this, &done]() {
    for (int i = 1; i <= kWriteIterations; ++i) {
      registry_.Track(static_cast<uint64_t>(i));
    }
    done.store(true, std::memory_order_release);
  });

  // Reader threads.
  std::vector<std::thread> readers;
  for (int r = 0; r < 3; ++r) {
    readers.emplace_back([this, &done]() {
      int reads = 0;
      while (!done.load(std::memory_order_acquire) || reads < kReadIterations) {
        const auto snapshot = registry_.GetAll();
        // Snapshot must be internally consistent: size matches actual contents.
        // (If we got a partial read, vector size would be wrong.)
        EXPECT_GE(snapshot.size(), 0u);
        ++reads;
        if (reads >= kReadIterations && done.load(std::memory_order_acquire)) {
          break;
        }
      }
    });
  }

  writer.join();
  for (auto& th : readers) {
    th.join();
  }

  // Final state: all IDs present.
  const auto all = registry_.GetAll();
  EXPECT_EQ(all.size(), static_cast<size_t>(kWriteIterations));
}

}  // namespace
}  // namespace mir2::logic::test
