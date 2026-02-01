#include "gateway/message_router.h"

#include <fstream>

#include "log/logger.h"
#include "yaml-cpp/yaml.h"

namespace mir2::gateway {

void MessageRouter::RegisterRoute(uint16_t msg_id, common::ServiceType service, bool require_auth) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  routes_[msg_id] = MessageRoute{msg_id, service, require_auth};
  SYSLOG_DEBUG("Registered route: msg_id={} -> service={} require_auth={}",
               msg_id, static_cast<int>(service), require_auth);
}

std::optional<common::ServiceType> MessageRouter::GetRouteTarget(uint16_t msg_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = routes_.find(msg_id);
  if (it != routes_.end()) {
    return it->second.target_service;
  }
  return std::nullopt;
}

bool MessageRouter::RequiresAuth(uint16_t msg_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = routes_.find(msg_id);
  if (it != routes_.end()) {
    return it->second.require_auth;
  }
  return false;
}

bool MessageRouter::LoadRoutesFromConfig(const std::string& config_path) {
  try {
    YAML::Node config = YAML::LoadFile(config_path);
    if (!config["message_routes"]) {
      SYSLOG_WARN("No message_routes section in config: {}", config_path);
      return true;  // 允许没有路由配置（使用默认路由）
    }

    const YAML::Node& routes_node = config["message_routes"];
    std::unique_lock<std::shared_mutex> lock(mutex_);
    routes_.clear();

    for (const auto& route : routes_node) {
      if (!route["msg_id"] || !route["service"]) {
        SYSLOG_ERROR("Invalid route config: missing msg_id or service");
        continue;
      }

      const uint16_t msg_id = route["msg_id"].as<uint16_t>();
      const std::string service_str = route["service"].as<std::string>();
      const bool require_auth = route["require_auth"] ? route["require_auth"].as<bool>() : false;

      common::ServiceType service = common::ServiceType::kGateway;
      if (service_str == "world") {
        service = common::ServiceType::kWorld;
      } else if (service_str == "game") {
        service = common::ServiceType::kGame;
      } else if (service_str == "db") {
        service = common::ServiceType::kDb;
      } else {
        SYSLOG_ERROR("Unknown service type in route config: {}", service_str);
        continue;
      }

      routes_[msg_id] = MessageRoute{msg_id, service, require_auth};
      SYSLOG_INFO("Loaded route: msg_id={} -> service={} require_auth={}",
                  msg_id, service_str, require_auth);
    }

    SYSLOG_INFO("Loaded {} message routes from {}", routes_.size(), config_path);
    return true;
  } catch (const std::exception& e) {
    SYSLOG_ERROR("Failed to load message routes from {}: {}", config_path, e.what());
    return false;
  }
}

void MessageRouter::Clear() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  routes_.clear();
}

size_t MessageRouter::GetRouteCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return routes_.size();
}

}  // namespace mir2::gateway
