/**
 * @file entity_lane_scheduler.h
 * @brief Per-entity coroutine lane scheduler (single-threaded logic context).
 */

#ifndef MIR2_LOGIC_ENTITY_LANE_SCHEDULER_H_
#define MIR2_LOGIC_ENTITY_LANE_SCHEDULER_H_

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>

#include <asio/io_context.hpp>

namespace mir2::logic {

class EntityLaneScheduler {
 public:
  explicit EntityLaneScheduler(asio::io_context& io_context);

  class ScopedLane {
   public:
    ScopedLane() = default;
    ScopedLane(EntityLaneScheduler* scheduler, uint64_t lane_key);
    ~ScopedLane();

    ScopedLane(const ScopedLane&) = delete;
    ScopedLane& operator=(const ScopedLane&) = delete;
    ScopedLane(ScopedLane&& other) noexcept;
    ScopedLane& operator=(ScopedLane&& other) noexcept;

   private:
    EntityLaneScheduler* scheduler_ = nullptr;
    uint64_t lane_key_ = 0;
  };

  class EnterAwaiter {
   public:
    EnterAwaiter(EntityLaneScheduler* scheduler, uint64_t lane_key);
    bool await_ready() const;
    bool await_suspend(std::coroutine_handle<> handle);
    ScopedLane await_resume();

   private:
    EntityLaneScheduler* scheduler_ = nullptr;
    uint64_t lane_key_ = 0;
  };

  EnterAwaiter Enter(uint64_t lane_key);

  size_t ActiveLanes() const { return lanes_.size(); }
  size_t PendingWaiters() const { return pending_waiters_; }

 private:
  struct LaneState {
    bool occupied = false;
    std::deque<std::coroutine_handle<>> waiters;
  };

  bool TryAcquire(uint64_t lane_key);
  bool QueueOrAcquire(uint64_t lane_key, std::coroutine_handle<> handle);
  void Release(uint64_t lane_key);

  asio::io_context& io_context_;
  std::unordered_map<uint64_t, LaneState> lanes_;
  size_t pending_waiters_ = 0;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_ENTITY_LANE_SCHEDULER_H_
