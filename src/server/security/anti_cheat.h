/**
 * @file anti_cheat.h
 * @brief 反作弊系统
 */

#ifndef MIR2_SECURITY_ANTI_CHEAT_H
#define MIR2_SECURITY_ANTI_CHEAT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mir2::security {

/**
 * @brief 作弊类型
 */
enum class CheatType {
  kSpeedHack,
  kTeleportHack,
  kDamageHack,
  kAttackSpeed,
  kPacketFlood
};

/**
 * @brief 反作弊系统
 */
class AntiCheat {
 public:
  static AntiCheat& Instance();

  bool ValidateMove(uint64_t player_id, int32_t from_x, int32_t from_y,
                    int32_t to_x, int32_t to_y, int64_t timestamp_ms);
  bool ValidateAttack(uint64_t player_id, uint64_t target_id,
                      int32_t damage, int64_t timestamp_ms);
  bool CheckMessageRate(uint64_t player_id, uint16_t msg_id);
  void ProcessViolation(uint64_t player_id, CheatType type, int severity);
  void ProcessViolation(uint64_t player_id, CheatType type, int severity, int64_t timestamp_ms);
  int GetViolationScore(uint64_t player_id) const;
  int GetViolationCount(uint64_t player_id, CheatType type) const;
  void ClearPlayerRecord(uint64_t player_id);

 private:
  AntiCheat() = default;

  struct ViolationEvent {
    CheatType type = CheatType::kSpeedHack;
    int severity = 0;
    int64_t timestamp_ms = 0;
  };

  struct PlayerRecord {
    int64_t last_move_time = 0;
    int32_t last_x = 0;
    int32_t last_y = 0;
    int64_t last_attack_time = 0;
    int64_t last_message_time = 0;
    int violation_score = 0;
    std::array<int, 5> violation_counts = {};
    std::vector<ViolationEvent> violations;
  };

  static size_t CheatTypeIndex(CheatType type);
  void ProcessViolationLocked(PlayerRecord& record,
                              CheatType type,
                              int severity,
                              int64_t timestamp_ms);

  std::unordered_map<uint64_t, PlayerRecord> records_;
  // Mutable so const query methods can lock safely.
  mutable std::mutex mutex_;

  static constexpr float kMaxMoveSpeed = 10.0f;
  static constexpr int kBanThreshold = 100;
};

}  // namespace mir2::security

#endif  // MIR2_SECURITY_ANTI_CHEAT_H
