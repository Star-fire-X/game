/**
 * @file channel_router.h
 * @brief Message routing between TCP and KCP channels.
 */

#ifndef MIR2_COMMON_NETWORK_CHANNEL_ROUTER_H_
#define MIR2_COMMON_NETWORK_CHANNEL_ROUTER_H_

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "common/network/i_channel.h"

namespace mir2::common {

/**
 * @brief Routes message ids to network channels.
 */
class ChannelRouter {
 public:
  ChannelRouter();

  ChannelType GetChannel(uint16_t msg_id) const;
  ChannelType GetChannelWithFallback(uint16_t msg_id, bool kcp_available) const;

  void SetRoute(uint16_t msg_id, ChannelType channel);
  void SetDefaultChannel(ChannelType channel);

 private:
  void InitializeDefaults();
  void AddRange(uint16_t start, uint16_t end, ChannelType channel);

  mutable std::mutex mutex_;
  std::unordered_map<uint16_t, ChannelType> routes_;
  ChannelType default_channel_ = ChannelType::kTcp;
};

}  // namespace mir2::common

#endif  // MIR2_COMMON_NETWORK_CHANNEL_ROUTER_H_
