/**
 * @file internal_message_helper.h
 * @brief 内部路由消息编解码
 */

#ifndef MIR2_COMMON_INTERNAL_MESSAGE_HELPER_H
#define MIR2_COMMON_INTERNAL_MESSAGE_HELPER_H

#include <cstdint>
#include <vector>

#include "common/enums.h"

namespace mir2::common {

/**
 * @brief 内部路由消息数据
 */
struct RoutedMessageData {
  uint64_t client_id = 0;
  uint16_t msg_id = 0;
  std::vector<uint8_t> payload;
};

/**
 * @brief 构建服务握手消息
 */
std::vector<uint8_t> BuildServiceHello(ServiceType service);

/**
 * @brief 构建服务握手响应
 */
std::vector<uint8_t> BuildServiceHelloAck(ServiceType service, bool ok);

/**
 * @brief 构建路由消息
 */
std::vector<uint8_t> BuildRoutedMessage(uint64_t client_id, uint16_t msg_id,
                                        const std::vector<uint8_t>& payload);

/**
 * @brief 解析路由消息
 */
bool ParseRoutedMessage(const std::vector<uint8_t>& buffer, RoutedMessageData* out_data);

/**
 * @brief 解析服务握手消息
 */
bool ParseServiceHello(const std::vector<uint8_t>& buffer, ServiceType* out_service);

/**
 * @brief 解析服务握手响应
 */
bool ParseServiceHelloAck(const std::vector<uint8_t>& buffer, ServiceType* out_service, bool* out_ok);

}  // namespace mir2::common

#endif  // MIR2_COMMON_INTERNAL_MESSAGE_HELPER_H
