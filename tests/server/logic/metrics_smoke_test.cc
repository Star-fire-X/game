#include <gtest/gtest.h>

#include <asio/connect.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include "monitor/metrics.h"

namespace mir2::logic::test {
namespace {

using namespace std::chrono_literals;

#if defined(LEGEND2_ENABLE_PROMETHEUS)

uint16_t AllocateFreeTcpPort() {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(io_context);
  acceptor.open(asio::ip::tcp::v4());
  acceptor.set_option(asio::socket_base::reuse_address(true));
  acceptor.bind(
      asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

std::string FetchMetricsText(uint16_t port) {
  asio::io_context io_context;
  asio::ip::tcp::resolver resolver(io_context);
  asio::ip::tcp::socket socket(io_context);

  auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
  asio::connect(socket, endpoints);

  static constexpr char kRequest[] =
      "GET /metrics HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n\r\n";
  asio::write(socket, asio::buffer(kRequest, std::strlen(kRequest)));

  std::string response;
  std::array<char, 4096> chunk{};
  asio::error_code ec;
  for (;;) {
    const size_t bytes = socket.read_some(asio::buffer(chunk), ec);
    if (bytes > 0) {
      response.append(chunk.data(), bytes);
    }
    if (ec == asio::error::eof) {
      break;
    }
    if (ec) {
      throw asio::system_error(ec);
    }
  }

  return response;
}

bool ContainsAllExpectedMetrics(const std::string& text) {
  return text.find("logic_hot_event_legacy_fallback_total") != std::string::npos &&
         text.find("logic_hot_event_bypass_total") != std::string::npos &&
         text.find("logic_mailbox_batch_size") != std::string::npos &&
         text.find("logic_mailbox_batch_players") != std::string::npos &&
         text.find("logic_mailbox_overflow_total") != std::string::npos;
}

#endif

}  // namespace

TEST(MetricsSmokeTest, ExposesCustomMetricsOnHttpEndpoint) {
#if !defined(LEGEND2_ENABLE_PROMETHEUS)
  GTEST_SKIP() << "Prometheus is disabled (LEGEND2_ENABLE_PROMETHEUS=OFF).";
#else
  const uint16_t port = AllocateFreeTcpPort();
  auto& metrics = monitor::Metrics::Instance();
  metrics.Init(port);

  metrics.IncrementCounter("logic.hot_event.legacy_fallback_total");
  metrics.IncrementCounter("logic.hot_event.bypass_total");
  metrics.SetGauge("logic.mailbox.batch_size", 64.0);
  metrics.SetGauge("logic.mailbox.batch_players", 8.0);
  metrics.IncrementCounter("logic.mailbox.overflow_total");

  std::string response_text;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    try {
      response_text = FetchMetricsText(port);
      if (ContainsAllExpectedMetrics(response_text)) {
        break;
      }
    } catch (const std::exception&) {
      // Exposer may not be immediately ready; retry until timeout.
    }
    std::this_thread::sleep_for(50ms);
  }

  EXPECT_NE(response_text.find("200"), std::string::npos);
  EXPECT_TRUE(ContainsAllExpectedMetrics(response_text)) << response_text;
#endif
}

}  // namespace mir2::logic::test
