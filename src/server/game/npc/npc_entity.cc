/**
 * @file npc_entity.cc
 * @brief NPC entity implementation.
 */

#include "game/npc/npc_entity.h"

#include "log/logger.h"

namespace mir2::game::npc {

NpcEntity::NpcEntity(uint64_t id) {
  data_.id = id;
}

void NpcEntity::ApplyConfig(const NpcConfig& config) {
  if (config.id != 0 && data_.id != 0 && config.id != data_.id) {
    SYSLOG_ERROR("NpcEntity: cannot change ID from {} to {}", data_.id, config.id);
    return;
  }
  if (config.id != 0 && data_.id == 0) {
    data_.id = config.id;
  }
  data_.template_id = config.template_id;
  data_.name = config.name;
  data_.type = config.type;
  data_.map_id = config.map_id;
  data_.x = config.x;
  data_.y = config.y;
  data_.direction = config.direction;
  data_.enabled = config.enabled;
  data_.script_id = config.script_id;
  data_.store_id = config.store_id;
  data_.guild_id = config.guild_id;
  data_.teleport_target = config.teleport_target;
}

void NpcEntity::SetPosition(int32_t x, int32_t y) {
  if (x < kMinCoordinate || y < kMinCoordinate) {
    SYSLOG_WARN("NpcEntity {}: invalid position ({}, {}), ignoring", data_.id, x, y);
    return;
  }
  if (x > kMaxCoordinate || y > kMaxCoordinate) {
    SYSLOG_WARN("NpcEntity {}: position ({}, {}) exceeds max bounds", data_.id, x, y);
    return;
  }
  data_.x = x;
  data_.y = y;
}

void NpcEntity::SetMap(uint32_t map_id, int32_t x, int32_t y) {
  if (x < kMinCoordinate || y < kMinCoordinate || x > kMaxCoordinate || y > kMaxCoordinate) {
    SYSLOG_WARN("NpcEntity {}: invalid map/position map={} ({}, {})", data_.id, map_id, x, y);
    return;
  }
  data_.map_id = map_id;
  data_.x = x;
  data_.y = y;
}

void NpcEntity::SetDirection(uint8_t direction) {
  data_.direction = direction;
}

void NpcEntity::SetScriptId(const std::string& script_id) {
  data_.script_id = script_id;
}

void NpcEntity::SetEnabled(bool enabled) {
  data_.enabled = enabled;
}

void NpcEntity::SetType(NpcType type) {
  data_.type = type;
}

}  // namespace mir2::game::npc
