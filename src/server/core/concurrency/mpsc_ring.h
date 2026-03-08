/**
 * @file mpsc_ring.h
 * @brief Bounded lock-free MPSC ring queue.
 */

#ifndef MIR2_CORE_CONCURRENCY_MPSC_RING_H_
#define MIR2_CORE_CONCURRENCY_MPSC_RING_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace mir2::core::concurrency {

template <typename T, std::size_t Capacity>
class MpscRingQueue {
 public:
  static_assert(Capacity >= 2, "MpscRingQueue capacity must be >= 2.");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "MpscRingQueue capacity must be power-of-two.");
  static_assert(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>,
                "MpscRingQueue requires assignable value type.");

  MpscRingQueue() {
    for (std::size_t i = 0; i < Capacity; ++i) {
      slots_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  MpscRingQueue(const MpscRingQueue&) = delete;
  MpscRingQueue& operator=(const MpscRingQueue&) = delete;

  bool TryEnqueue(const T& value) { return Emplace(value); }

  bool TryEnqueue(T&& value) { return Emplace(std::move(value)); }

  bool TryDequeue(T* out) {
    if (!out) {
      return false;
    }

    std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    Slot& slot = slots_[pos & kMask];
    const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
    const intptr_t diff =
        static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

    if (diff == 0) {
      dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
      *out = std::move(slot.value);
      slot.sequence.store(pos + Capacity, std::memory_order_release);
      return true;
    }

    return false;
  }

  std::size_t ApproxSize() const {
    const std::size_t tail = enqueue_pos_.load(std::memory_order_acquire);
    const std::size_t head = dequeue_pos_.load(std::memory_order_acquire);
    return tail - head;
  }

 private:
  struct alignas(64) Slot {
    std::atomic<std::size_t> sequence{};
    T value{};
  };

  template <typename U>
  bool Emplace(U&& value) {
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

    for (;;) {
      Slot& slot = slots_[pos & kMask];
      const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
      const intptr_t diff =
          static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

      if (diff == 0) {
        if (enqueue_pos_.compare_exchange_weak(
                pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
          slot.value = std::forward<U>(value);
          slot.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
        continue;
      }

      if (diff < 0) {
        return false;
      }

      pos = enqueue_pos_.load(std::memory_order_relaxed);
    }
  }

  static constexpr std::size_t kMask = Capacity - 1;

  std::array<Slot, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> enqueue_pos_{0};
  alignas(64) std::atomic<std::size_t> dequeue_pos_{0};
};

}  // namespace mir2::core::concurrency

#endif  // MIR2_CORE_CONCURRENCY_MPSC_RING_H_
