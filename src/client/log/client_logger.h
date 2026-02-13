/**
 * @file client_logger.h
 * @brief Lightweight client-side logging system.
 *
 * Provides categorized logging for the Legend2 client using spdlog.
 * Unlike the server logger, this does not carry coroutine trace context
 * since the client has no coroutine infrastructure.
 *
 * Usage:
 *   ClientLogger::Instance().Initialize("logs/client", "info");
 *   CLIENT_LOG_INFO(mir2::client::log::LogCategory::kNetwork, "connected to {}:{}", host, port);
 *   CLOG_INFO("application started");  // defaults to kSystem category
 */

#ifndef LEGEND2_CLIENT_LOG_CLIENT_LOGGER_H
#define LEGEND2_CLIENT_LOG_CLIENT_LOGGER_H

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace mir2::client::log {

/**
 * @brief Log categories for the client.
 *
 * Each category produces its own rotating log file and shares a common
 * console sink.
 */
enum class LogCategory {
    kSystem,
    kNetwork,
    kGame,
    kUI,
    kRender,
    kAudio,
    kResource,
};

namespace detail {

/// Log pattern consistent with the server logger format.
constexpr const char* kClientLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v";

/// All client log categories for iteration.
constexpr std::array<LogCategory, 7> kAllClientCategories = {
    LogCategory::kSystem,
    LogCategory::kNetwork,
    LogCategory::kGame,
    LogCategory::kUI,
    LogCategory::kRender,
    LogCategory::kAudio,
    LogCategory::kResource,
};

/// Maps a LogCategory to its human-readable name used for logger names and
/// file paths.
inline const char* category_name(LogCategory category) {
    switch (category) {
        case LogCategory::kSystem:   return "client_system";
        case LogCategory::kNetwork:  return "client_network";
        case LogCategory::kGame:     return "client_game";
        case LogCategory::kUI:       return "client_ui";
        case LogCategory::kRender:   return "client_render";
        case LogCategory::kAudio:    return "client_audio";
        case LogCategory::kResource: return "client_resource";
        default:                     return "client_system";
    }
}

}  // namespace detail

/**
 * @brief Client-side log manager.
 *
 * Singleton that owns per-category spdlog loggers. Each category gets a
 * shared console sink and its own rotating file sink.
 *
 * Thread safety: all public methods are protected by an internal mutex so
 * the logger can safely be used from the main thread and any IO / loader
 * threads.
 */
class ClientLogger {
public:
    /// Returns the singleton instance (Meyer's singleton).
    static ClientLogger& Instance() {
        static ClientLogger instance;
        return instance;
    }

    /**
     * @brief Initialize the logging system.
     *
     * Creates log directory, console sink, and per-category rotating file
     * sinks.  Safe to call more than once -- previous loggers are dropped
     * and recreated.
     *
     * @param log_path   Directory where log files are written.
     * @param level      spdlog level string ("trace", "debug", "info", ...).
     * @param max_size_mb  Maximum size of a single log file in megabytes.
     * @param max_files    Maximum number of rotated files per category.
     * @return true on success, false if an exception occurred.
     */
    inline bool Initialize(const std::string& log_path,
                           const std::string& level,
                           int max_size_mb = 50,
                           int max_files = 5) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);

            std::filesystem::create_directories(log_path);

            spdlog::level::level_enum log_level = spdlog::level::from_str(level);

            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(log_level);

            const size_t max_size = static_cast<size_t>(max_size_mb) * 1024 * 1024;
            const size_t max_file_count = static_cast<size_t>(max_files);

            loggers_.clear();

            for (const auto category : detail::kAllClientCategories) {
                const std::string logger_name = detail::category_name(category);

                // Drop any previously registered logger with the same name.
                if (spdlog::get(logger_name)) {
                    spdlog::drop(logger_name);
                }

                const std::string file_name = log_path + "/" + logger_name + ".log";
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    file_name, max_size, max_file_count);
                file_sink->set_level(log_level);

                std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
                auto logger = std::make_shared<spdlog::logger>(
                    logger_name, sinks.begin(), sinks.end());
                logger->set_level(log_level);
                logger->set_pattern(detail::kClientLogPattern);

                spdlog::register_logger(logger);
                loggers_[category] = logger;
            }

            initialized_ = true;
            return true;
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief Shut down the logging system.
     *
     * Flushes all loggers, drops them from spdlog's registry, and clears
     * internal state.  Safe to call even if Initialize was never called.
     */
    inline void Shutdown() {
        std::vector<std::shared_ptr<spdlog::logger>> loggers_to_flush;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loggers_to_flush.reserve(loggers_.size());
            for (const auto& [_, logger] : loggers_) {
                if (logger) {
                    loggers_to_flush.push_back(logger);
                }
            }
            loggers_.clear();
            initialized_ = false;
        }

        for (const auto& logger : loggers_to_flush) {
            logger->flush();
        }

        for (const auto category : detail::kAllClientCategories) {
            const std::string logger_name = detail::category_name(category);
            if (spdlog::get(logger_name)) {
                spdlog::drop(logger_name);
            }
        }

        if (spdlog::get("client_fallback")) {
            spdlog::drop("client_fallback");
        }
    }

    /**
     * @brief Retrieve the logger for the given category.
     *
     * If Initialize has not been called yet (or the category is unknown),
     * returns a fallback console-only logger so that log calls never crash.
     *
     * @param category  The log category.
     * @return A shared_ptr to the spdlog logger.
     */
    inline std::shared_ptr<spdlog::logger> GetLogger(LogCategory category) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = loggers_.find(category);
            if (it != loggers_.end()) {
                return it->second;
            }
        }

        // Fallback: create (or reuse) a minimal console logger so callers
        // that log before Initialize() do not dereference a nullptr.
        static std::mutex fallback_mutex;
        static std::shared_ptr<spdlog::logger> local_fallback;

        std::lock_guard<std::mutex> lock(fallback_mutex);

        if (auto fb = spdlog::get("client_fallback")) {
            return fb;
        }
        if (local_fallback) {
            return local_fallback;
        }

        try {
            local_fallback = spdlog::stdout_color_mt("client_fallback");
        } catch (...) {
            auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            local_fallback = std::make_shared<spdlog::logger>("client_fallback_unreg", sink);
        }

        local_fallback->set_pattern(detail::kClientLogPattern);
        return local_fallback;
    }

    /**
     * @brief Flush all active loggers immediately.
     */
    inline void Flush() {
        std::vector<std::shared_ptr<spdlog::logger>> loggers_to_flush;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loggers_to_flush.reserve(loggers_.size());
            for (const auto& [_, logger] : loggers_) {
                if (logger) {
                    loggers_to_flush.push_back(logger);
                }
            }
        }

        for (const auto& logger : loggers_to_flush) {
            logger->flush();
        }
    }

    /// Returns true if Initialize() has been called successfully.
    inline bool is_initialized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

private:
    ClientLogger() = default;
    ~ClientLogger() = default;

    ClientLogger(const ClientLogger&) = delete;
    ClientLogger& operator=(const ClientLogger&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<LogCategory, std::shared_ptr<spdlog::logger>> loggers_;
    bool initialized_ = false;
};

// ---------------------------------------------------------------------------
// Convenience macros -- categorized
// ---------------------------------------------------------------------------

#define CLIENT_LOG_INFO(cat, fmt, ...) \
    mir2::client::log::ClientLogger::Instance().GetLogger(cat)->info(fmt, ##__VA_ARGS__)

#define CLIENT_LOG_DEBUG(cat, fmt, ...) \
    mir2::client::log::ClientLogger::Instance().GetLogger(cat)->debug(fmt, ##__VA_ARGS__)

#define CLIENT_LOG_WARN(cat, fmt, ...) \
    mir2::client::log::ClientLogger::Instance().GetLogger(cat)->warn(fmt, ##__VA_ARGS__)

#define CLIENT_LOG_ERROR(cat, fmt, ...) \
    mir2::client::log::ClientLogger::Instance().GetLogger(cat)->error(fmt, ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Shorthand macros -- default to kSystem category
// ---------------------------------------------------------------------------

#define CLOG_INFO(fmt, ...) \
    CLIENT_LOG_INFO(mir2::client::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)

#define CLOG_DEBUG(fmt, ...) \
    CLIENT_LOG_DEBUG(mir2::client::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)

#define CLOG_WARN(fmt, ...) \
    CLIENT_LOG_WARN(mir2::client::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)

#define CLOG_ERROR(fmt, ...) \
    CLIENT_LOG_ERROR(mir2::client::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)

}  // namespace mir2::client::log

#endif  // LEGEND2_CLIENT_LOG_CLIENT_LOGGER_H
