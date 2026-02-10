/**
 * @file fallback_controller.h
 * @brief KCP fallback/recovery state controller.
 */

#ifndef MIR2_COMMON_NETWORK_FALLBACK_CONTROLLER_H_
#define MIR2_COMMON_NETWORK_FALLBACK_CONTROLLER_H_

#include <cstdint>
#include <mutex>

namespace mir2::common {

/**
 * @brief Tracks KCP availability and recovery timing.
 */
class FallbackController {
 public:
  enum class State : uint8_t {
    kNormal = 0,
    kFallback = 1,
    kRecovering = 2
  };

  struct Config {
    uint32_t handshake_timeout_ms = 5000;
    uint32_t recovery_interval_ms = 30000;
    uint32_t max_recovery_interval_ms = 300000;
  };

  FallbackController();
  explicit FallbackController(const Config& config);

  State GetState() const;
  bool IsKcpAllowed() const;
  bool IsHandshakeTimedOut(int64_t now_ms) const;
  bool ShouldAttemptRecovery(int64_t now_ms) const;

  void StartHandshake(int64_t now_ms);
  void OnRecoveryAttempt(int64_t now_ms);
  void OnKcpHandshakeSuccess();
  void OnKcpHandshakeTimeout(int64_t now_ms);
  void OnKcpDisconnect(int64_t now_ms);
  void Reset();

 private:
  void EnterFallback(int64_t now_ms);

  Config config_{};
  mutable std::mutex mutex_;
  State state_{State::kNormal};
  bool handshake_in_progress_{false};
  int64_t handshake_start_ms_{0};
  int64_t next_recovery_ms_{0};
  uint32_t backoff_ms_{0};
};

}  // namespace mir2::common

#endif  // MIR2_COMMON_NETWORK_FALLBACK_CONTROLLER_H_
