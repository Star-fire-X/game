/**
 * @file hot_event_pipeline.h
 * @brief Hot-path event ingest and MPSC queue pipeline.
 */

#ifndef MIR2_LOGIC_EVENTS_HOT_EVENT_PIPELINE_H_
#define MIR2_LOGIC_EVENTS_HOT_EVENT_PIPELINE_H_

#include <cstddef>
#include <cstdint>

#include "core/concurrency/mpsc_ring.h"
#include "logic/events/event_arena.h"
#include "logic/events/hot_event.h"
#include "logic/handler_context.h"

namespace mir2::logic::events {

class HotEventPipeline {
 public:
  static constexpr std::size_t kQueueCapacity = 1u << 16;

  enum class EnqueueResult : uint8_t {
    kBypass = 0,
    kEnqueued = 1,
    kQueueFull = 2,
    kInvalidPayload = 3,
    kPayloadTooLarge = 4,
    kArenaExhausted = 5,
  };

  HotEventPipeline() = default;

  void InitializeFromEnv();
  void ForceEnable() noexcept { enabled_ = true; }
  bool enabled() const { return enabled_; }

  EnqueueResult TryEnqueue(const HandlerContext& ctx,
                           uint16_t msg_id,
                           const uint8_t* payload,
                           std::size_t payload_size);

  bool TryDequeue(HotEvent* out_event);
  bool TryReadVarPayload(const HotEvent& event,
                         const uint8_t** out_data,
                         uint32_t* out_size) const;
  void ReleaseVarPayload(const HotEvent& event);
  void ObserveDrain(std::size_t drained);
  std::size_t ApproxSize() const;

 private:
  enum class BuildResult : uint8_t {
    kOk = 0,
    kInvalidPayload = 1,
    kPayloadTooLarge = 2,
    kArenaExhausted = 3,
  };

  BuildResult TryBuildHotEvent(const HandlerContext& ctx,
                               uint16_t msg_id,
                               const uint8_t* payload,
                               std::size_t payload_size,
                               HotEvent* out_event);

  bool enabled_ = false;
  EventArena arena_;
  core::concurrency::MpscRingQueue<HotEvent, kQueueCapacity> queue_;
};

}  // namespace mir2::logic::events

#endif  // MIR2_LOGIC_EVENTS_HOT_EVENT_PIPELINE_H_
