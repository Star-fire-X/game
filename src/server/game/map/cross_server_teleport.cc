#include "game/map/cross_server_teleport.h"

#include <mutex>
#include <unordered_map>

#include "log/logger.h"

namespace mir2::game::map {
namespace {

struct PendingRequest {
  std::string target_server;
  std::string target_map;
  int32_t target_x = 0;
  int32_t target_y = 0;
};

std::mutex& PendingMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<uint32_t, PendingRequest>& PendingRequests() {
  static std::unordered_map<uint32_t, PendingRequest> pending;
  return pending;
}

}  // namespace

bool CrossServerTeleport::RequestCrossServerTeleport(
    entt::entity entity,
    const std::string& target_server,
    const std::string& target_map,
    int32_t target_x,
    int32_t target_y) {
  const uint32_t entity_id = static_cast<uint32_t>(entt::to_integral(entity));
  {
    std::lock_guard<std::mutex> lock(PendingMutex());
    PendingRequests()[entity_id] = {target_server, target_map, target_x, target_y};
  }

  SYSLOG_INFO("CrossServerTeleport: request entity={} server={} map={} ({}, {})",
              entity_id, target_server, target_map, target_x, target_y);

  // TODO: integrate with Gateway to send cross-server teleport request.
  return true;
}

void CrossServerTeleport::HandleCrossServerResponse(
    entt::entity entity,
    bool success,
    const std::string& error_msg) {
  const uint32_t entity_id = static_cast<uint32_t>(entt::to_integral(entity));
  PendingRequest request;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(PendingMutex());
    auto& pending = PendingRequests();
    auto it = pending.find(entity_id);
    if (it != pending.end()) {
      request = it->second;
      pending.erase(it);
      found = true;
    }
  }

  if (success) {
    if (found) {
      SYSLOG_INFO("CrossServerTeleport: success entity={} server={} map={} ({}, {})",
                  entity_id, request.target_server, request.target_map,
                  request.target_x, request.target_y);
    } else {
      SYSLOG_INFO("CrossServerTeleport: success entity={} (no pending request)",
                  entity_id);
    }
    // TODO: finalize teleport state once Gateway integration is available.
    return;
  }

  SYSLOG_WARN("CrossServerTeleport: failed entity={} error={}", entity_id, error_msg);
  // TODO: send error message to client once messaging integration is available.
}

}  // namespace mir2::game::map
