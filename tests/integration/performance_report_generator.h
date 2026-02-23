#ifndef MIR2_TESTS_INTEGRATION_PERFORMANCE_REPORT_GENERATOR_H_
#define MIR2_TESTS_INTEGRATION_PERFORMANCE_REPORT_GENERATOR_H_

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "integration/test_helpers.h"

namespace mir2::test::integration {

class PerformanceReportGenerator {
 public:
  struct MetricRecord {
    std::chrono::system_clock::time_point timestamp;
    std::string name;
    double value = 0.0;
    std::string unit;
  };

  struct StatsSummary {
    std::string name;
    PerformanceMonitor::Stats stats;
    std::string unit;
    std::optional<double> p95_target;
    std::optional<double> p99_target;
  };

  struct ThroughputSummary {
    std::string name;
    double msg_per_sec = 0.0;
    double mb_per_sec = 0.0;
    double target_msg_per_sec = 0.0;
  };

  struct ConcurrencySummary {
    std::string name;
    size_t clients = 0;
    double success_rate = 0.0;
    double cpu_percent = 0.0;
    double mem_mb = 0.0;
    double success_target = 0.0;
    double cpu_target = 0.0;
    double mem_target = 0.0;
  };

  static PerformanceReportGenerator& Instance() {
    static PerformanceReportGenerator instance;
    return instance;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.clear();
    stats_.clear();
    throughput_.clear();
    concurrency_.clear();
  }

  void AddStats(const std::string& name,
                const PerformanceMonitor::Stats& stats,
                const std::string& unit,
                std::optional<double> p95_target = std::nullopt,
                std::optional<double> p99_target = std::nullopt) {
    std::lock_guard<std::mutex> lock(mutex_);
    StatsSummary summary{name, stats, unit, p95_target, p99_target};
    stats_.push_back(summary);

    RecordMetricLocked(name + ".min", stats.min_ms, unit);
    RecordMetricLocked(name + ".max", stats.max_ms, unit);
    RecordMetricLocked(name + ".avg", stats.avg_ms, unit);
    RecordMetricLocked(name + ".p50", stats.p50_ms, unit);
    RecordMetricLocked(name + ".p95", stats.p95_ms, unit);
    RecordMetricLocked(name + ".p99", stats.p99_ms, unit);
  }

  void AddThroughput(const std::string& name,
                     double msg_per_sec,
                     double mb_per_sec,
                     double target_msg_per_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    ThroughputSummary summary{name, msg_per_sec, mb_per_sec, target_msg_per_sec};
    throughput_.push_back(summary);
    RecordMetricLocked(name + ".msg_per_sec", msg_per_sec, "msg/s");
    RecordMetricLocked(name + ".mb_per_sec", mb_per_sec, "MB/s");
  }

  void AddConcurrency(const std::string& name,
                      size_t clients,
                      double success_rate,
                      double cpu_percent,
                      double mem_mb,
                      double success_target,
                      double cpu_target,
                      double mem_target) {
    std::lock_guard<std::mutex> lock(mutex_);
    ConcurrencySummary summary{name,
                               clients,
                               success_rate,
                               cpu_percent,
                               mem_mb,
                               success_target,
                               cpu_target,
                               mem_target};
    concurrency_.push_back(summary);
    RecordMetricLocked(name + ".success_rate", success_rate, "ratio");
    RecordMetricLocked(name + ".cpu_percent", cpu_percent, "%");
    RecordMetricLocked(name + ".memory_mb", mem_mb, "MB");
  }

  bool HasData() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !(metrics_.empty() && stats_.empty() && throughput_.empty() && concurrency_.empty());
  }

  bool WriteCsv(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_.empty()) {
      return false;
    }

    if (!EnsureParentPath(path)) {
      return false;
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }

    out << "timestamp,metric_name,value,unit\n";
    for (const auto& metric : metrics_) {
      out << FormatTimestamp(metric.timestamp) << ','
          << metric.name << ','
          << metric.value << ','
          << metric.unit << '\n';
    }
    return true;
  }

  bool WriteMarkdown(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.empty() && throughput_.empty() && concurrency_.empty()) {
      return false;
    }

    if (!EnsureParentPath(path)) {
      return false;
    }

    std::ostringstream out;
    out << "# Stage 3 Performance Report\n\n";
    out << "Generated: " << FormatTimestamp(std::chrono::system_clock::now()) << "\n\n";

    out << "## PRD Success Metrics\n";
    out << "| Metric | Target | Measured | Pass |\n";
    out << "| --- | --- | --- | --- |\n";

    const StatsSummary* rtt = FindStats("RttBenchmark");
    AppendSuccessRow(out,
                     "移动同步 RTT (KCP)",
                     "p95 < 50ms, p99 < 80ms",
                     rtt,
                     50.0,
                     80.0);

    const StatsSummary* combat = FindStats("CombatLatencyBenchmark");
    AppendSuccessRow(out,
                     "战斗动画延迟",
                     "p95 < 80ms",
                     combat,
                     80.0,
                     std::nullopt);

    const StatsSummary* recovery = FindStats("PacketLossRecoveryBenchmark");
    AppendSuccessRow(out,
                     "丢包恢复时间",
                     "p95 < 100ms",
                     recovery,
                     100.0,
                     std::nullopt);

    if (!throughput_.empty()) {
      const auto& throughput = throughput_.front();
      const bool pass = throughput.msg_per_sec >= throughput.target_msg_per_sec;
      out << "| 吞吐量 | > " << throughput.target_msg_per_sec << " msg/s | "
          << throughput.msg_per_sec << " msg/s | " << (pass ? "PASS" : "FAIL")
          << " |\n";
    } else {
      out << "| 吞吐量 | > 1000 msg/s | N/A | N/A |\n";
    }

    if (!concurrency_.empty()) {
      const auto& concurrency = concurrency_.front();
      const bool pass_rate = concurrency.success_rate >= concurrency.success_target;
      out << "| 并发连接成功率 | > " << (concurrency.success_target * 100.0)
          << "% (1000 connections) | " << (concurrency.success_rate * 100.0)
          << "% | " << (pass_rate ? "PASS" : "FAIL") << " |\n";
      const bool has_cpu = concurrency.cpu_percent > 0.0;
      const bool has_mem = concurrency.mem_mb > 0.0;
      const bool pass_cpu = has_cpu && concurrency.cpu_percent <= concurrency.cpu_target;
      const bool pass_mem = has_mem && concurrency.mem_mb <= concurrency.mem_target;
      out << "| 服务端 CPU 使用率 | < " << concurrency.cpu_target << "% | "
          << (has_cpu ? std::to_string(concurrency.cpu_percent) + "%" : "N/A")
          << " | " << (has_cpu ? (pass_cpu ? "PASS" : "FAIL") : "N/A") << " |\n";
      out << "| 服务端内存占用 | < " << concurrency.mem_target << " MB | "
          << (has_mem ? std::to_string(concurrency.mem_mb) + " MB" : "N/A")
          << " | " << (has_mem ? (pass_mem ? "PASS" : "FAIL") : "N/A") << " |\n";
    } else {
      out << "| 并发连接成功率 | > 95% (1000 connections) | N/A | N/A |\n";
      out << "| 服务端 CPU 使用率 | < 80% | N/A | N/A |\n";
      out << "| 服务端内存占用 | < 500 MB | N/A | N/A |\n";
    }

    out << "\n## Benchmark Details\n";

    for (const auto& summary : stats_) {
      out << "\n### " << summary.name << "\n";
      out << "| Metric | Value (" << summary.unit << ") |\n";
      out << "| --- | --- |\n";
      out << "| Min | " << summary.stats.min_ms << " |\n";
      out << "| Max | " << summary.stats.max_ms << " |\n";
      out << "| Avg | " << summary.stats.avg_ms << " |\n";
      out << "| P50 | " << summary.stats.p50_ms << " |\n";
      out << "| P95 | " << summary.stats.p95_ms << " |\n";
      out << "| P99 | " << summary.stats.p99_ms << " |\n";

      if (summary.p95_target.has_value() || summary.p99_target.has_value()) {
        out << "\nTargets:\n";
        if (summary.p95_target.has_value()) {
          out << "- p95 < " << *summary.p95_target << summary.unit << "\n";
        }
        if (summary.p99_target.has_value()) {
          out << "- p99 < " << *summary.p99_target << summary.unit << "\n";
        }
      }
    }

    for (const auto& throughput : throughput_) {
      out << "\n### " << throughput.name << "\n";
      out << "- Throughput: " << throughput.msg_per_sec << " msg/s\n";
      out << "- Data Rate: " << throughput.mb_per_sec << " MB/s\n";
      out << "- Target: > " << throughput.target_msg_per_sec << " msg/s\n";
    }

    for (const auto& concurrency : concurrency_) {
      out << "\n### " << concurrency.name << "\n";
      out << "- Clients: " << concurrency.clients << "\n";
      out << "- Handshake Success Rate: " << (concurrency.success_rate * 100.0) << "%\n";
      out << "- CPU Usage: " << concurrency.cpu_percent << "% (target < "
          << concurrency.cpu_target << "%)\n";
      out << "- Memory Usage: " << concurrency.mem_mb << " MB (target < "
          << concurrency.mem_target << " MB)\n";
    }

    out << "\nRaw metrics are available in the CSV report generated alongside this file.\n";

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
      return false;
    }
    file << out.str();
    return true;
  }

 private:
  PerformanceReportGenerator() = default;

  static std::string FormatTimestamp(std::chrono::system_clock::time_point tp) {
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_result{};
#ifdef _WIN32
    localtime_s(&tm_result, &time);
#else
    localtime_r(&time, &tm_result);
#endif
    std::ostringstream stream;
    stream << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S");
    return stream.str();
  }

  static bool EnsureParentPath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path file_path(path);
    auto parent = file_path.parent_path();
    if (parent.empty()) {
      return true;
    }
    std::filesystem::create_directories(parent, ec);
    return !ec;
  }

  void RecordMetricLocked(const std::string& name, double value, const std::string& unit) {
    MetricRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.name = name;
    record.value = value;
    record.unit = unit;
    metrics_.push_back(std::move(record));
  }

  static void AppendSuccessRow(std::ostringstream& out,
                               const std::string& label,
                               const std::string& target,
                               const StatsSummary* stats,
                               std::optional<double> p95_target,
                               std::optional<double> p99_target) {
    if (!stats) {
      out << "| " << label << " | " << target << " | N/A | N/A |\n";
      return;
    }
    bool pass = true;
    if (p95_target.has_value()) {
      pass = pass && stats->stats.p95_ms < *p95_target;
    }
    if (p99_target.has_value()) {
      pass = pass && stats->stats.p99_ms < *p99_target;
    }
    out << "| " << label << " | " << target << " | p95=" << stats->stats.p95_ms;
    if (p99_target.has_value()) {
      out << ", p99=" << stats->stats.p99_ms;
    }
    out << " | " << (pass ? "PASS" : "FAIL") << " |\n";
  }

  const StatsSummary* FindStats(const std::string& name) const {
    for (const auto& summary : stats_) {
      if (summary.name == name) {
        return &summary;
      }
    }
    return nullptr;
  }

  mutable std::mutex mutex_;
  std::vector<MetricRecord> metrics_;
  std::vector<StatsSummary> stats_;
  std::vector<ThroughputSummary> throughput_;
  std::vector<ConcurrencySummary> concurrency_;
};

inline bool BenchmarkOnlyEnabled() {
  const char* env = std::getenv("LEGEND2_BENCHMARK_ONLY");
  if (!env) {
    return false;
  }
  return std::string(env) == "1" || std::string(env) == "true" ||
         std::string(env) == "TRUE";
}

}  // namespace mir2::test::integration

#endif  // MIR2_TESTS_INTEGRATION_PERFORMANCE_REPORT_GENERATOR_H_
