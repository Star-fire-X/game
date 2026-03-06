#include "network/conv_blacklist.h"

#include <limits>

namespace mir2::network {

ConvBlacklist::ConvBlacklist() : config_({}) {}
ConvBlacklist::ConvBlacklist(Config config) : config_(config) {}

void ConvBlacklist::SetConfig(Config config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
  blacklist_.clear();
  failures_.clear();
  last_cleanup_ms_ = 0;
}

bool ConvBlacklist::IsBlacklisted(uint32_t conv, uint32_t source_ip, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Key key{.conv = conv, .source_ip = source_ip};

  auto it = blacklist_.find(key);
  if (it == blacklist_.end()) {
    return false;
  }

  if (now_ms >= it->second) {
    blacklist_.erase(it);
    return false;
  }

  return true;
}

void ConvBlacklist::RecordFailure(uint32_t conv, uint32_t source_ip, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Key key{.conv = conv, .source_ip = source_ip};

  auto& entry = failures_[key];
  if (entry.count < std::numeric_limits<uint8_t>::max()) {
    ++entry.count;
  }
  entry.last_fail_ms = now_ms;

  if (entry.count >= config_.max_failures) {
    blacklist_[key] = now_ms + config_.blacklist_ttl_ms;
    failures_.erase(key);
  }
}

void ConvBlacklist::Add(uint32_t conv, uint32_t source_ip, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Key key{.conv = conv, .source_ip = source_ip};
  blacklist_[key] = now_ms + config_.blacklist_ttl_ms;
  failures_.erase(key);
}

void ConvBlacklist::Remove(uint32_t conv, uint32_t source_ip) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Key key{.conv = conv, .source_ip = source_ip};
  blacklist_.erase(key);
  failures_.erase(key);
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
