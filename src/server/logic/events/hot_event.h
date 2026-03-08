/**
 * @file hot_event.h
 * @brief Fixed-size hot-path event definition.
 */

#ifndef MIR2_LOGIC_EVENTS_HOT_EVENT_H_
#define MIR2_LOGIC_EVENTS_HOT_EVENT_H_

#include <cstdint>
#include <type_traits>

#include "logic/events/var_ref.h"

namespace mir2::logic::events {

enum class HotEventType : uint8_t {
  kUnknown = 0,
  kMove = 1,
  kAttack = 2,
  kSkill = 3,
  kHeartbeat = 4,
  kChat = 5,
  kGeneric = 6,
};

enum class HotEventPriority : uint8_t {
  kNormal = 0,
  kCritical = 1,
  kBestEffort = 2,
};

struct MoveHotData {
  int32_t target_x = 0;
  int32_t target_y = 0;
};

struct AttackHotData {
  uint64_t target_id = 0;
  uint16_t target_type = 0;
  uint16_t reserved0 = 0;
  uint32_t reserved1 = 0;
};

struct SkillHotData {
  uint64_t target_id = 0;
  uint32_t skill_id = 0;
  uint32_t reserved = 0;
};

struct HeartbeatHotData {
  uint32_t seq = 0;
  uint32_t client_time = 0;
  uint32_t reserved0 = 0;
  uint32_t reserved1 = 0;
};

union HotEventData {
  MoveHotData move;
  AttackHotData attack;
  SkillHotData skill;
  HeartbeatHotData heartbeat;

  constexpr HotEventData() : heartbeat() {}
};

// Keep one cache line to minimize producer/consumer miss penalty.
struct alignas(64) HotEvent {
  uint64_t client_id = 0;
  uint32_t entity_id = 0;
  uint32_t entity_version = 0;
  uint16_t msg_id = 0;
  HotEventType type = HotEventType::kUnknown;
  uint8_t flags = 0;
  uint64_t enqueue_ts_us = 0;
  HotEventData data;
  VarRef var_ref;
};

constexpr uint8_t kHotEventPriorityMask = 0x03;

inline void SetHotEventPriority(HotEvent* event, HotEventPriority priority) {
  if (!event) {
    return;
  }
  event->flags = static_cast<uint8_t>(
      (event->flags & ~kHotEventPriorityMask) |
      (static_cast<uint8_t>(priority) & kHotEventPriorityMask));
}

inline HotEventPriority GetHotEventPriority(const HotEvent& event) {
  switch (event.flags & kHotEventPriorityMask) {
    case static_cast<uint8_t>(HotEventPriority::kCritical):
      return HotEventPriority::kCritical;
    case static_cast<uint8_t>(HotEventPriority::kBestEffort):
      return HotEventPriority::kBestEffort;
    case static_cast<uint8_t>(HotEventPriority::kNormal):
    default:
      return HotEventPriority::kNormal;
  }
}

static_assert(sizeof(HotEventData) == 16, "HotEventData must stay compact.");
static_assert(sizeof(HotEvent) == 64, "HotEvent must fit in one cache line.");
static_assert(alignof(HotEvent) == 64, "HotEvent must be cache-line aligned.");
static_assert(std::is_trivially_copyable_v<HotEvent>,
              "HotEvent must remain trivially copyable.");

}  // namespace mir2::logic::events

#endif  // MIR2_LOGIC_EVENTS_HOT_EVENT_H_
