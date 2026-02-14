#include "logic/entity_lane_scheduler.h"

#include <asio/post.hpp>

namespace mir2::logic {

EntityLaneScheduler::EntityLaneScheduler(asio::io_context& io_context)
    : io_context_(io_context) {}

EntityLaneScheduler::ScopedLane::ScopedLane(EntityLaneScheduler* scheduler,
                                            uint64_t lane_key)
    : scheduler_(scheduler), lane_key_(lane_key) {}

EntityLaneScheduler::ScopedLane::~ScopedLane() {
  if (scheduler_) {
    scheduler_->Release(lane_key_);
  }
}

EntityLaneScheduler::ScopedLane::ScopedLane(ScopedLane&& other) noexcept
    : scheduler_(other.scheduler_), lane_key_(other.lane_key_) {
  other.scheduler_ = nullptr;
  other.lane_key_ = 0;
}

EntityLaneScheduler::ScopedLane& EntityLaneScheduler::ScopedLane::operator=(
    ScopedLane&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (scheduler_) {
    scheduler_->Release(lane_key_);
  }
  scheduler_ = other.scheduler_;
  lane_key_ = other.lane_key_;
  other.scheduler_ = nullptr;
  other.lane_key_ = 0;
  return *this;
}

EntityLaneScheduler::EnterAwaiter::EnterAwaiter(EntityLaneScheduler* scheduler,
                                                uint64_t lane_key)
    : scheduler_(scheduler), lane_key_(lane_key) {}

bool EntityLaneScheduler::EnterAwaiter::await_ready() const {
  if (!scheduler_) {
    return true;
  }
  return scheduler_->TryAcquire(lane_key_);
}

bool EntityLaneScheduler::EnterAwaiter::await_suspend(std::coroutine_handle<> handle) {
  if (!scheduler_) {
    return false;
  }
  return scheduler_->QueueOrAcquire(lane_key_, handle);
}

EntityLaneScheduler::ScopedLane EntityLaneScheduler::EnterAwaiter::await_resume() {
  return ScopedLane(scheduler_, lane_key_);
}

EntityLaneScheduler::EnterAwaiter EntityLaneScheduler::Enter(uint64_t lane_key) {
  return EnterAwaiter(this, lane_key);
}

bool EntityLaneScheduler::TryAcquire(uint64_t lane_key) {
  auto& lane = lanes_[lane_key];
  if (lane.occupied) {
    return false;
  }
  lane.occupied = true;
  return true;
}

bool EntityLaneScheduler::QueueOrAcquire(uint64_t lane_key, std::coroutine_handle<> handle) {
  auto& lane = lanes_[lane_key];
  if (!lane.occupied) {
    lane.occupied = true;
    return false;
  }
  lane.waiters.push_back(handle);
  ++pending_waiters_;
  return true;
}

void EntityLaneScheduler::Release(uint64_t lane_key) {
  auto it = lanes_.find(lane_key);
  if (it == lanes_.end()) {
    return;
  }

  auto& lane = it->second;
  if (!lane.waiters.empty()) {
    const auto next = lane.waiters.front();
    lane.waiters.pop_front();
    if (pending_waiters_ > 0) {
      --pending_waiters_;
    }
    asio::post(io_context_, [next]() mutable {
      if (next) {
        next.resume();
      }
    });
    return;
  }

  lanes_.erase(it);
}

}  // namespace mir2::logic
