/**
 * @file message_dispatcher.h
 * @brief 消息分发器
 */

#ifndef MIR2_NETWORK_MESSAGE_DISPATCHER_H
#define MIR2_NETWORK_MESSAGE_DISPATCHER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace mir2::network {

class TcpSession;

/**
 * @brief 消息处理回调
 */
using MessageHandler =
    std::function<void(const std::shared_ptr<TcpSession>&, const std::vector<uint8_t>&)>;

/**
 * @brief 消息分发器
 */
class MessageDispatcher {
 public:
  /**
   * @brief 注册消息处理函数
   */
  void RegisterHandler(uint16_t msg_id, MessageHandler handler);

  /**
   * @brief 分发消息
   */
  void Dispatch(const std::shared_ptr<TcpSession>& session, uint16_t msg_id,
                const std::vector<uint8_t>& payload) const;

 private:
  std::unordered_map<uint16_t, MessageHandler> handlers_;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_MESSAGE_DISPATCHER_H
