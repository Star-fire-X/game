/**
 * @file spsc_ring.h
 * @brief Bounded lock-free SPSC ring queue.
 */

#ifndef MIR2_CORE_CONCURRENCY_SPSC_RING_H_
#define MIR2_CORE_CONCURRENCY_SPSC_RING_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace mir2::core::concurrency {

template <typename T, std::size_t Capacity>
class SpscRingQueue {
 public:
  static_assert(Capacity >= 2, "SpscRingQueue capacity must be >= 2.");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "SpscRingQueue capacity must be power-of-two.");
  static_assert(std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>,
                "SpscRingQueue requires assignable value type.");

  SpscRingQueue() = default;
  SpscRingQueue(const SpscRingQueue&) = delete;
  SpscRingQueue& operator=(const SpscRingQueue&) = delete;

  bool TryEnqueue(const T& value) { return Emplace(value); }
  bool TryEnqueue(T&& value) { return Emplace(std::move(value)); }

  bool TryDequeue(T* out) {
    if (!out) {
      return false;
    }

    const std::size_t tail = tail_.load(std::memory_order_acquire);
    std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail) {
      return false;
    }

    *out = std::move(storage_[head & kMask]);
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  std::size_t ApproxSize() const {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    return tail - head;
  }

 private:
  template <typename U>
  bool Emplace(U&& value) {
    const std::size_t head = head_.load(std::memory_order_acquire);
    std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - head >= Capacity) {
      return false;
    }

    storage_[tail & kMask] = std::forward<U>(value);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  static constexpr std::size_t kMask = Capacity - 1;

  std::array<T, Capacity> storage_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace mir2::core::concurrency

#endif  // MIR2_CORE_CONCURRENCY_SPSC_RING_H_
