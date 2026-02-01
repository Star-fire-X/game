/**
 * @file message_router.h
 * @brief 网关消息路由表
 */

#ifndef MIR2_GATEWAY_MESSAGE_ROUTER_H
#define MIR2_GATEWAY_MESSAGE_ROUTER_H

#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/enums.h"

namespace mir2::gateway {

/**
 * @brief 消息路由规则
 */
struct MessageRoute {
  uint16_t msg_id;
  common::ServiceType target_service;
  bool require_auth;  // 是否需要认证后才能转发

  MessageRoute() = default;
  MessageRoute(uint16_t id, common::ServiceType service, bool auth = false)
      : msg_id(id), target_service(service), require_auth(auth) {}
};

/**
 * @brief 消息路由器
 *
 * 负责管理消息 ID 到后端服务的映射关系，支持动态配置与热更新。
 */
class MessageRouter {
 public:
  MessageRouter() = default;
  ~MessageRouter() = default;

  MessageRouter(const MessageRouter&) = delete;
  MessageRouter& operator=(const MessageRouter&) = delete;

  /**
   * @brief 注册消息路由规则
   * @param msg_id 消息ID
   * @param service 目标服务类型
   * @param require_auth 是否需要认证
   */
  void RegisterRoute(uint16_t msg_id, common::ServiceType service, bool require_auth = false);

  /**
   * @brief 获取消息的目标服务
   * @param msg_id 消息ID
   * @return 目标服务类型，如果未找到返回 std::nullopt
   */
  std::optional<common::ServiceType> GetRouteTarget(uint16_t msg_id) const;

  /**
   * @brief 检查消息是否需要认证
   * @param msg_id 消息ID
   * @return 是否需要认证，默认不需要
   */
  bool RequiresAuth(uint16_t msg_id) const;

  /**
   * @brief 从配置加载路由表
   * @param config_path 配置文件路径
   * @return 是否加载成功
   */
  bool LoadRoutesFromConfig(const std::string& config_path);

  /**
   * @brief 清空所有路由规则
   */
  void Clear();

  /**
   * @brief 获取已注册路由数量
   */
  size_t GetRouteCount() const;

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<uint16_t, MessageRoute> routes_;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_MESSAGE_ROUTER_H
