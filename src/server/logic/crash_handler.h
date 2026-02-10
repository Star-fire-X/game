/**
 * @file crash_handler.h
 * @brief Crash dump handler
 */

#ifndef MIR2_LOGIC_CRASH_HANDLER_H_
#define MIR2_LOGIC_CRASH_HANDLER_H_

namespace mir2::logic {

/**
 * @brief Initialize Breakpad and register crash callback.
 */
class CrashHandler final {
 public:
  static bool Initialize();
  static void Shutdown();

  CrashHandler() = delete;
  CrashHandler(const CrashHandler&) = delete;
  CrashHandler& operator=(const CrashHandler&) = delete;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_CRASH_HANDLER_H_
