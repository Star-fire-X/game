/**
 * @file gate_manager.cc
 * @brief 传送门管理器实现
 */

#include "game/map/gate_manager.h"

#include <cstdlib>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "log/logger.h"

namespace mir2::game::map {

namespace {

std::optional<int32_t> TryParseMapId(std::string_view value) {
  if (value.empty()) {
    return std::nullopt;
  }

  constexpr int32_t kMaxDigits = 10;
  if (value.size() > static_cast<size_t>(kMaxDigits)) {
    return std::nullopt;
  }

  int32_t parsed = 0;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    const int32_t digit = ch - '0';
    if (parsed > (std::numeric_limits<int32_t>::max() - digit) / 10) {
      return std::nullopt;
    }
    parsed = parsed * 10 + digit;
  }
  return parsed;
}

}  // namespace

void GateManager::AddGate(const GateInfo& gate) {
  const size_t index = gates_.size();
  gates_.push_back(gate);
  map_gates_[gate.source_map].push_back(index);

  coord_index_[MakeCoordKey(gate.source_map, gate.source_x, gate.source_y)] = index;
  if (const auto numeric_map_id = TryParseMapId(gate.source_map);
      numeric_map_id.has_value()) {
    numeric_coord_index_[MakeNumericCoordKey(*numeric_map_id,
                                             gate.source_x,
                                             gate.source_y)] = index;
  }
}

void GateManager::ReplaceAllGates(const std::vector<GateInfo>& gates) {
  gates_.clear();
  map_gates_.clear();
  coord_index_.clear();
  numeric_coord_index_.clear();

  std::unordered_set<uint32_t> seen_gate_ids;
  for (const auto& gate : gates) {
    if (gate.gate_id == 0 || gate.source_map.empty() || gate.target_map.empty()) {
      SYSLOG_WARN("Skip invalid gate during ReplaceAllGates gate_id={}", gate.gate_id);
      continue;
    }
    if (!seen_gate_ids.insert(gate.gate_id).second) {
      SYSLOG_WARN("Skip duplicate gate_id during ReplaceAllGates gate_id={}",
                  gate.gate_id);
      continue;
    }
    if (coord_index_.contains(MakeCoordKey(gate.source_map, gate.source_x, gate.source_y))) {
      SYSLOG_WARN("Skip duplicate gate source during ReplaceAllGates source_map={} x={} y={}",
                  gate.source_map,
                  gate.source_x,
                  gate.source_y);
      continue;
    }
    AddGate(gate);
  }
}

std::optional<GateInfo> GateManager::CheckGateTrigger(const std::string& map_id,
                                                      int32_t x,
                                                      int32_t y) const {
  const auto it = coord_index_.find(MakeCoordKey(map_id, x, y));
  if (it != coord_index_.end() && it->second < gates_.size()) {
    return gates_[it->second];
  }

  if (const auto numeric_map_id = TryParseMapId(map_id);
      numeric_map_id.has_value()) {
    const auto numeric_it = numeric_coord_index_.find(
        MakeNumericCoordKey(*numeric_map_id, x, y));
    if (numeric_it != numeric_coord_index_.end() &&
        numeric_it->second < gates_.size()) {
      return gates_[numeric_it->second];
    }
  }

  return std::nullopt;
}

std::optional<GateInfo> GateManager::CheckGateTrigger(int32_t map_id,
                                                      int32_t x,
                                                      int32_t y) const {
  const auto it = numeric_coord_index_.find(MakeNumericCoordKey(map_id, x, y));
  if (it != numeric_coord_index_.end() && it->second < gates_.size()) {
    return gates_[it->second];
  }

  return std::nullopt;
}

}  // namespace mir2::game::map
