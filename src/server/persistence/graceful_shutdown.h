/**
 * @file graceful_shutdown.h
 * @brief Graceful shutdown handler (Epic 2: Story 2.3)
 */

#ifndef MIR2_PERSISTENCE_GRACEFUL_SHUTDOWN_H
#define MIR2_PERSISTENCE_GRACEFUL_SHUTDOWN_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <signal.h>
#include <string>

namespace mir2::persistence {

/**
 * @brief Graceful shutdown handler (Story 2.3)
 *
 * Responsibilities:
 * - Intercept SIGTERM/SIGINT signals
 * - Execute shutdown sequence:
 *   1. Broadcast shutdown notification
 *   2. Disable new connections
 *   3. Wait for battles to complete
 *   4. Save full snapshot
 *   5. Exit cleanly
 *
 * Thread-safe signal handling.
 */
class GracefulShutdownHandler {
 public:
    /**
     * @brief Shutdown callback types
     */
    using ShutdownCallback = std::function<void()>;
    using SaveCallback = std::function<void()>;
    using NotifyCallback = std::function<void()>;

    /**
     * @brief Create shutdown handler
     * @param shutdown_timeout_ms Maximum time to wait for graceful shutdown (default 30s)
     */
    explicit GracefulShutdownHandler(uint32_t shutdown_timeout_ms = 30000);

    ~GracefulShutdownHandler();

    /**
     * @brief Register shutdown signal handlers (SIGTERM, SIGINT)
     * @param notify_callback Called to notify players
     * @param save_callback Called to save final snapshot
     * @param shutdown_callback Called to initiate server shutdown
     */
    void RegisterHandlers(
        NotifyCallback notify_callback,
        SaveCallback save_callback,
        ShutdownCallback shutdown_callback);

    /**
     * @brief Check if shutdown requested
     */
    bool IsShutdownRequested() const { return shutdown_requested_; }

    /**
     * @brief Get shutdown state
     */
    const char* GetShutdownState() const;

    /**
     * @brief Manually trigger graceful shutdown
     */
    void RequestShutdown();

 private:
    uint32_t shutdown_timeout_ms_;
    std::atomic<bool> shutdown_requested_{false};

    NotifyCallback notify_callback_;
    SaveCallback save_callback_;
    ShutdownCallback shutdown_callback_;

    /**
     * @brief Execute shutdown sequence
     */
    void ExecuteShutdownSequence();

    /**
     * @brief Signal handler (static, delegates to instance)
     */
    static void SignalHandler(int signal);

    /**
     * @brief Global instance pointer for signal handler
     */
    static GracefulShutdownHandler* global_instance_;
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_GRACEFUL_SHUTDOWN_H
