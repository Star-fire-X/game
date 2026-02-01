#include "security/anti_cheat.h"

#include <cmath>

#include "core/utils.h"

namespace mir2::security {

AntiCheat& AntiCheat::Instance() {
  static AntiCheat instance;
  return instance;
}

bool AntiCheat::ValidateMove(uint64_t player_id, int32_t from_x, int32_t from_y,
                             int32_t to_x, int32_t to_y, int64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& record = records_[player_id];
  (void)from_x;
  (void)from_y;

  if (record.last_move_time > 0) {
    const float dx = static_cast<float>(to_x - record.last_x);
    const float dy = static_cast<float>(to_y - record.last_y);
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float delta_time = static_cast<float>(timestamp_ms - record.last_move_time) / 1000.0f;

    if (delta_time > 0.0f) {
      const float speed = distance / delta_time;
      if (speed > kMaxMoveSpeed) {
        ProcessViolationLocked(record, CheatType::kSpeedHack, 10, timestamp_ms);
        return false;
      }
    }
  }

  record.last_move_time = timestamp_ms;
  record.last_x = to_x;
  record.last_y = to_y;
  return true;
}

bool AntiCheat::ValidateAttack(uint64_t player_id, uint64_t /*target_id*/,
                               int32_t /*damage*/, int64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& record = records_[player_id];

  constexpr int64_t kMinAttackIntervalMs = 200;
  if (record.last_attack_time > 0 &&
      (timestamp_ms - record.last_attack_time) < kMinAttackIntervalMs) {
    ProcessViolationLocked(record, CheatType::kAttackSpeed, 5, timestamp_ms);
    return false;
  }

  record.last_attack_time = timestamp_ms;
  return true;
}

bool AntiCheat::CheckMessageRate(uint64_t player_id, uint16_t /*msg_id*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& record = records_[player_id];

  const int64_t now_ms = core::GetCurrentTimestampMs();
  constexpr int64_t kMinMessageIntervalMs = 30;
  if (record.last_message_time > 0) {
    const int64_t delta = now_ms - record.last_message_time;
    if (delta < kMinMessageIntervalMs) {
      ProcessViolationLocked(record, CheatType::kPacketFlood, 1, now_ms);
      return false;
    }
  }
  record.last_message_time = now_ms;
  return true;
}

void AntiCheat::ProcessViolation(uint64_t player_id, CheatType type, int severity) {
  ProcessViolation(player_id, type, severity,
                   core::GetCurrentTimestampMs());
}

void AntiCheat::ClearPlayerRecord(uint64_t player_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  records_.erase(player_id);
}

void AntiCheat::ProcessViolation(uint64_t player_id,
                                 CheatType type,
                                 int severity,
                                 int64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& record = records_[player_id];
  ProcessViolationLocked(record, type, severity, timestamp_ms);
}

int AntiCheat::GetViolationScore(uint64_t player_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = records_.find(player_id);
  if (it == records_.end()) {
    return 0;
  }
  return it->second.violation_score;
}

int AntiCheat::GetViolationCount(uint64_t player_id, CheatType type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = records_.find(player_id);
  if (it == records_.end()) {
    return 0;
  }
  const size_t index = CheatTypeIndex(type);
  if (index >= it->second.violation_counts.size()) {
    return 0;
  }
  return it->second.violation_counts[index];
}

size_t AntiCheat::CheatTypeIndex(CheatType type) {
  switch (type) {
    case CheatType::kSpeedHack:
      return 0;
    case CheatType::kTeleportHack:
      return 1;
    case CheatType::kDamageHack:
      return 2;
    case CheatType::kAttackSpeed:
      return 3;
    case CheatType::kPacketFlood:
      return 4;
    default:
      return 0;
  }
}

void AntiCheat::ProcessViolationLocked(PlayerRecord& record,
                                       CheatType type,
                                       int severity,
                                       int64_t timestamp_ms) {
  record.violation_score += severity;
  if (record.violation_score >= kBanThreshold) {
    record.violation_score = kBanThreshold;
  }
  const size_t index = CheatTypeIndex(type);
  if (index < record.violation_counts.size()) {
    record.violation_counts[index] += 1;
  }
  record.violations.push_back({type, severity, timestamp_ms});
  if (record.violations.size() > 100) {
    record.violations.erase(record.violations.begin());
  }
}

}  // namespace mir2::security
