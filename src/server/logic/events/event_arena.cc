#include "logic/events/event_arena.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace mir2::logic::events {
namespace {

struct ThreadBinding {
  const EventArena* arena = nullptr;
  uint16_t producer_id = EventArena::kInvalidProducerId;
};

thread_local ThreadBinding g_thread_binding;

}  // namespace

EventArena::EventArena() : EventArena(Config{}) {}

EventArena::EventArena(Config config) : config_(config) {
  if (config_.max_producers == 0) {
    config_.max_producers = 1;
  }
  if (config_.total_blocks == 0) {
    config_.total_blocks = 64;
  }
  if (config_.block_size == 0) {
    config_.block_size = 4096;
  }

  blocks_.resize(config_.total_blocks);
  for (uint16_t i = 0; i < config_.total_blocks; ++i) {
    blocks_[i].bytes.resize(config_.block_size);
    global_free_blocks_.push_back(i);
  }

  producer_states_.resize(config_.max_producers);
}

bool EventArena::Store(const uint8_t* data, uint32_t size, VarRef* out_ref) {
  if (!out_ref || !data || size == 0 || size > config_.block_size) {
    return false;
  }

  const uint16_t producer_id = ResolveProducerForCurrentThread();
  if (producer_id == kInvalidProducerId || producer_id >= producer_states_.size()) {
    return false;
  }

  auto& state = producer_states_[producer_id];
  DrainReturnedBlocks(&state);

  if (state.current_block == kInvalidBlockIndex ||
      state.current_offset + size > config_.block_size) {
    SealCurrentBlock(&state);
    if (!AcquireBlock(producer_id, &state, &state.current_block)) {
      return false;
    }
  }

  const uint16_t block_id = state.current_block;
  auto& block = blocks_[block_id];
  const uint32_t offset = state.current_offset;

  std::memcpy(block.bytes.data() + offset, data, size);
  block.ref_count.fetch_add(1, std::memory_order_release);

  out_ref->producer_id = producer_id;
  out_ref->block_index = block_id;
  out_ref->offset = offset;
  out_ref->length = size;
  out_ref->generation = block.generation.load(std::memory_order_acquire);

  state.current_offset += size;
  return true;
}

bool EventArena::Load(const VarRef& ref,
                      const uint8_t** out_data,
                      uint32_t* out_size) const {
  if (!out_data || !out_size || !IsVarRefValid(ref)) {
    return false;
  }

  const auto& block = blocks_[ref.block_index];
  if (block.owner_producer.load(std::memory_order_acquire) != ref.producer_id) {
    return false;
  }
  if (block.generation.load(std::memory_order_acquire) != ref.generation) {
    return false;
  }
  if (ref.offset + ref.length > config_.block_size) {
    return false;
  }

  *out_data = block.bytes.data() + ref.offset;
  *out_size = ref.length;
  return true;
}

void EventArena::Release(const VarRef& ref) {
  if (!IsVarRefValid(ref)) {
    return;
  }

  auto& block = blocks_[ref.block_index];
  if (block.owner_producer.load(std::memory_order_acquire) != ref.producer_id) {
    return;
  }
  if (block.generation.load(std::memory_order_acquire) != ref.generation) {
    return;
  }

  const uint32_t prev = block.ref_count.fetch_sub(1, std::memory_order_acq_rel);
  if (prev == 0) {
    block.ref_count.store(0, std::memory_order_release);
    return;
  }
  if (prev != 1) {
    return;
  }

  if (block.active.load(std::memory_order_acquire)) {
    return;
  }

  RecycleBlockToProducer(ref.producer_id, ref.block_index);
}

uint16_t EventArena::ResolveProducerForCurrentThread() {
  if (g_thread_binding.arena == this &&
      g_thread_binding.producer_id != kInvalidProducerId) {
    return g_thread_binding.producer_id;
  }

  const uint16_t producer_id = RegisterProducerForCurrentThread();
  g_thread_binding.arena = this;
  g_thread_binding.producer_id = producer_id;
  return producer_id;
}

uint16_t EventArena::RegisterProducerForCurrentThread() {
  const uint16_t producer_id = next_producer_id_.fetch_add(1, std::memory_order_relaxed);
  if (producer_id >= config_.max_producers || producer_id >= producer_states_.size()) {
    return kInvalidProducerId;
  }

  producer_states_[producer_id].registered = true;
  return producer_id;
}

void EventArena::DrainReturnedBlocks(ProducerState* state) {
  if (!state) {
    return;
  }

  uint16_t block_id = kInvalidBlockIndex;
  while (state->return_queue.TryDequeue(&block_id)) {
    if (block_id != kInvalidBlockIndex) {
      state->local_free_blocks.push_back(block_id);
    }
  }
}

bool EventArena::AcquireBlock(uint16_t producer_id,
                              ProducerState* state,
                              uint16_t* out_block_id) {
  if (!state || !out_block_id) {
    return false;
  }

  if (!state->local_free_blocks.empty()) {
    *out_block_id = state->local_free_blocks.back();
    state->local_free_blocks.pop_back();
  } else {
    std::lock_guard<std::mutex> lock(global_free_mutex_);
    if (global_free_blocks_.empty()) {
      return false;
    }
    *out_block_id = global_free_blocks_.back();
    global_free_blocks_.pop_back();
  }

  auto& block = blocks_[*out_block_id];
  block.owner_producer.store(producer_id, std::memory_order_release);
  block.active.store(true, std::memory_order_release);
  block.ref_count.store(0, std::memory_order_release);
  block.generation.fetch_add(1, std::memory_order_acq_rel);
  state->current_offset = 0;
  return true;
}

void EventArena::SealCurrentBlock(ProducerState* state) {
  if (!state || state->current_block == kInvalidBlockIndex) {
    return;
  }

  const uint16_t block_id = state->current_block;
  auto& block = blocks_[block_id];
  block.active.store(false, std::memory_order_release);

  if (block.ref_count.load(std::memory_order_acquire) == 0) {
    state->local_free_blocks.push_back(block_id);
  }

  state->current_block = kInvalidBlockIndex;
  state->current_offset = 0;
}

void EventArena::RecycleBlockToProducer(uint16_t producer_id, uint16_t block_id) {
  if (producer_id < producer_states_.size() && producer_states_[producer_id].registered) {
    if (producer_states_[producer_id].return_queue.TryEnqueue(block_id)) {
      return;
    }
  }

  auto& block = blocks_[block_id];
  block.owner_producer.store(kInvalidProducerId, std::memory_order_release);
  block.active.store(false, std::memory_order_release);

  std::lock_guard<std::mutex> lock(global_free_mutex_);
  global_free_blocks_.push_back(block_id);
}

bool EventArena::IsVarRefValid(const VarRef& ref) const {
  if (ref.producer_id == kInvalidProducerId ||
      ref.block_index == kInvalidBlockIndex) {
    return false;
  }
  if (ref.producer_id >= producer_states_.size() ||
      ref.block_index >= blocks_.size()) {
    return false;
  }
  if (ref.length == 0 || ref.offset + ref.length > config_.block_size) {
    return false;
  }
  return true;
}

}  // namespace mir2::logic::events
