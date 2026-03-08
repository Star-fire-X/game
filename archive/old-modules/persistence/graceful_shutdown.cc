/**
 * @file graceful_shutdown.cc
 * @brief Graceful shutdown handler implementation
 */

#include "persistence/graceful_shutdown.h"
#include "log/logger.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace mir2::persistence {

GracefulShutdownHandler* GracefulShutdownHandler::global_instance_ = nullptr;

GracefulShutdownHandler::GracefulShutdownHandler(uint32_t shutdown_timeout_ms)
    : shutdown_timeout_ms_(shutdown_timeout_ms) {
    global_instance_ = this;
    SYSLOG_INFO("GracefulShutdownHandler initialized (timeout: {}ms)", shutdown_timeout_ms_);
}

GracefulShutdownHandler::~GracefulShutdownHandler() {
    global_instance_ = nullptr;
}

void GracefulShutdownHandler::RegisterHandlers(
    NotifyCallback notify_callback,
    SaveCallback save_callback,
    ShutdownCallback shutdown_callback) {
    notify_callback_ = notify_callback;
    save_callback_ = save_callback;
    shutdown_callback_ = shutdown_callback;

    // Register signal handlers
    signal(SIGTERM, &GracefulShutdownHandler::SignalHandler);
    signal(SIGINT, &GracefulShutdownHandler::SignalHandler);

    SYSLOG_INFO("Graceful shutdown handlers registered (SIGTERM, SIGINT)");
}

const char* GracefulShutdownHandler::GetShutdownState() const {
    if (!shutdown_requested_) {
        return "running";
    }
    return "shutdown_in_progress";
}

void GracefulShutdownHandler::RequestShutdown() {
    if (shutdown_requested_.exchange(true)) {
        SYSLOG_WARN("Shutdown already requested");
        return;
    }

    SYSLOG_WARN("Graceful shutdown requested");
    ExecuteShutdownSequence();
}

void GracefulShutdownHandler::ExecuteShutdownSequence() {
    auto shutdown_start = std::chrono::steady_clock::now();

    try {
        // Step 1: Notify players
        if (notify_callback_) {
            SYSLOG_INFO("Step 1/5: Notifying players of shutdown");
            notify_callback_();
            std::this_thread::sleep_for(std::chrono::seconds(2));  // Give time for notification
        }

        // Step 2: Disable new connections
        SYSLOG_INFO("Step 2/5: Disabling new connections");
        // Note: This would be implemented in application layer

        // Step 3: Wait for battles (with timeout)
        SYSLOG_INFO("Step 3/5: Waiting for active battles to complete");
        auto max_wait = std::chrono::milliseconds(shutdown_timeout_ms_ / 2);
        auto wait_start = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - wait_start < max_wait) {
            // Check if battles still active (stub)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Step 4: Save final snapshot
        SYSLOG_INFO("Step 4/5: Saving final snapshot");
        if (save_callback_) {
            save_callback_();
        }

        // Step 5: Clean exit
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - shutdown_start);

        SYSLOG_INFO("Step 5/5: Clean shutdown completed in {}ms", elapsed.count());

        if (shutdown_callback_) {
            shutdown_callback_();
        }

        // Exit process
        std::exit(0);
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Error during graceful shutdown: {}", e.what());
        // Force exit after timeout
        std::exit(1);
    }
}

void GracefulShutdownHandler::SignalHandler(int signal) {
    if (global_instance_) {
        const char* signal_name = (signal == SIGTERM) ? "SIGTERM" :
                                 (signal == SIGINT) ? "SIGINT" : "UNKNOWN";
        SYSLOG_WARN("Received signal: {}", signal_name);
        global_instance_->RequestShutdown();
    }
}

}  // namespace mir2::persistence
