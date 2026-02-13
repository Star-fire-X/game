#include "logic/events/hot_event_pipeline.h"

#include <chrono>
#include <cstdlib>
#include <string_view>
#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "combat_generated.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "log/logger.h"
#include "monitor/metrics.h"
#include "system_generated.h"

namespace mir2::logic::events {
namespace {

constexpr const char* kEnableEnv = "LEGEND2_HOT_EVENT_PIPELINE";
constexpr const char* kMetricEnqueueTotal = "logic.hot_event.enqueue_total";
constexpr const char* kMetricEnqueueDropped = "logic.hot_event.enqueue_dropped_total";
constexpr const char* kMetricDrainTotal = "logic.hot_event.drain_total";
constexpr const char* kMetricQueueDepth = "logic.hot_event.queue_depth";
constexpr const char* kMetricQueueFullDrop = "logic.hot_event.queue_full_drop_total";
constexpr const char* kMetricPayloadTooLargeDrop =
    "logic.hot_event.payload_too_large_drop_total";
constexpr const char* kMetricArenaExhaustedTotal =
    "logic.hot_event.arena_exhausted_total";
constexpr const char* kMetricBypassTotal = "logic.hot_event.bypass_total";
constexpr uint32_t kMaxChatPayloadBytes = 1024;
constexpr uint32_t kMaxGenericPayloadBytes = 1024;

bool IsTruthy(std::string_view value) {
  return value == "1" || value == "true" || value == "TRUE" ||
         value == "on" || value == "ON";
}

HotEventPriority ClassifyMsgPriority(common::MsgId id) {
  switch (id) {
    case common::MsgId::kLoginReq:
    case common::MsgId::kLogout:
    case common::MsgId::kCreateRoleReq:
    case common::MsgId::kSelectRoleReq:
    case common::MsgId::kRoleListReq:
    case common::MsgId::kMoveReq:
    case common::MsgId::kAttackReq:
    case common::MsgId::kSkillReq:
    case common::MsgId::kUseItemReq:
    case common::MsgId::kDropItemReq:
    case common::MsgId::kPickupItemReq:
    case common::MsgId::kEquipReq:
    case common::MsgId::kUnequipReq:
    case common::MsgId::kNpcInteractReq:
    case common::MsgId::kNpcMenuSelect:
      return HotEventPriority::kCritical;
    case common::MsgId::kHeartbeat:
    case common::MsgId::kChatReq:
      return HotEventPriority::kBestEffort;
    default:
      return HotEventPriority::kNormal;
  }
}

uint64_t NowMicroseconds() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}  // namespace

void HotEventPipeline::InitializeFromEnv() {
  const char* value = std::getenv(kEnableEnv);
  enabled_ = (value == nullptr) ? true : IsTruthy(value);
  SYSLOG_INFO("HotEventPipeline {} (env {}={})",
              enabled_ ? "enabled" : "disabled",
              kEnableEnv,
              value != nullptr ? value : "<unset>");
}

HotEventPipeline::EnqueueResult HotEventPipeline::TryEnqueue(
    const HandlerContext& ctx,
    uint16_t msg_id,
    const uint8_t* payload,
    std::size_t payload_size) {
  if (!enabled_) {
    monitor::Metrics::Instance().IncrementCounter(kMetricBypassTotal);
    return EnqueueResult::kBypass;
  }

  HotEvent event{};
  switch (TryBuildHotEvent(ctx, msg_id, payload, payload_size, &event)) {
    case BuildResult::kOk:
      break;
    case BuildResult::kInvalidPayload:
      return EnqueueResult::kInvalidPayload;
    case BuildResult::kPayloadTooLarge:
      monitor::Metrics::Instance().IncrementCounter(kMetricEnqueueDropped);
      monitor::Metrics::Instance().IncrementCounter(kMetricPayloadTooLargeDrop);
      return EnqueueResult::kPayloadTooLarge;
    case BuildResult::kArenaExhausted:
      monitor::Metrics::Instance().IncrementCounter(kMetricArenaExhaustedTotal);
      return EnqueueResult::kArenaExhausted;
  }

  if (!queue_.TryEnqueue(event)) {
    if (event.var_ref.length > 0) {
      arena_.Release(event.var_ref);
    }
    monitor::Metrics::Instance().IncrementCounter(kMetricEnqueueDropped);
    monitor::Metrics::Instance().IncrementCounter(kMetricQueueFullDrop);
    return EnqueueResult::kQueueFull;
  }

  monitor::Metrics::Instance().IncrementCounter(kMetricEnqueueTotal);
  return EnqueueResult::kEnqueued;
}

bool HotEventPipeline::TryDequeue(HotEvent* out_event) {
  if (!enabled_ || !out_event) {
    return false;
  }
  return queue_.TryDequeue(out_event);
}

bool HotEventPipeline::TryReadVarPayload(const HotEvent& event,
                                         const uint8_t** out_data,
                                         uint32_t* out_size) const {
  if (event.var_ref.length == 0) {
    return false;
  }
  return arena_.Load(event.var_ref, out_data, out_size);
}

void HotEventPipeline::ReleaseVarPayload(const HotEvent& event) {
  if (event.var_ref.length == 0) {
    return;
  }
  arena_.Release(event.var_ref);
}

void HotEventPipeline::ObserveDrain(std::size_t drained) {
  if (!enabled_) {
    return;
  }
  if (drained > 0) {
    monitor::Metrics::Instance().IncrementCounter(
        kMetricDrainTotal, static_cast<uint64_t>(drained));
  }
  monitor::Metrics::Instance().SetGauge(
      kMetricQueueDepth, static_cast<double>(queue_.ApproxSize()));
}

std::size_t HotEventPipeline::ApproxSize() const {
  if (!enabled_) {
    return 0;
  }
  return queue_.ApproxSize();
}

HotEventPipeline::BuildResult HotEventPipeline::TryBuildHotEvent(
    const HandlerContext& ctx,
    uint16_t msg_id,
    const uint8_t* payload,
    std::size_t payload_size,
    HotEvent* out_event) {
  if (!out_event) {
    return BuildResult::kInvalidPayload;
  }

  out_event->client_id = ctx.client_id;
  out_event->msg_id = msg_id;
  if (ctx.entity != entt::null) {
    out_event->entity_id =
        static_cast<uint32_t>(static_cast<entt::id_type>(ctx.entity));
  }
  out_event->entity_version = ctx.entity_version;
  out_event->enqueue_ts_us = NowMicroseconds();
  out_event->var_ref = VarRef{};

  const auto id = static_cast<common::MsgId>(msg_id);
  SetHotEventPriority(out_event, ClassifyMsgPriority(id));
  switch (id) {
    case common::MsgId::kMoveReq: {
      common::MoveRequest request;
      const auto status = common::DecodeMoveRequest(
          common::kMoveRequestMsgId, payload, payload_size, &request);
      if (status != common::MessageCodecStatus::kOk) {
        return BuildResult::kInvalidPayload;
      }
      out_event->type = HotEventType::kMove;
      out_event->data.move.target_x = request.target_x;
      out_event->data.move.target_y = request.target_y;
      return BuildResult::kOk;
    }
    case common::MsgId::kAttackReq: {
      common::AttackRequest request;
      const auto status = common::DecodeAttackRequest(
          common::kAttackRequestMsgId, payload, payload_size, &request);
      if (status != common::MessageCodecStatus::kOk) {
        return BuildResult::kInvalidPayload;
      }
      out_event->type = HotEventType::kAttack;
      out_event->data.attack.target_id = request.target_id;
      out_event->data.attack.target_type =
          static_cast<uint16_t>(request.target_type);
      return BuildResult::kOk;
    }
    case common::MsgId::kSkillReq: {
      if (!payload || payload_size == 0) {
        return BuildResult::kInvalidPayload;
      }
      flatbuffers::Verifier verifier(payload, payload_size);
      if (!verifier.VerifyBuffer<mir2::proto::SkillReq>(nullptr)) {
        return BuildResult::kInvalidPayload;
      }
      const auto* req = flatbuffers::GetRoot<mir2::proto::SkillReq>(payload);
      if (!req) {
        return BuildResult::kInvalidPayload;
      }
      out_event->type = HotEventType::kSkill;
      out_event->data.skill.target_id = req->target_id();
      out_event->data.skill.skill_id = req->skill_id();
      return BuildResult::kOk;
    }
    case common::MsgId::kHeartbeat: {
      if (payload && payload_size > 0) {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (verifier.VerifyBuffer<mir2::proto::Heartbeat>(nullptr)) {
          const auto* hb = flatbuffers::GetRoot<mir2::proto::Heartbeat>(payload);
          if (hb) {
            out_event->data.heartbeat.seq = hb->seq();
            out_event->data.heartbeat.client_time = hb->client_time();
          }
        }
      }
      out_event->type = HotEventType::kHeartbeat;
      return BuildResult::kOk;
    }
    case common::MsgId::kChatReq: {
      if (!payload || payload_size == 0) {
        return BuildResult::kInvalidPayload;
      }
      if (payload_size > kMaxChatPayloadBytes) {
        return BuildResult::kPayloadTooLarge;
      }
      flatbuffers::Verifier verifier(payload, payload_size);
      if (!verifier.VerifyBuffer<mir2::proto::ChatReq>(nullptr)) {
        return BuildResult::kInvalidPayload;
      }
      VarRef ref{};
      if (!arena_.Store(payload, static_cast<uint32_t>(payload_size), &ref)) {
        return BuildResult::kArenaExhausted;
      }
      out_event->type = HotEventType::kChat;
      out_event->var_ref = ref;
      return BuildResult::kOk;
    }
    default:
      out_event->type = HotEventType::kGeneric;
      if (!payload || payload_size == 0) {
        return BuildResult::kOk;
      }
      if (payload_size > kMaxGenericPayloadBytes) {
        return BuildResult::kPayloadTooLarge;
      }
      VarRef ref{};
      if (!arena_.Store(payload, static_cast<uint32_t>(payload_size), &ref)) {
        return BuildResult::kArenaExhausted;
      }
      out_event->var_ref = ref;
      return BuildResult::kOk;
  }
}

}  // namespace mir2::logic::events
