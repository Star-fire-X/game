/**
 * @file event_arena.h
 * @brief TLS producer arena for variable-length hot events.
 */

#ifndef MIR2_LOGIC_EVENTS_EVENT_ARENA_H_
#define MIR2_LOGIC_EVENTS_EVENT_ARENA_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <vector>

#include "core/concurrency/spsc_ring.h"
#include "logic/events/var_ref.h"

namespace mir2::logic::events {

class EventArena {
 public:
  static constexpr uint16_t kInvalidProducerId = std::numeric_limits<uint16_t>::max();
  static constexpr uint16_t kInvalidBlockIndex = std::numeric_limits<uint16_t>::max();

  struct Config {
    uint16_t max_producers = 16;
    uint16_t total_blocks = 512;
    uint32_t block_size = 4096;
  };

  EventArena();
  explicit EventArena(Config config);

  EventArena(const EventArena&) = delete;
  EventArena& operator=(const EventArena&) = delete;

  bool Store(const uint8_t* data, uint32_t size, VarRef* out_ref);
  bool Load(const VarRef& ref, const uint8_t** out_data, uint32_t* out_size) const;
  void Release(const VarRef& ref);

  uint16_t max_producers() const { return config_.max_producers; }
  uint32_t block_size() const { return config_.block_size; }

 private:
  static constexpr size_t kReturnQueueCapacity = 1024;

  struct Block {
    std::vector<uint8_t> bytes;
    std::atomic<uint32_t> ref_count{0};
    std::atomic<uint32_t> generation{1};
    std::atomic<uint16_t> owner_producer{kInvalidProducerId};
    std::atomic<bool> active{false};
  };

  struct ProducerState {
    uint16_t current_block = kInvalidBlockIndex;
    uint32_t current_offset = 0;
    std::vector<uint16_t> local_free_blocks;
    core::concurrency::SpscRingQueue<uint16_t, kReturnQueueCapacity> return_queue;
    bool registered = false;
  };

  uint16_t ResolveProducerForCurrentThread();
  uint16_t RegisterProducerForCurrentThread();
  void DrainReturnedBlocks(ProducerState* state);
  bool AcquireBlock(uint16_t producer_id, ProducerState* state, uint16_t* out_block_id);
  void SealCurrentBlock(ProducerState* state);
  void RecycleBlockToProducer(uint16_t producer_id, uint16_t block_id);

  bool IsVarRefValid(const VarRef& ref) const;

  Config config_;
  std::deque<Block> blocks_;
  std::deque<ProducerState> producer_states_;

  std::atomic<uint16_t> next_producer_id_{0};

  mutable std::mutex global_free_mutex_;
  std::vector<uint16_t> global_free_blocks_;
};

}  // namespace mir2::logic::events

#endif  // MIR2_LOGIC_EVENTS_EVENT_ARENA_H_
