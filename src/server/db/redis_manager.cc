#include "db/redis_manager.h"

#include <iostream>

namespace mir2::db {

bool RedisManager::Initialize(const config::RedisConfig& config) {
  context_ = redisConnect(config.host.c_str(), config.port);
  if (!context_ || context_->err) {
    std::cerr << "Redis connect failed: "
              << (context_ ? context_->errstr : "null context") << std::endl;
    if (context_) {
      redisFree(context_);
      context_ = nullptr;
    }
    return false;
  }

  if (!config.password.empty()) {
    auto auth_reply = Execute("AUTH " + config.password);
    if (!auth_reply || auth_reply->type == REDIS_REPLY_ERROR) {
      std::cerr << "Redis auth failed" << std::endl;
      Shutdown();
      return false;
    }
  }

  if (config.db != 0) {
    auto select_reply = Execute("SELECT " + std::to_string(config.db));
    if (!select_reply || select_reply->type == REDIS_REPLY_ERROR) {
      std::cerr << "Redis select db failed" << std::endl;
      Shutdown();
      return false;
    }
  }

  return true;
}

void RedisManager::Shutdown() {
  if (context_) {
    redisFree(context_);
    context_ = nullptr;
  }
}

RedisManager::ReplyPtr RedisManager::Execute(const std::string& command) {
  if (!context_) {
    return ReplyPtr(nullptr, freeReplyObject);
  }
  auto* reply = static_cast<redisReply*>(redisCommand(context_, command.c_str()));
  return ReplyPtr(reply, freeReplyObject);
}

}  // namespace mir2::db
