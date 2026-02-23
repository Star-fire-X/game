#include "network/conv_blacklist.h"

#include <limits>

namespace mir2::network {

ConvBlacklist::ConvBlacklist() : config_({}) {}
ConvBlacklist::ConvBlacklist(Config config) : config_(config) {}

bool ConvBlacklist::IsBlacklisted(uint32_t conv, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = blacklist_.find(conv);
  if (it == blacklist_.end()) {
    return false;
  }

  if (now_ms >= it->second) {
    blacklist_.erase(it);
    return false;
  }

  return true;
}

void ConvBlacklist::RecordFailure(uint32_t conv, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto& entry = failures_[conv];
  if (entry.count < std::numeric_limits<uint8_t>::max()) {
    ++entry.count;
  }
  entry.last_fail_ms = now_ms;

  if (entry.count >= config_.max_failures) {
    blacklist_[conv] = now_ms + config_.blacklist_ttl_ms;
    failures_.erase(conv);
  }
}

void ConvBlacklist::Add(uint32_t conv, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  blacklist_[conv] = now_ms + config_.blacklist_ttl_ms;
  failures_.erase(conv);
}

void ConvBlacklist::Remove(uint32_t conv) {
  std::lock_guard<std::mutex> lock(mutex_);
  blacklist_.erase(conv);
  failures_.erase(conv);
}

void ConvBlacklist::Cleanup(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_cleanup_ms_ = now_ms;
  for (auto it = blacklist_.begin(); it != blacklist_.end();) {
    if (now_ms >= it->second) {
      it = blacklist_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = failures_.begin(); it != failures_.end();) {
    if (now_ms - it->second.last_fail_ms >= config_.failure_ttl_ms) {
      it = failures_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace mir2::network
