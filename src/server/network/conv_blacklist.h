/**
 * @file conv_blacklist.h
 * @brief Conv blacklist for KCP sessions.
 */

#ifndef MIR2_NETWORK_CONV_BLACKLIST_H_
#define MIR2_NETWORK_CONV_BLACKLIST_H_

#include <cstddef>
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

  void SetConfig(Config config);

  bool IsBlacklisted(uint32_t conv, uint32_t source_ip, int64_t now_ms);
  void RecordFailure(uint32_t conv, uint32_t source_ip, int64_t now_ms);
  void Add(uint32_t conv, uint32_t source_ip, int64_t now_ms);
  void Remove(uint32_t conv, uint32_t source_ip);
  void Cleanup(int64_t now_ms);

 private:
  struct Key {
    uint32_t conv = 0;
    uint32_t source_ip = 0;

    bool operator==(const Key& other) const {
      return conv == other.conv && source_ip == other.source_ip;
    }
  };

  struct KeyHash {
    size_t operator()(const Key& key) const {
      return (static_cast<size_t>(key.conv) << 32) ^
             static_cast<size_t>(key.source_ip);
    }
  };

  struct FailureEntry {
    uint8_t count = 0;
    int64_t last_fail_ms = 0;
  };

  Config config_;
  std::unordered_map<Key, int64_t, KeyHash> blacklist_;
  std::unordered_map<Key, FailureEntry, KeyHash> failures_;
  std::mutex mutex_;
  int64_t last_cleanup_ms_ = 0;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_CONV_BLACKLIST_H_
