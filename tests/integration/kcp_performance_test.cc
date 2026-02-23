#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

#include <asio.hpp>

#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "integration/performance_report_generator.h"
#include "integration/test_helpers.h"

#define private public
#define protected public
#include "integration/kcp_integration_test_base.h"
#undef private
#undef protected

namespace {

using Clock = std::chrono::high_resolution_clock;
using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::ChannelType;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::test::integration::KcpIntegrationTestBase;
using mir2::test::integration::NetworkSimulator;
using mir2::test::integration::PerformanceMonitor;
using mir2::test::integration::PerformanceReportGenerator;

constexpr uint16_t kMoveMsgId = static_cast<uint16_t>(MsgId::kMoveReq);
constexpr uint16_t kAttackReqId = static_cast<uint16_t>(MsgId::kAttackReq);
constexpr uint16_t kAttackRspId = static_cast<uint16_t>(MsgId::kAttackRsp);

constexpr size_t kRttSamples = 100;
constexpr size_t kRecoverySamples = 100;
constexpr size_t kCombatSamples = 100;
constexpr size_t kThroughputMessages = 10000;

constexpr double kRttP95TargetMs = 50.0;
constexpr double kRttP99TargetMs = 80.0;
constexpr double kRecoveryP95TargetMs = 100.0;
constexpr double kCombatP95TargetMs = 80.0;
constexpr double kThroughputTargetMsgPerSec = 1000.0;
constexpr double kConcurrentSuccessTarget = 0.95;
constexpr double kCpuTargetPercent = 80.0;
constexpr double kMemoryTargetMb = 500.0;

std::vector<uint8_t> BuildMoveReqPayload(int32_t x, int32_t y) {
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  mir2::common::MoveRequest request{};
  request.target_x = x;
  request.target_y = y;
  auto payload = mir2::common::EncodeMoveRequest(request, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::vector<uint8_t> BuildAttackReqPayload(uint64_t target_id) {
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  mir2::common::AttackRequest request{};
  request.target_id = target_id;
  request.target_type = mir2::proto::EntityType::MONSTER;
  auto payload = mir2::common::EncodeAttackRequest(request, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::vector<uint8_t> BuildAttackRspPayload(uint64_t attacker_id, uint64_t target_id) {
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  mir2::common::AttackResponse response{};
  response.code = mir2::proto::ErrorCode::ERR_OK;
  response.attacker_id = attacker_id;
  response.target_id = target_id;
  response.damage = 10;
  response.target_hp = 90;
  response.target_dead = false;
  auto payload = mir2::common::EncodeAttackResponse(response, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::filesystem::path ResolveRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "CMakeLists.txt")) {
      return current;
    }
    current = current.parent_path();
  }
  return std::filesystem::current_path();
}

class SimulatedUdpTransport final : public mir2::client::IUdpTransport {
 public:
  SimulatedUdpTransport(asio::io_context& io_context, NetworkSimulator* simulator)
      : io_context_(io_context),
        simulator_(simulator),
        transport_(std::make_unique<mir2::client::UdpTransport>(io_context)) {
  }

  bool Bind(uint16_t port) override {
    return transport_->Bind(port);
  }

  void Close() override {
    transport_->Close();
  }

  bool IsOpen() const override {
    return transport_->IsOpen();
  }

  void StartReceive(ReceiveHandler handler) override {
    receive_handler_ = std::move(handler);
    transport_->StartReceive(
        [this](const asio::ip::udp::endpoint& endpoint, const uint8_t* data, size_t size) {
          if (!receive_handler_ || !data || size == 0) {
            return;
          }

          auto payload = std::vector<uint8_t>(data, data + size);
          auto deliver = [handler = receive_handler_, endpoint,
                          payload = std::move(payload)]() mutable {
            handler(endpoint, payload.data(), payload.size());
          };

          if (simulator_) {
            simulator_->Send(std::move(deliver));
          } else {
            deliver();
          }
        });
  }

  void SendTo(const asio::ip::udp::endpoint& endpoint,
              const uint8_t* data,
              size_t size) override {
    if (!data || size == 0) {
      return;
    }

    auto payload = std::vector<uint8_t>(data, data + size);
    auto deliver = [this, endpoint, payload = std::move(payload)]() mutable {
      transport_->SendTo(endpoint, payload.data(), payload.size());
    };

    if (simulator_) {
      simulator_->Send(std::move(deliver));
    } else {
      deliver();
    }
  }

 private:
  asio::io_context& io_context_;
  NetworkSimulator* simulator_ = nullptr;
  std::unique_ptr<mir2::client::UdpTransport> transport_;
  ReceiveHandler receive_handler_;
};

class KcpPerformanceTest : public KcpIntegrationTestBase {
 protected:
  bool EstablishDualChannel(DualChannelClient* target,
                            std::chrono::milliseconds timeout = 5s) {
    if (!target) {
      return false;
    }
    if (!target->connect(kHost, kTcpPort)) {
      return false;
    }
    if (!WaitForCondition([&]() { return target->is_connected(); }, timeout)) {
      return false;
    }
    return WaitForCondition(
        [&]() {
          return target->get_kcp_state() == DualChannelClient::KcpUpgradeState::kConfirmed;
        },
        timeout);
  }

  bool EstablishDualChannel() {
    return EstablishDualChannel(client_.get());
  }

  void RegisterEcho(uint16_t msg_id) {
    if (!server_) {
      return;
    }
    server_->RegisterHandler(
        msg_id,
        [this, msg_id](const std::shared_ptr<mir2::network::TcpSession>& session,
                       const std::vector<uint8_t>& payload) {
          if (!session || !server_) {
            return;
          }
          server_->Send(session->GetSessionId(), msg_id, payload);
        });
  }

  std::optional<NetworkPacket> WaitForPacket(DualChannelClient* target,
                                             uint16_t msg_id,
                                             std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (target == client_.get()) {
        PumpOnce();
      } else {
        if (server_) {
          server_->Tick();
        }
        if (target) {
          target->update();
        }
      }

      if (target) {
        auto packet = target->receive();
        if (packet && packet->msg_id == msg_id) {
          return packet;
        }
      }
      std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
  }
};

class KcpLossyPerformanceTest : public KcpPerformanceTest {
 protected:
  std::unique_ptr<DualChannelClient> CreateTestClient() override {
    auto client = std::make_unique<DualChannelClient>();

    NetworkSimulator::Config sim_config{};
    sim_config.loss_rate = 0.05;
    sim_config.seed = 42;
    udp_simulator_ = std::make_unique<NetworkSimulator>(client->io_context_, sim_config);

    auto transport = std::make_unique<SimulatedUdpTransport>(client->io_context_,
                                                             udp_simulator_.get());
    if (client->kcp_channel_) {
      client->kcp_channel_->transport_ = std::move(transport);
    }

    return client;
  }

  void TearDown() override {
    if (udp_simulator_) {
      udp_simulator_->Flush();
    }
    KcpPerformanceTest::TearDown();
  }

 private:
  std::unique_ptr<NetworkSimulator> udp_simulator_;
};

class KcpConcurrencyPerformanceTest : public KcpIntegrationTestBase {
 protected:
  std::unique_ptr<mir2::test::integration::MockGameServer> CreateTestServer() override {
    mir2::test::integration::MockGameServer::Config config;
    config.bind_ip = kHost;
    config.tcp_port = kTcpPort;
    config.udp_port = kUdpPort;
    config.max_connections = 1200;

    auto server = std::make_unique<mir2::test::integration::MockGameServer>(io_context_, config);
    server->Start();
    return server;
  }
};

struct ProcessMetricsSnapshot {
  double cpu_time_seconds = 0.0;
  double memory_mb = 0.0;
};

ProcessMetricsSnapshot ReadProcessMetrics() {
  ProcessMetricsSnapshot snapshot;
#ifdef _WIN32
  FILETIME creation_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time,
                      &kernel_time, &user_time)) {
    ULARGE_INTEGER kernel{};
    ULARGE_INTEGER user{};
    kernel.LowPart = kernel_time.dwLowDateTime;
    kernel.HighPart = kernel_time.dwHighDateTime;
    user.LowPart = user_time.dwLowDateTime;
    user.HighPart = user_time.dwHighDateTime;
    snapshot.cpu_time_seconds =
        (static_cast<double>(kernel.QuadPart + user.QuadPart) * 1e-7);
  }

  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters))) {
    snapshot.memory_mb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
  }
#else
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    snapshot.cpu_time_seconds = static_cast<double>(usage.ru_utime.tv_sec) +
                                static_cast<double>(usage.ru_utime.tv_usec) / 1e6 +
                                static_cast<double>(usage.ru_stime.tv_sec) +
                                static_cast<double>(usage.ru_stime.tv_usec) / 1e6;
#ifdef __APPLE__
    snapshot.memory_mb = static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    snapshot.memory_mb = static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
  }
#endif
  return snapshot;
}

}  // namespace

TEST_F(KcpPerformanceTest, RttBenchmark) {
  if (!mir2::test::integration::BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Pass --benchmark-only to run performance benchmarks.";
  }

  ASSERT_TRUE(EstablishDualChannel());

  server_->SetRoute(kMoveMsgId, ChannelType::kKcp);
  client_->set_route(kMoveMsgId, ChannelType::kKcp);
  RegisterEcho(kMoveMsgId);

  PerformanceMonitor monitor;
  for (size_t i = 0; i < kRttSamples; ++i) {
    const auto payload = BuildMoveReqPayload(static_cast<int32_t>(i % 200),
                                             static_cast<int32_t>((i * 3) % 200));
    ASSERT_FALSE(payload.empty());
    const auto start = Clock::now();

    client_->send(kMoveMsgId, payload);
    auto packet = WaitForPacket(client_.get(), kMoveMsgId, 500ms);
    ASSERT_TRUE(packet.has_value());

    const auto rtt = Clock::now() - start;
    monitor.RecordRtt(std::chrono::duration_cast<std::chrono::nanoseconds>(rtt));
  }

  const auto stats = monitor.GetRttStats();
  PerformanceReportGenerator::Instance().AddStats("RttBenchmark", stats, "ms",
                                                  kRttP95TargetMs, kRttP99TargetMs);

  EXPECT_LT(stats.p95_ms, kRttP95TargetMs);
  EXPECT_LT(stats.p99_ms, kRttP99TargetMs);
}

TEST_F(KcpConcurrencyPerformanceTest, ConcurrentConnectionsStressTest) {
  if (!mir2::test::integration::BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Pass --benchmark-only to run performance benchmarks.";
  }

  constexpr size_t kClientCount = 1000;
  std::vector<std::unique_ptr<DualChannelClient>> clients;
  clients.reserve(kClientCount);

  size_t connect_attempted = 0;
  for (size_t i = 0; i < kClientCount; ++i) {
    auto client = std::make_unique<DualChannelClient>();
    ++connect_attempted;
    if (client->connect(kHost, kTcpPort)) {
      clients.push_back(std::move(client));
    }
  }

  auto metrics_start = ReadProcessMetrics();
  const auto start_time = Clock::now();

  std::vector<bool> confirmed(clients.size(), false);
  size_t confirmed_count = 0;
  const auto deadline = Clock::now() + 15s;

  while (Clock::now() < deadline && confirmed_count < clients.size()) {
    server_->Tick();
    for (size_t i = 0; i < clients.size(); ++i) {
      auto& client = clients[i];
      client->update();
      if (!confirmed[i] && client->is_connected() &&
          client->get_kcp_state() == DualChannelClient::KcpUpgradeState::kConfirmed) {
        confirmed[i] = true;
        ++confirmed_count;
      }
    }
    std::this_thread::sleep_for(1ms);
  }

  const auto end_time = Clock::now();
  auto metrics_end = ReadProcessMetrics();

  const double wall_seconds = std::chrono::duration<double>(end_time - start_time).count();
  const double cpu_seconds = metrics_end.cpu_time_seconds - metrics_start.cpu_time_seconds;
  const unsigned int cores = std::max(1u, std::thread::hardware_concurrency());
  double cpu_percent = 0.0;
  if (wall_seconds > 0.0) {
    cpu_percent = (cpu_seconds / wall_seconds) * 100.0 / static_cast<double>(cores);
  }

  const double mem_mb = metrics_end.memory_mb;
  const double success_rate = (connect_attempted == 0)
                                  ? 0.0
                                  : static_cast<double>(confirmed_count) /
                                        static_cast<double>(connect_attempted);

  PerformanceReportGenerator::Instance().AddConcurrency(
      "ConcurrentConnectionsStressTest",
      connect_attempted,
      success_rate,
      cpu_percent,
      mem_mb,
      kConcurrentSuccessTarget,
      kCpuTargetPercent,
      kMemoryTargetMb);

  EXPECT_GT(success_rate, kConcurrentSuccessTarget);
  if (cpu_percent > 0.0) {
    EXPECT_LT(cpu_percent, kCpuTargetPercent);
  }
  if (mem_mb > 0.0) {
    EXPECT_LT(mem_mb, kMemoryTargetMb);
  }
}

TEST_F(KcpLossyPerformanceTest, PacketLossRecoveryBenchmark) {
  if (!mir2::test::integration::BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Pass --benchmark-only to run performance benchmarks.";
  }

  ASSERT_TRUE(EstablishDualChannel());

  server_->SetRoute(kMoveMsgId, ChannelType::kKcp);
  client_->set_route(kMoveMsgId, ChannelType::kKcp);
  RegisterEcho(kMoveMsgId);

  PerformanceMonitor monitor;
  for (size_t i = 0; i < kRecoverySamples; ++i) {
    const auto payload = BuildMoveReqPayload(static_cast<int32_t>((i + 10) % 200),
                                             static_cast<int32_t>((i * 5) % 200));
    ASSERT_FALSE(payload.empty());
    const auto start = Clock::now();

    client_->send(kMoveMsgId, payload);
    auto packet = WaitForPacket(client_.get(), kMoveMsgId, 1500ms);
    ASSERT_TRUE(packet.has_value());

    const auto rtt = Clock::now() - start;
    monitor.RecordLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(rtt));
  }

  const auto stats = monitor.GetLatencyStats();
  PerformanceReportGenerator::Instance().AddStats("PacketLossRecoveryBenchmark", stats, "ms",
                                                  kRecoveryP95TargetMs, std::nullopt);
  EXPECT_LT(stats.p95_ms, kRecoveryP95TargetMs);
}

TEST_F(KcpPerformanceTest, ThroughputBenchmark) {
  if (!mir2::test::integration::BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Pass --benchmark-only to run performance benchmarks.";
  }

  ASSERT_TRUE(EstablishDualChannel());

  server_->SetRoute(kMoveMsgId, ChannelType::kKcp);
  client_->set_route(kMoveMsgId, ChannelType::kKcp);
  RegisterEcho(kMoveMsgId);

  std::atomic<size_t> received{0};
  client_->set_on_message([&](const NetworkPacket& packet) {
    if (packet.msg_id == kMoveMsgId) {
      received.fetch_add(1, std::memory_order_relaxed);
    }
  });

  const auto payload = BuildMoveReqPayload(88, 66);
  ASSERT_FALSE(payload.empty());

  const auto start = Clock::now();
  for (size_t i = 0; i < kThroughputMessages; ++i) {
    client_->send(kMoveMsgId, payload);
  }

  const auto deadline = Clock::now() + 10s;
  while (received.load(std::memory_order_relaxed) < kThroughputMessages &&
         Clock::now() < deadline) {
    PumpOnce();
    std::this_thread::sleep_for(1ms);
  }

  const auto end = Clock::now();
  const double elapsed = std::chrono::duration<double>(end - start).count();
  const double msg_per_sec = elapsed > 0.0
                                 ? static_cast<double>(kThroughputMessages) / elapsed
                                 : 0.0;
  const double total_mb = (static_cast<double>(payload.size()) *
                           static_cast<double>(kThroughputMessages)) /
                          (1024.0 * 1024.0);
  const double mb_per_sec = elapsed > 0.0 ? total_mb / elapsed : 0.0;

  PerformanceReportGenerator::Instance().AddThroughput("ThroughputBenchmark",
                                                       msg_per_sec,
                                                       mb_per_sec,
                                                       kThroughputTargetMsgPerSec);

  EXPECT_GE(received.load(std::memory_order_relaxed), kThroughputMessages);
  EXPECT_GT(msg_per_sec, kThroughputTargetMsgPerSec);
}

TEST_F(KcpPerformanceTest, CombatLatencyBenchmark) {
  if (!mir2::test::integration::BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Pass --benchmark-only to run performance benchmarks.";
  }

  ASSERT_TRUE(EstablishDualChannel());

  server_->SetRoute(kAttackReqId, ChannelType::kKcp);
  server_->SetRoute(kAttackRspId, ChannelType::kKcp);
  client_->set_route(kAttackReqId, ChannelType::kKcp);

  server_->RegisterHandler(
      kAttackReqId,
      [this](const std::shared_ptr<mir2::network::TcpSession>& session,
             const std::vector<uint8_t>& /*payload*/) {
        if (!session || !server_) {
          return;
        }
        auto rsp_payload = BuildAttackRspPayload(1001, 2002);
        if (rsp_payload.empty()) {
          return;
        }
        server_->Send(session->GetSessionId(), kAttackRspId, rsp_payload);
      });

  PerformanceMonitor monitor;
  for (size_t i = 0; i < kCombatSamples; ++i) {
    const auto payload = BuildAttackReqPayload(10000 + i);
    ASSERT_FALSE(payload.empty());
    const auto start = Clock::now();

    client_->send(kAttackReqId, payload);
    auto packet = WaitForPacket(client_.get(), kAttackRspId, 500ms);
    ASSERT_TRUE(packet.has_value());

    const auto latency = Clock::now() - start;
    monitor.RecordLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(latency));
  }

  const auto stats = monitor.GetLatencyStats();
  PerformanceReportGenerator::Instance().AddStats("CombatLatencyBenchmark", stats, "ms",
                                                  kCombatP95TargetMs, std::nullopt);
  EXPECT_LT(stats.p95_ms, kCombatP95TargetMs);
}

class PerformanceReportEnvironment : public ::testing::Environment {
 public:
  void TearDown() override {
    auto& generator = PerformanceReportGenerator::Instance();
    if (!generator.HasData()) {
      return;
    }

    const auto repo_root = ResolveRepoRoot();
    const auto report_path = repo_root / "docs" / "STAGE3-PERFORMANCE-REPORT.md";
    const auto csv_path = repo_root / "docs" / "STAGE3-PERFORMANCE-REPORT.csv";

    generator.WriteCsv(csv_path.string());
    generator.WriteMarkdown(report_path.string());
  }
};

::testing::Environment* const kPerformanceReportEnv =
    ::testing::AddGlobalTestEnvironment(new PerformanceReportEnvironment());

