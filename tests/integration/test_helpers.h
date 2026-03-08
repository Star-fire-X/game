#ifndef MIR2_TESTS_INTEGRATION_TEST_HELPERS_H_
#define MIR2_TESTS_INTEGRATION_TEST_HELPERS_H_

#include <asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "common/network/i_channel.h"

namespace mir2::test::integration {

class PerformanceMonitor {
 public:
  // Basic latency/RRT tracker for integration tests.
  struct Stats {
    size_t count = 0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double avg_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
  };

  void RecordRtt(std::chrono::nanoseconds rtt) {
    RecordSample(rtt_samples_, rtt);
  }

  void RecordLatency(std::chrono::nanoseconds latency) {
    RecordSample(latency_samples_, latency);
  }

  void RecordSent(size_t count = 1) {
    sent_count_.fetch_add(count, std::memory_order_relaxed);
  }

  void RecordReceived(size_t count = 1) {
    received_count_.fetch_add(count, std::memory_order_relaxed);
  }

  void Reset() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      rtt_samples_.clear();
      latency_samples_.clear();
    }
    sent_count_.store(0, std::memory_order_relaxed);
    received_count_.store(0, std::memory_order_relaxed);
  }

  Stats GetRttStats() const {
    std::vector<double> samples;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      samples = rtt_samples_;
    }
    return BuildStats(samples);
  }

  Stats GetLatencyStats() const {
    std::vector<double> samples;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      samples = latency_samples_;
    }
    return BuildStats(samples);
  }

  double GetPacketLossRate() const {
    const double sent = static_cast<double>(sent_count_.load(std::memory_order_relaxed));
    const double received = static_cast<double>(received_count_.load(std::memory_order_relaxed));
    if (sent <= 0.0) {
      return 0.0;
    }
    const double loss = (sent - received) / sent;
    return loss < 0.0 ? 0.0 : loss;
  }

  uint64_t sent_count() const {
    return sent_count_.load(std::memory_order_relaxed);
  }

  uint64_t received_count() const {
    return received_count_.load(std::memory_order_relaxed);
  }

 private:
  void RecordSample(std::vector<double>& samples, std::chrono::nanoseconds value) {
    const double ms = std::chrono::duration<double, std::milli>(value).count();
    std::lock_guard<std::mutex> lock(mutex_);
    samples.push_back(ms);
  }

  static double Percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) {
      return 0.0;
    }
    const double index = p * (static_cast<double>(sorted.size() - 1));
    const size_t lo = static_cast<size_t>(index);
    const size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = index - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
  }

  static Stats BuildStats(std::vector<double> samples) {
    Stats stats;
    stats.count = samples.size();
    if (samples.empty()) {
      return stats;
    }

    std::sort(samples.begin(), samples.end());
    stats.min_ms = samples.front();
    stats.max_ms = samples.back();
    double sum = 0.0;
    for (double value : samples) {
      sum += value;
    }
    stats.avg_ms = sum / static_cast<double>(samples.size());
    stats.p50_ms = Percentile(samples, 0.50);
    stats.p95_ms = Percentile(samples, 0.95);
    stats.p99_ms = Percentile(samples, 0.99);
    return stats;
  }

  mutable std::mutex mutex_;
  std::vector<double> rtt_samples_;
  std::vector<double> latency_samples_;
  std::atomic<uint64_t> sent_count_{0};
  std::atomic<uint64_t> received_count_{0};
};

class NetworkSimulator {
 public:
  // Simulates loss, latency, and simple out-of-order delivery.
  struct Config {
    double loss_rate = 0.0;
    double reorder_rate = 0.0;
    std::chrono::milliseconds min_delay{0};
    std::chrono::milliseconds max_delay{0};
    std::chrono::milliseconds reorder_extra_delay{5};
    uint32_t seed = std::random_device{}();
  };

  explicit NetworkSimulator(asio::io_context& io_context)
      : NetworkSimulator(io_context, Config()) {}

  explicit NetworkSimulator(asio::io_context& io_context, Config config)
      : io_context_(io_context), config_(std::move(config)), rng_(config_.seed) {}

  void SetLossRate(double rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.loss_rate = Clamp01(rate);
  }

  void SetReorderRate(double rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.reorder_rate = Clamp01(rate);
  }

  void SetLatency(std::chrono::milliseconds min_delay,
                  std::chrono::milliseconds max_delay) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.min_delay = min_delay;
    config_.max_delay = max_delay;
    if (config_.max_delay < config_.min_delay) {
      config_.max_delay = config_.min_delay;
    }
  }

  void SetReorderExtraDelay(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.reorder_extra_delay = delay;
  }

  Config config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
  }

  void Flush() {
    std::optional<Pending> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (reorder_pending_) {
        pending = std::move(reorder_pending_);
        reorder_pending_.reset();
      }
    }
    if (pending) {
      Schedule(*pending);
    }
  }

  void Send(std::function<void()> deliver) {
    if (!deliver) {
      return;
    }

    Pending current;
    current.deliver = std::move(deliver);

    std::optional<Pending> delayed;
    std::optional<Pending> postponed;
    std::chrono::milliseconds extra_delay{0};

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (ShouldDropLocked()) {
        return;
      }

      current.delay = ComputeDelayLocked();

      extra_delay = config_.reorder_extra_delay;

      if (reorder_pending_) {
        delayed = std::move(current);
        postponed = std::move(reorder_pending_);
        reorder_pending_.reset();
      } else if (ShouldReorderLocked()) {
        reorder_pending_ = std::move(current);
        return;
      } else {
        delayed = std::move(current);
      }
    }

    if (delayed) {
      Schedule(*delayed);
    }
    if (postponed) {
      postponed->delay += extra_delay;
      Schedule(*postponed);
    }
  }

  template <typename Payload, typename Deliver>
  void Send(Payload payload, Deliver deliver) {
    Send([payload = std::move(payload), deliver = std::move(deliver)]() mutable {
      deliver(std::move(payload));
    });
  }

 private:
  struct Pending {
    std::function<void()> deliver;
    std::chrono::milliseconds delay{0};
  };

  static double Clamp01(double value) {
    if (value < 0.0) {
      return 0.0;
    }
    if (value > 1.0) {
      return 1.0;
    }
    return value;
  }

  bool ShouldDropLocked() {
    if (config_.loss_rate <= 0.0) {
      return false;
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng_) < config_.loss_rate;
  }

  bool ShouldReorderLocked() {
    if (config_.reorder_rate <= 0.0) {
      return false;
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng_) < config_.reorder_rate;
  }

  std::chrono::milliseconds ComputeDelayLocked() {
    if (config_.max_delay <= config_.min_delay) {
      return config_.min_delay;
    }
    const int64_t min_ms = config_.min_delay.count();
    const int64_t max_ms = config_.max_delay.count();
    std::uniform_int_distribution<int64_t> dist(min_ms, max_ms);
    return std::chrono::milliseconds(dist(rng_));
  }

  void Schedule(const Pending& pending) {
    auto timer = std::make_shared<asio::steady_timer>(io_context_, pending.delay);
    timer->async_wait([deliver = pending.deliver, timer](const asio::error_code& ec) {
      if (!ec && deliver) {
        deliver();
      }
    });
  }

  asio::io_context& io_context_;
  mutable std::mutex mutex_;
  Config config_;
  std::mt19937 rng_;
  std::optional<Pending> reorder_pending_;
};

class MessageCapture {
 public:
  // Thread-safe capture queue for inbound/outbound messages.
  struct CapturedMessage {
    mir2::common::ChannelType channel = mir2::common::ChannelType::kTcp;
    uint16_t msg_id = 0;
    std::vector<uint8_t> payload;
    std::chrono::steady_clock::time_point timestamp;
  };

  void Capture(mir2::common::ChannelType channel,
               uint16_t msg_id,
               const std::vector<uint8_t>& payload) {
    CapturedMessage entry;
    entry.channel = channel;
    entry.msg_id = msg_id;
    entry.payload = payload;
    entry.timestamp = std::chrono::steady_clock::now();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      messages_.push_back(std::move(entry));
    }
    cv_.notify_all();
  }

  std::optional<CapturedMessage> WaitForMessage(
      uint16_t msg_id,
      std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
      auto it = std::find_if(messages_.begin(), messages_.end(),
                             [msg_id](const CapturedMessage& msg) {
                               return msg.msg_id == msg_id;
                             });
      if (it != messages_.end()) {
        CapturedMessage result = std::move(*it);
        messages_.erase(it);
        return result;
      }

      if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
        return std::nullopt;
      }
    }
  }

  std::vector<CapturedMessage> Drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CapturedMessage> drained(messages_.begin(), messages_.end());
    messages_.clear();
    return drained;
  }

  size_t Count(uint16_t msg_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<size_t>(std::count_if(messages_.begin(), messages_.end(),
                                             [msg_id](const CapturedMessage& msg) {
                                               return msg.msg_id == msg_id;
                                             }));
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<CapturedMessage> messages_;
};

template <typename Predicate, typename Rep, typename Period,
          typename PollRep, typename PollPeriod>
// Repeatedly evaluates predicate until timeout, optionally ticking between polls.
bool WaitForCondition(Predicate predicate,
                      std::chrono::duration<Rep, Period> timeout,
                      std::chrono::duration<PollRep, PollPeriod> poll_interval,
                      const std::function<void()>& tick = {}) {
  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + timeout;

  while (std::chrono::steady_clock::now() <= deadline) {
    if (predicate()) {
      return true;
    }
    if (tick) {
      tick();
    }
    std::this_thread::sleep_for(poll_interval);
  }
  return predicate();
}

template <typename Predicate, typename Rep, typename Period>
bool WaitForCondition(Predicate predicate,
                      std::chrono::duration<Rep, Period> timeout,
                      const std::function<void()>& tick = {}) {
  return WaitForCondition(predicate, timeout, std::chrono::milliseconds(5), tick);
}

}  // namespace mir2::test::integration

#endif  // MIR2_TESTS_INTEGRATION_TEST_HELPERS_H_
