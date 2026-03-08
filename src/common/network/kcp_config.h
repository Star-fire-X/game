/**
 * @file kcp_config.h
 * @brief KCP configuration defaults.
 */

#ifndef MIR2_COMMON_NETWORK_KCP_CONFIG_H_
#define MIR2_COMMON_NETWORK_KCP_CONFIG_H_

#include <cstdint>

namespace mir2::common {

/**
 * @brief KCP configuration parameters.
 */
struct KcpConfig {
  int32_t nodelay = 1;
  int32_t interval = 10;
  int32_t resend = 2;
  int32_t nc = 1;

  int32_t snd_wnd = 128;
  int32_t rcv_wnd = 128;
  int32_t mtu = 1400;

  uint32_t timeout_ms = 5000;
  uint32_t recovery_interval_ms = 30000;
};

}  // namespace mir2::common

#endif  // MIR2_COMMON_NETWORK_KCP_CONFIG_H_
