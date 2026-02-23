/**
 * @file entity_lane_scheduler_test.cc
 * @brief Comprehensive unit tests for EntityLaneScheduler.
 *
 * Tests the per-entity coroutine lane scheduler, verifying lane acquisition,
 * queueing, FIFO release ordering, ScopedLane RAII semantics, and edge cases.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <vector>

#include <asio/io_context.hpp>

#include "logic/entity_lane_scheduler.h"

namespace mir2::logic {
namespace {

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class EntityLaneSchedulerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scheduler_ = std::make_unique<EntityLaneScheduler>(io_context_);
  }

  void TearDown() override { scheduler_.reset(); }

  asio::io_context io_context_;
  std::unique_ptr<EntityLaneScheduler> scheduler_;
};

// ---------------------------------------------------------------------------
// Helper: a minimal coroutine-handle wrapper that records whether it was
// resumed.  We allocate a small frame on the heap so that
// std::coroutine_handle<>::from_promise() gives us a real, resumable handle.
// ---------------------------------------------------------------------------

struct ResumeTracker {
  struct promise_type {
    std::atomic<bool>* resumed_flag = nullptr;

    ResumeTracker get_return_object() { return ResumeTracker{this}; }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };

  explicit ResumeTracker(promise_type* p)
      : handle_(std::coroutine_handle<promise_type>::from_promise(*p)) {}

  ResumeTracker(const ResumeTracker&) = delete;
  ResumeTracker& operator=(const ResumeTracker&) = delete;

  ResumeTracker(ResumeTracker&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }
  ResumeTracker& operator=(ResumeTracker&& other) noexcept {
    if (this != &other) {
      if (handle_ && !handle_.done()) {
        handle_.destroy();
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~ResumeTracker() {
    if (handle_ && !handle_.done()) {
      handle_.destroy();
    }
  }

  std::coroutine_handle<promise_type> handle_ = nullptr;
};

// A trivial coroutine body that, when resumed, sets the flag and suspends
// again so we can safely destroy it afterwards.
ResumeTracker MakeResumeTracker(std::atomic<bool>* flag) {
  // The coroutine starts suspended (initial_suspend = suspend_always).
  // When the handle is first resumed it executes the body below.
  auto tracker = [](std::atomic<bool>* f) -> ResumeTracker {
    // On first resume after initial_suspend we arrive here.
    f->store(true, std::memory_order_release);
    // Suspend again so the frame stays alive for the destructor to destroy.
    co_await std::suspend_always{};
  }(flag);
  return tracker;
}

// ---------------------------------------------------------------------------
// 1. Basic state tests
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, InitialState) {
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
  EXPECT_EQ(scheduler_->PendingWaiters(), 0u);
}

TEST_F(EntityLaneSchedulerTest, EnterReturnsAwaiter) {
  // Enter() should return an EnterAwaiter; just verify we can call it without
  // crashing and that it compiles to the expected type.
  auto awaiter = scheduler_->Enter(1);
  // The awaiter is valid -- we can call await_ready on it.
  (void)awaiter.await_ready();
}

// ---------------------------------------------------------------------------
// 2. TryAcquire via await_ready
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, FirstAcquire_Ready) {
  auto awaiter = scheduler_->Enter(1);
  EXPECT_TRUE(awaiter.await_ready());
}

TEST_F(EntityLaneSchedulerTest, SecondAcquire_NotReady) {
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());
  // Lane 1 is now occupied. Keep it occupied via the ScopedLane.
  auto scoped = first.await_resume();

  auto second = scheduler_->Enter(1);
  EXPECT_FALSE(second.await_ready());
}

TEST_F(EntityLaneSchedulerTest, DifferentLanes_BothReady) {
  auto a1 = scheduler_->Enter(1);
  EXPECT_TRUE(a1.await_ready());
  auto s1 = a1.await_resume();

  auto a2 = scheduler_->Enter(2);
  EXPECT_TRUE(a2.await_ready());
}

TEST_F(EntityLaneSchedulerTest, ActiveLanesAfterAcquire) {
  auto awaiter = scheduler_->Enter(42);
  ASSERT_TRUE(awaiter.await_ready());
  auto scoped = awaiter.await_resume();

  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

// ---------------------------------------------------------------------------
// 3. ScopedLane lifecycle
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, ScopedLaneRelease) {
  {
    auto awaiter = scheduler_->Enter(1);
    ASSERT_TRUE(awaiter.await_ready());
    auto scoped = awaiter.await_resume();
    EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
  }
  // ScopedLane destructed -- lane should have been erased (no waiters).
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
}

TEST_F(EntityLaneSchedulerTest, ScopedLaneMoveConstruct) {
  auto awaiter = scheduler_->Enter(1);
  ASSERT_TRUE(awaiter.await_ready());
  auto original = awaiter.await_resume();
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);

  // Move-construct a new ScopedLane from the original.
  auto moved = std::move(original);

  // The original should no longer own the lane; destroying it should be a
  // no-op. The lane should still be active because 'moved' holds it.
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, ScopedLaneMoveAssign) {
  // Acquire lane 1.
  auto a1 = scheduler_->Enter(1);
  ASSERT_TRUE(a1.await_ready());
  auto lane1 = a1.await_resume();

  // Acquire lane 2.
  auto a2 = scheduler_->Enter(2);
  ASSERT_TRUE(a2.await_ready());
  auto lane2 = a2.await_resume();

  EXPECT_EQ(scheduler_->ActiveLanes(), 2u);

  // Move-assign lane2 = lane1.  This should:
  //   (a) release the old lane held by lane2 (lane 2)
  //   (b) transfer lane 1 ownership from lane1 to lane2
  lane2 = std::move(lane1);

  // Lane 2 should have been released (erased), lane 1 still active.
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, ScopedLaneDefaultConstruct) {
  // A default-constructed ScopedLane should be safe to destroy (no-op).
  { EntityLaneScheduler::ScopedLane lane; }
  // If we get here without crashing, the test passes.
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
}

TEST_F(EntityLaneSchedulerTest, ScopedLaneSelfMoveAssign) {
  auto awaiter = scheduler_->Enter(1);
  ASSERT_TRUE(awaiter.await_ready());
  auto lane = awaiter.await_resume();
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);

  // Self move-assign must be a safe no-op.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
  lane = std::move(lane);
#pragma GCC diagnostic pop

  // Lane should still be active.
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

// ---------------------------------------------------------------------------
// 4. Queueing (await_suspend)
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, SuspendWhenOccupied) {
  // Acquire lane 1 first.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());
  auto scoped = first.await_resume();

  // Second enter on the same lane should not be ready.
  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());

  // await_suspend should return true (the handle is queued).
  auto handle = std::noop_coroutine();
  EXPECT_TRUE(second.await_suspend(handle));
}

TEST_F(EntityLaneSchedulerTest, NoSuspendWhenFree) {
  // When the lane is free, await_suspend should acquire and return false
  // (i.e., do not actually suspend).
  auto awaiter = scheduler_->Enter(1);
  // Deliberately skip await_ready to simulate the race case where
  // await_ready returned false but by the time await_suspend runs the lane
  // is free.
  auto handle = std::noop_coroutine();
  EXPECT_FALSE(awaiter.await_suspend(handle));
}

TEST_F(EntityLaneSchedulerTest, PendingWaitersIncrement) {
  // Occupy lane 1.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());
  auto scoped = first.await_resume();

  EXPECT_EQ(scheduler_->PendingWaiters(), 0u);

  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());
  EXPECT_TRUE(second.await_suspend(std::noop_coroutine()));

  EXPECT_EQ(scheduler_->PendingWaiters(), 1u);
}

TEST_F(EntityLaneSchedulerTest, MultiplePendingWaiters) {
  // Occupy lane 1.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());
  auto scoped = first.await_resume();

  // Queue 5 waiters on the same lane.
  std::vector<EntityLaneScheduler::EnterAwaiter> awaiters;
  for (int i = 0; i < 5; ++i) {
    awaiters.push_back(scheduler_->Enter(1));
    ASSERT_FALSE(awaiters.back().await_ready());
    ASSERT_TRUE(awaiters.back().await_suspend(std::noop_coroutine()));
  }

  EXPECT_EQ(scheduler_->PendingWaiters(), 5u);
}

// ---------------------------------------------------------------------------
// 5. Release with pending waiters
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, ReleaseResumesNext) {
  // Acquire lane 1.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());

  // Create a trackable coroutine handle for the waiter.
  std::atomic<bool> resumed{false};
  auto tracker = MakeResumeTracker(&resumed);
  auto raw_handle =
      std::coroutine_handle<>::from_address(tracker.handle_.address());

  // Queue the waiter.
  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());
  ASSERT_TRUE(second.await_suspend(raw_handle));

  // Release lane 1 by destroying the ScopedLane.
  {
    auto scoped = first.await_resume();
    // scoped destructor will call Release.
  }

  // The resume should be posted to io_context.  Execute it.
  EXPECT_FALSE(resumed.load(std::memory_order_acquire));
  auto executed = io_context_.run_one();
  EXPECT_GE(executed, 1u);
  EXPECT_TRUE(resumed.load(std::memory_order_acquire));
}

TEST_F(EntityLaneSchedulerTest, ReleaseDecrementsWaiters) {
  // Occupy lane 1.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());

  // Queue a waiter.
  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());
  ASSERT_TRUE(second.await_suspend(std::noop_coroutine()));
  EXPECT_EQ(scheduler_->PendingWaiters(), 1u);

  // Release lane 1.
  { auto scoped = first.await_resume(); }

  // PendingWaiters should have decremented.
  EXPECT_EQ(scheduler_->PendingWaiters(), 0u);
}

TEST_F(EntityLaneSchedulerTest, ReleaseLaneStaysWhenWaitersExist) {
  // Occupy lane 1.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());

  // Queue a waiter.
  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());
  ASSERT_TRUE(second.await_suspend(std::noop_coroutine()));

  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);

  // Release lane 1.
  { auto scoped = first.await_resume(); }

  // The lane should still be present because the next waiter has been given
  // ownership (the lane entry persists for the next occupant).
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, ReleaseErasesLaneWhenEmpty) {
  {
    auto awaiter = scheduler_->Enter(1);
    ASSERT_TRUE(awaiter.await_ready());
    auto scoped = awaiter.await_resume();
    EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
  }
  // No waiters -- Release should erase the lane entirely.
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
}

// ---------------------------------------------------------------------------
// 6. Multiple lanes
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, IndependentLanes) {
  // Acquire lane 10.
  auto a10 = scheduler_->Enter(10);
  ASSERT_TRUE(a10.await_ready());
  auto s10 = a10.await_resume();

  // Acquire lane 20 -- should succeed independently.
  auto a20 = scheduler_->Enter(20);
  EXPECT_TRUE(a20.await_ready());
  auto s20 = a20.await_resume();

  EXPECT_EQ(scheduler_->ActiveLanes(), 2u);

  // Attempt lane 10 again -- should be blocked.
  auto a10b = scheduler_->Enter(10);
  EXPECT_FALSE(a10b.await_ready());

  // Attempt lane 20 again -- should be blocked.
  auto a20b = scheduler_->Enter(20);
  EXPECT_FALSE(a20b.await_ready());
}

TEST_F(EntityLaneSchedulerTest, ManyLanes) {
  std::vector<EntityLaneScheduler::ScopedLane> lanes;
  lanes.reserve(100);

  for (uint64_t i = 0; i < 100; ++i) {
    auto awaiter = scheduler_->Enter(i);
    ASSERT_TRUE(awaiter.await_ready()) << "lane " << i << " should be free";
    lanes.push_back(awaiter.await_resume());
  }

  EXPECT_EQ(scheduler_->ActiveLanes(), 100u);
}

TEST_F(EntityLaneSchedulerTest, ManyLanesRelease) {
  {
    std::vector<EntityLaneScheduler::ScopedLane> lanes;
    lanes.reserve(100);

    for (uint64_t i = 0; i < 100; ++i) {
      auto awaiter = scheduler_->Enter(i);
      ASSERT_TRUE(awaiter.await_ready());
      lanes.push_back(awaiter.await_resume());
    }
    ASSERT_EQ(scheduler_->ActiveLanes(), 100u);
  }
  // All ScopedLanes destroyed; all lanes should be erased.
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
}

// ---------------------------------------------------------------------------
// 7. Edge cases
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, NullSchedulerAwaiter_AwaitReady) {
  // EnterAwaiter with nullptr scheduler: await_ready returns true.
  EntityLaneScheduler::EnterAwaiter awaiter(nullptr, 1);
  EXPECT_TRUE(awaiter.await_ready());
}

TEST_F(EntityLaneSchedulerTest, NullSchedulerAwaiter_AwaitSuspend) {
  // EnterAwaiter with nullptr scheduler: await_suspend returns false.
  EntityLaneScheduler::EnterAwaiter awaiter(nullptr, 1);
  EXPECT_FALSE(awaiter.await_suspend(std::noop_coroutine()));
}

TEST_F(EntityLaneSchedulerTest, NullSchedulerAwaiter_AwaitResume) {
  // EnterAwaiter with nullptr scheduler: await_resume returns a ScopedLane
  // with null scheduler; destroying it should be a safe no-op.
  EntityLaneScheduler::EnterAwaiter awaiter(nullptr, 1);
  auto scoped = awaiter.await_resume();
  // Destructor should not crash.
}

TEST_F(EntityLaneSchedulerTest, ReleaseUnknownLane) {
  // Releasing a lane that was never acquired should be a no-op.
  // We cannot call Release directly (it's private), but we can simulate this
  // by creating a ScopedLane for a lane that was never TryAcquired.
  // Since ScopedLane(scheduler, key) calls Release(key) in its destructor,
  // and Release returns early if the lane is not found, this should be safe.
  //
  // We construct the ScopedLane via the awaiter's await_resume (which always
  // creates a ScopedLane(scheduler, key) regardless of lane state).
  {
    EntityLaneScheduler::EnterAwaiter awaiter(scheduler_.get(), 999);
    // Skip await_ready/await_suspend -- directly call await_resume to get
    // a ScopedLane that references lane 999 which was never acquired.
    // This simulates an edge case where Release is called on a non-existent
    // lane.
    //
    // But first we need to be careful: await_resume returns ScopedLane that
    // will call Release(999). Since lane 999 was never created in lanes_,
    // Release should early-return on lanes_.find == end.
    auto scoped = awaiter.await_resume();
  }
  // No crash, no state corruption.
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
}

TEST_F(EntityLaneSchedulerTest, ZeroLaneKey) {
  auto awaiter = scheduler_->Enter(0);
  EXPECT_TRUE(awaiter.await_ready());
  auto scoped = awaiter.await_resume();
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, MaxUint64LaneKey) {
  auto awaiter = scheduler_->Enter(UINT64_MAX);
  EXPECT_TRUE(awaiter.await_ready());
  auto scoped = awaiter.await_resume();
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

// ---------------------------------------------------------------------------
// 8. Integration with asio
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, PostedResumeExecutes) {
  // Verify that Release with waiters posts a handler to io_context and
  // running io_context processes it.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());

  std::atomic<bool> resumed{false};
  auto tracker = MakeResumeTracker(&resumed);
  auto raw_handle =
      std::coroutine_handle<>::from_address(tracker.handle_.address());

  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());
  ASSERT_TRUE(second.await_suspend(raw_handle));

  // Release the first occupant.
  { auto scoped = first.await_resume(); }

  // io_context should have one pending handler.
  EXPECT_FALSE(resumed.load(std::memory_order_acquire));
  size_t handlers_run = io_context_.run_one();
  EXPECT_EQ(handlers_run, 1u);
  EXPECT_TRUE(resumed.load(std::memory_order_acquire));

  // No more handlers pending.
  io_context_.restart();
  EXPECT_EQ(io_context_.poll(), 0u);
}

TEST_F(EntityLaneSchedulerTest, FIFOOrdering) {
  // Verify that multiple waiters on the same lane are resumed in FIFO order.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());

  constexpr int kWaiterCount = 4;
  std::vector<std::atomic<bool>> resumed_flags(kWaiterCount);
  for (auto& f : resumed_flags) {
    f.store(false, std::memory_order_relaxed);
  }
  std::vector<ResumeTracker> trackers;
  trackers.reserve(kWaiterCount);

  // Queue waiters in order 0, 1, 2, 3.
  std::vector<EntityLaneScheduler::EnterAwaiter> awaiters;
  for (int i = 0; i < kWaiterCount; ++i) {
    trackers.push_back(MakeResumeTracker(&resumed_flags[i]));
    auto raw_handle =
        std::coroutine_handle<>::from_address(trackers.back().handle_.address());

    awaiters.push_back(scheduler_->Enter(1));
    ASSERT_FALSE(awaiters.back().await_ready());
    ASSERT_TRUE(awaiters.back().await_suspend(raw_handle));
  }

  ASSERT_EQ(scheduler_->PendingWaiters(), static_cast<size_t>(kWaiterCount));

  // Release the first occupant; this should post waiter 0.
  { auto scoped = first.await_resume(); }

  // Run the posted handler -- waiter 0 should be resumed first.
  io_context_.run_one();
  EXPECT_TRUE(resumed_flags[0].load(std::memory_order_acquire));
  EXPECT_FALSE(resumed_flags[1].load(std::memory_order_acquire));
  EXPECT_FALSE(resumed_flags[2].load(std::memory_order_acquire));
  EXPECT_FALSE(resumed_flags[3].load(std::memory_order_acquire));

  // Now waiter 0 holds the lane (via the resume).  To release it and trigger
  // waiter 1, we simulate what would happen when waiter 0's ScopedLane is
  // destroyed.  Since waiter 0 was resumed by asio::post (not by our direct
  // code), the lane is still occupied with waiters 1-3 queued.
  //
  // We need to create a ScopedLane for waiter 0 and destroy it to trigger
  // the next release.
  {
    auto scoped0 = awaiters[0].await_resume();
    // scoped0 destructor will Release lane 1, posting waiter 1.
  }
  io_context_.restart();
  io_context_.run_one();
  EXPECT_TRUE(resumed_flags[1].load(std::memory_order_acquire));
  EXPECT_FALSE(resumed_flags[2].load(std::memory_order_acquire));
  EXPECT_FALSE(resumed_flags[3].load(std::memory_order_acquire));

  // Release waiter 1's lane -> posts waiter 2.
  {
    auto scoped1 = awaiters[1].await_resume();
  }
  io_context_.restart();
  io_context_.run_one();
  EXPECT_TRUE(resumed_flags[2].load(std::memory_order_acquire));
  EXPECT_FALSE(resumed_flags[3].load(std::memory_order_acquire));

  // Release waiter 2's lane -> posts waiter 3.
  {
    auto scoped2 = awaiters[2].await_resume();
  }
  io_context_.restart();
  io_context_.run_one();
  EXPECT_TRUE(resumed_flags[3].load(std::memory_order_acquire));

  // Release waiter 3's lane -> no more waiters, lane should be erased.
  {
    auto scoped3 = awaiters[3].await_resume();
  }
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);
  EXPECT_EQ(scheduler_->PendingWaiters(), 0u);
}

// ---------------------------------------------------------------------------
// 9. Additional edge cases and stress scenarios
// ---------------------------------------------------------------------------

TEST_F(EntityLaneSchedulerTest, ReacquireAfterRelease) {
  // Acquire and release a lane, then re-acquire it.
  {
    auto awaiter = scheduler_->Enter(1);
    ASSERT_TRUE(awaiter.await_ready());
    auto scoped = awaiter.await_resume();
  }
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);

  // Re-acquire the same lane.
  auto awaiter = scheduler_->Enter(1);
  EXPECT_TRUE(awaiter.await_ready());
  auto scoped = awaiter.await_resume();
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, MoveConstructedScopedLaneReleasesOnDestruction) {
  auto awaiter = scheduler_->Enter(1);
  ASSERT_TRUE(awaiter.await_ready());
  auto original = awaiter.await_resume();

  {
    auto moved = std::move(original);
    EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
    // 'moved' goes out of scope here -- should release the lane.
  }
  EXPECT_EQ(scheduler_->ActiveLanes(), 0u);

  // The original should be a no-op on destruction.
}

TEST_F(EntityLaneSchedulerTest, MoveAssignedScopedLaneReleasesOldLaneImmediately) {
  // Acquire lanes 1 and 2.
  auto a1 = scheduler_->Enter(1);
  ASSERT_TRUE(a1.await_ready());
  auto lane1 = a1.await_resume();

  auto a2 = scheduler_->Enter(2);
  ASSERT_TRUE(a2.await_ready());
  auto lane2 = a2.await_resume();
  EXPECT_EQ(scheduler_->ActiveLanes(), 2u);

  // Move-assign: lane1 = lane2. This should release lane 1 (old) immediately,
  // and transfer lane 2 ownership to lane1.
  lane1 = std::move(lane2);
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);

  // Verify lane 1 is now free (can be re-acquired).
  auto a1b = scheduler_->Enter(1);
  EXPECT_TRUE(a1b.await_ready());
}

TEST_F(EntityLaneSchedulerTest, ConsecutiveLaneKeys) {
  // Acquire consecutive lane keys to verify no hash collisions affect behavior.
  std::vector<EntityLaneScheduler::ScopedLane> lanes;
  for (uint64_t i = 1000; i < 1010; ++i) {
    auto awaiter = scheduler_->Enter(i);
    ASSERT_TRUE(awaiter.await_ready()) << "lane " << i;
    lanes.push_back(awaiter.await_resume());
  }
  EXPECT_EQ(scheduler_->ActiveLanes(), 10u);
}

TEST_F(EntityLaneSchedulerTest, PendingWaitersCountAcrossMultipleLanes) {
  // Occupy lanes 1 and 2, then queue waiters on both.
  auto a1 = scheduler_->Enter(1);
  ASSERT_TRUE(a1.await_ready());
  auto s1 = a1.await_resume();

  auto a2 = scheduler_->Enter(2);
  ASSERT_TRUE(a2.await_ready());
  auto s2 = a2.await_resume();

  // Queue 3 waiters on lane 1.
  for (int i = 0; i < 3; ++i) {
    auto w = scheduler_->Enter(1);
    ASSERT_FALSE(w.await_ready());
    ASSERT_TRUE(w.await_suspend(std::noop_coroutine()));
  }

  // Queue 2 waiters on lane 2.
  for (int i = 0; i < 2; ++i) {
    auto w = scheduler_->Enter(2);
    ASSERT_FALSE(w.await_ready());
    ASSERT_TRUE(w.await_suspend(std::noop_coroutine()));
  }

  // Total pending should be 5.
  EXPECT_EQ(scheduler_->PendingWaiters(), 5u);
  EXPECT_EQ(scheduler_->ActiveLanes(), 2u);
}

TEST_F(EntityLaneSchedulerTest, ReleaseWithNoop_CoroutineHandleDoesNotCrash) {
  // Queue a noop_coroutine handle and verify that release + io_context
  // processing does not crash.
  auto first = scheduler_->Enter(1);
  ASSERT_TRUE(first.await_ready());

  auto second = scheduler_->Enter(1);
  ASSERT_FALSE(second.await_ready());
  ASSERT_TRUE(second.await_suspend(std::noop_coroutine()));

  // Release the first occupant.
  { auto scoped = first.await_resume(); }

  // Run the posted handler -- noop_coroutine.resume() should be harmless.
  size_t handled = io_context_.run_one();
  EXPECT_EQ(handled, 1u);
}

TEST_F(EntityLaneSchedulerTest, MultipleReleasesOnSameLaneSequentially) {
  // Acquire lane 1, release, acquire again, release again. Verify clean state.
  for (int round = 0; round < 10; ++round) {
    {
      auto awaiter = scheduler_->Enter(1);
      ASSERT_TRUE(awaiter.await_ready()) << "round=" << round;
      auto scoped = awaiter.await_resume();
      EXPECT_EQ(scheduler_->ActiveLanes(), 1u) << "round=" << round;
    }
    EXPECT_EQ(scheduler_->ActiveLanes(), 0u) << "round=" << round;
    EXPECT_EQ(scheduler_->PendingWaiters(), 0u) << "round=" << round;
  }
}

TEST_F(EntityLaneSchedulerTest, QueueOrAcquireOnFreeLaneViaAwaitSuspend) {
  // When await_suspend is called on a free lane (skipping await_ready), it
  // should acquire without suspending (return false).
  auto awaiter = scheduler_->Enter(7);
  // Do NOT call await_ready.
  bool suspended = awaiter.await_suspend(std::noop_coroutine());
  EXPECT_FALSE(suspended);
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, AwaitResumeAlwaysReturnsScopedLane) {
  // await_resume should always return a valid ScopedLane, regardless of
  // whether the lane was acquired via await_ready or await_suspend.
  auto awaiter = scheduler_->Enter(1);
  ASSERT_TRUE(awaiter.await_ready());

  auto scoped = awaiter.await_resume();
  // Verify the ScopedLane is functional by checking it keeps the lane active.
  EXPECT_EQ(scheduler_->ActiveLanes(), 1u);
}

TEST_F(EntityLaneSchedulerTest, DestroySchedulerWithActiveLanes) {
  // Acquire a lane, then destroy the scheduler while the lane is still active.
  // The ScopedLane will try to call Release, but the scheduler is already gone.
  // This tests that the ScopedLane does not hold a dangling reference in a way
  // that causes a crash (in practice, the ScopedLane MUST be destroyed before
  // the scheduler, but we test that the destruction order doesn't cause issues
  // when done correctly).
  auto awaiter = scheduler_->Enter(1);
  ASSERT_TRUE(awaiter.await_ready());
  auto scoped = awaiter.await_resume();

  // Destroy the ScopedLane first (correct order).
  scoped = EntityLaneScheduler::ScopedLane{};

  // Now it's safe to destroy the scheduler.
  scheduler_.reset();
}

}  // namespace
}  // namespace mir2::logic
