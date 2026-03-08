/**
 * @file message_router.h
 * @brief 网关消息路由器（已废弃，保留用于兼容）
 * 
 * 双进程架构下，Gateway 无脑转发所有消息到 LogicServer，
 * 不再需要路由表和认证检查。
 */

#ifndef MIR2_GATEWAY_MESSAGE_ROUTER_H_
#define MIR2_GATEWAY_MESSAGE_ROUTER_H_

namespace mir2::gateway {

/**
 * @brief 消息路由器（已废弃）
 * 
 * 双进程架构下，所有消息统一转发到 LogicServer。
 * 认证检查由 LogicServer 负责。
 * 
 * @deprecated 保留此类仅为兼容性，实际不再使用。
 */
class MessageRouter {
 public:
  MessageRouter() = default;
  ~MessageRouter() = default;

  MessageRouter(const MessageRouter&) = delete;
  MessageRouter& operator=(const MessageRouter&) = delete;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_MESSAGE_ROUTER_H_
