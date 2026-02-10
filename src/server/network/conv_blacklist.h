/**
 * @file conv_blacklist.h
 * @brief Conv blacklist for KCP sessions.
 */

#ifndef MIR2_NETWORK_CONV_BLACKLIST_H_
#define MIR2_NETWORK_CONV_BLACKLIST_H_

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace mir2::network {

/**
 * @brief Blacklist conv IDs after repeated auth failures.
 */
class ConvBlacklist {
 public:
  struct Config {
    uint8_t max_failures = 3;
    int64_t blacklist_ttl_ms = 5 * 60 * 1000;
    int64_t failure_ttl_ms = 5 * 60 * 1000;
    int64_t cleanup_interval_ms = 30000;
  };

  ConvBlacklist();
  explicit ConvBlacklist(Config config);

  bool IsBlacklisted(uint32_t conv, int64_t now_ms);
  void RecordFailure(uint32_t conv, int64_t now_ms);
  void Add(uint32_t conv, int64_t now_ms);
  void Remove(uint32_t conv);
  void Cleanup(int64_t now_ms);

 private:
  struct FailureEntry {
    uint8_t count = 0;
    int64_t last_fail_ms = 0;
  };

  Config config_;
  std::unordered_map<uint32_t, int64_t> blacklist_;
  std::unordered_map<uint32_t, FailureEntry> failures_;
  std::mutex mutex_;
  int64_t last_cleanup_ms_ = 0;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_CONV_BLACKLIST_H_
