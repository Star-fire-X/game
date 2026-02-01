/**
 * @file redis_manager.h
 * @brief Redis管理器
 */

#ifndef MIR2_DB_REDIS_MANAGER_H
#define MIR2_DB_REDIS_MANAGER_H

#include <memory>
#include <string>

#include <hiredis/hiredis.h>

#include "config/config_manager.h"

namespace mir2::db {

/**
 * @brief Redis管理器
 */
class RedisManager {
 public:
  using ReplyPtr = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

  bool Initialize(const config::RedisConfig& config);
  void Shutdown();

  /**
   * @brief 执行命令（返回结果需检查是否为空）
   */
  ReplyPtr Execute(const std::string& command);

  bool IsReady() const { return context_ != nullptr; }

 private:
  redisContext* context_ = nullptr;
};

}  // namespace mir2::db

#endif  // MIR2_DB_REDIS_MANAGER_H
