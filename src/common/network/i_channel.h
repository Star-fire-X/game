/**
 * @file i_channel.h
 * @brief Common network channel interface.
 */

#ifndef MIR2_COMMON_NETWORK_I_CHANNEL_H_
#define MIR2_COMMON_NETWORK_I_CHANNEL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace mir2::common {

/**
 * @brief Channel type.
 */
enum class ChannelType : uint8_t {
  kTcp = 0,
  kKcp = 1
};

/**
 * @brief Channel connection state.
 */
enum class ChannelState : uint8_t {
  Disconnected = 0,
  Connecting = 1,
  Connected = 2,
  Disconnecting = 3
};

/**
 * @brief Abstract network channel interface.
 */
class IChannel {
 public:
  virtual ~IChannel() = default;

  virtual bool Connect(const std::string& host, uint16_t port) = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  virtual void Send(uint16_t msg_id, const std::vector<uint8_t>& payload) = 0;
  virtual void Update() = 0;

  virtual ChannelType Type() const = 0;
};

}  // namespace mir2::common

#endif  // MIR2_COMMON_NETWORK_I_CHANNEL_H_
