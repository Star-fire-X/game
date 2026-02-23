/**
 * @file client_logger_test.cc
 * @brief Unit tests for ClientLogger singleton.
 *
 * Tests cover initialization, shutdown, category enumeration, fallback
 * behaviour, file creation, and re-initialization.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "client/log/client_logger.h"

namespace mir2::client::log {
namespace {

namespace fs = std::filesystem;

class ClientLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "mir2_client_logger_test" /
                   ::testing::UnitTest::GetInstance()
                       ->current_test_info()
                       ->name();
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    void TearDown() override {
        ClientLogger::Instance().Shutdown();

        for (const auto cat : detail::kAllClientCategories) {
            const std::string name = detail::category_name(cat);
            if (spdlog::get(name)) {
                spdlog::drop(name);
            }
        }
        if (spdlog::get("client_fallback")) {
            spdlog::drop("client_fallback");
        }

        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    bool InitDefault() {
        return ClientLogger::Instance().Initialize(
            tmp_dir_.string(), "info", /*max_size_mb=*/1, /*max_files=*/2);
    }

    fs::path tmp_dir_;
};

TEST_F(ClientLoggerTest, NotInitializedByDefault) {
    EXPECT_FALSE(ClientLogger::Instance().is_initialized());
}

TEST_F(ClientLoggerTest, InitializeSucceeds) {
    ASSERT_TRUE(InitDefault());
    EXPECT_TRUE(ClientLogger::Instance().is_initialized());
}

TEST_F(ClientLoggerTest, GetLoggerBeforeInit) {
    ASSERT_FALSE(ClientLogger::Instance().is_initialized());
    auto logger = ClientLogger::Instance().GetLogger(LogCategory::kSystem);
    ASSERT_NE(logger, nullptr);
    logger->info("fallback log line from GetLoggerBeforeInit");
}

TEST_F(ClientLoggerTest, GetLoggerAfterInit) {
    ASSERT_TRUE(InitDefault());
    for (const auto cat : detail::kAllClientCategories) {
        auto logger = ClientLogger::Instance().GetLogger(cat);
        EXPECT_NE(logger, nullptr)
            << "GetLogger returned nullptr for category "
            << detail::category_name(cat);
    }
}

TEST_F(ClientLoggerTest, GetLoggerAllCategories) {
    ASSERT_TRUE(InitDefault());

    std::vector<std::string> names;
    for (const auto cat : detail::kAllClientCategories) {
        auto logger = ClientLogger::Instance().GetLogger(cat);
        ASSERT_NE(logger, nullptr);
        names.push_back(logger->name());
    }

    EXPECT_EQ(names.size(), 7u);

    std::sort(names.begin(), names.end());
    auto last = std::unique(names.begin(), names.end());
    EXPECT_EQ(last, names.end()) << "Duplicate logger names detected";
}

TEST_F(ClientLoggerTest, ShutdownClearsState) {
    ASSERT_TRUE(InitDefault());
    ASSERT_TRUE(ClientLogger::Instance().is_initialized());
    ClientLogger::Instance().Shutdown();
    EXPECT_FALSE(ClientLogger::Instance().is_initialized());
}

TEST_F(ClientLoggerTest, ShutdownThenGetLogger) {
    ASSERT_TRUE(InitDefault());
    ClientLogger::Instance().Shutdown();

    auto logger = ClientLogger::Instance().GetLogger(LogCategory::kNetwork);
    ASSERT_NE(logger, nullptr);

    const auto& name = logger->name();
    EXPECT_TRUE(name.find("fallback") != std::string::npos)
        << "Expected fallback logger but got: " << name;
}

TEST_F(ClientLoggerTest, ReInitialize) {
    ASSERT_TRUE(InitDefault());
    ASSERT_TRUE(ClientLogger::Instance().is_initialized());
    ClientLogger::Instance().Shutdown();
    ASSERT_FALSE(ClientLogger::Instance().is_initialized());

    ASSERT_TRUE(InitDefault());
    EXPECT_TRUE(ClientLogger::Instance().is_initialized());

    for (const auto cat : detail::kAllClientCategories) {
        EXPECT_NE(ClientLogger::Instance().GetLogger(cat), nullptr);
    }
}

TEST_F(ClientLoggerTest, InitializeCreatesDirectory) {
    ASSERT_FALSE(fs::exists(tmp_dir_));
    ASSERT_TRUE(InitDefault());
    EXPECT_TRUE(fs::exists(tmp_dir_));
    EXPECT_TRUE(fs::is_directory(tmp_dir_));
}

TEST_F(ClientLoggerTest, InitializeCreatesLogFiles) {
    ASSERT_TRUE(InitDefault());

    for (const auto cat : detail::kAllClientCategories) {
        auto logger = ClientLogger::Instance().GetLogger(cat);
        ASSERT_NE(logger, nullptr);
        logger->info("init check for {}", detail::category_name(cat));
    }
    ClientLogger::Instance().Flush();

    for (const auto cat : detail::kAllClientCategories) {
        const std::string expected_name =
            std::string(detail::category_name(cat)) + ".log";
        const fs::path expected_path = tmp_dir_ / expected_name;
        EXPECT_TRUE(fs::exists(expected_path))
            << "Expected log file not found: " << expected_path;
    }
}

TEST_F(ClientLoggerTest, CategoryNameMapping) {
    EXPECT_STREQ(detail::category_name(LogCategory::kSystem),   "client_system");
    EXPECT_STREQ(detail::category_name(LogCategory::kNetwork),  "client_network");
    EXPECT_STREQ(detail::category_name(LogCategory::kGame),     "client_game");
    EXPECT_STREQ(detail::category_name(LogCategory::kUI),       "client_ui");
    EXPECT_STREQ(detail::category_name(LogCategory::kRender),   "client_render");
    EXPECT_STREQ(detail::category_name(LogCategory::kAudio),    "client_audio");
    EXPECT_STREQ(detail::category_name(LogCategory::kResource), "client_resource");
}

TEST_F(ClientLoggerTest, FlushDoesNotCrash) {
    EXPECT_NO_THROW(ClientLogger::Instance().Flush());

    ASSERT_TRUE(InitDefault());
    EXPECT_NO_THROW(ClientLogger::Instance().Flush());

    ClientLogger::Instance().Shutdown();
    EXPECT_NO_THROW(ClientLogger::Instance().Flush());
}

}  // namespace
}  // namespace mir2::client::log
