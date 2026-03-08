#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "log/logger.h"

namespace mir2::log {
namespace {

std::filesystem::path BuildUniqueBaseDir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::current_path() /
         ("logger_idempotency_test_" + std::to_string(stamp));
}

TEST(LoggerIdempotencyTest, InitializeTwiceSucceeds) {
  auto& logger = Logger::Instance();
  const auto base_dir = BuildUniqueBaseDir();
  const auto first_dir = base_dir / "first";
  const auto second_dir = base_dir / "second";

  ASSERT_TRUE(logger.Initialize(first_dir.string(), "info", 1, 2));
  auto first_logger = logger.GetLogger(LogCategory::kSystem);
  ASSERT_NE(first_logger, nullptr);

  ASSERT_TRUE(logger.Initialize(second_dir.string(), "debug", 1, 2));
  auto second_logger = logger.GetLogger(LogCategory::kSystem);
  ASSERT_NE(second_logger, nullptr);
  second_logger->info("logger idempotency smoke");

  logger.Shutdown();
  std::error_code ec;
  std::filesystem::remove_all(base_dir, ec);
}

TEST(LoggerIdempotencyTest, ReinitializeAfterShutdownSucceeds) {
  auto& logger = Logger::Instance();
  const auto base_dir = BuildUniqueBaseDir();
  const auto first_dir = base_dir / "first";
  const auto second_dir = base_dir / "second";

  ASSERT_TRUE(logger.Initialize(first_dir.string(), "info", 1, 2));
  logger.Shutdown();

  ASSERT_TRUE(logger.Initialize(second_dir.string(), "info", 1, 2));
  auto system_logger = logger.GetLogger(LogCategory::kSystem);
  ASSERT_NE(system_logger, nullptr);
  system_logger->info("logger reinitialize smoke");

  logger.Shutdown();
  std::error_code ec;
  std::filesystem::remove_all(base_dir, ec);
}

}  // namespace
}  // namespace mir2::log
