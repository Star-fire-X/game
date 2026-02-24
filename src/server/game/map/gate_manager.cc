/**
 * @file gate_manager.cc
 * @brief 传送门管理器实现
 */

#include "game/map/gate_manager.h"

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

#include <yaml-cpp/yaml.h>

#include "log/logger.h"

namespace mir2::game::map {

namespace {

template <typename T>
T ReadOrDefault(const YAML::Node& node, const char* key, const T& default_value) {
  if (node && node[key]) {
    try {
      return node[key].as<T>();
    } catch (const std::exception&) {
      return default_value;
    }
  }
  return default_value;
}

std::string ReadString(const YAML::Node& node,
                       const char* key,
                       const std::string& default_value) {
  if (!node || !node[key]) {
    return default_value;
  }

  const YAML::Node value = node[key];
  if (!value.IsScalar()) {
    return default_value;
  }

  try {
    return value.as<std::string>();
  } catch (const std::exception&) {
    try {
      return std::to_string(value.as<int64_t>());
    } catch (const std::exception&) {
      return default_value;
    }
  }
}

std::optional<std::filesystem::path> ResolveAllowedConfigBase() {
  std::error_code cwd_ec;
  const std::filesystem::path cwd = std::filesystem::current_path(cwd_ec);
  if (cwd_ec) {
    return std::nullopt;
  }

  std::filesystem::path configured_base;
  if (const char* env_base = std::getenv("LEGEND2_CONFIG_BASE_PATH");
      env_base != nullptr && env_base[0] != '\0') {
    configured_base = std::filesystem::path(env_base);
    if (!configured_base.is_absolute()) {
      configured_base = (cwd / configured_base).lexically_normal();
    }
  } else {
    configured_base = cwd / "config";
  }

  std::error_code canonical_ec;
  const std::filesystem::path canonical_base =
      std::filesystem::canonical(configured_base, canonical_ec);
  if (canonical_ec) {
    return std::nullopt;
  }

  return canonical_base;
}

const std::optional<std::filesystem::path>& GetAllowedConfigBase() {
  static const std::optional<std::filesystem::path> kAllowedBase =
      ResolveAllowedConfigBase();
  return kAllowedBase;
}

bool IsPathWithinBase(const std::filesystem::path& path,
                      const std::filesystem::path& base) {
  std::error_code rel_ec;
  const std::filesystem::path rel_path =
      std::filesystem::relative(path, base, rel_ec);
  if (rel_ec || rel_path.empty() || rel_path.is_absolute()) {
    return false;
  }

  for (const auto& component : rel_path) {
    if (component == "..") {
      return false;
    }
  }
  return true;
}

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
    numeric_coord_index_[MakeNumericCoordKey(*numeric_map_id, gate.source_x,
                                             gate.source_y)] = index;
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

void GateManager::LoadFromConfig(const std::string& config_path) {
  gates_.clear();
  map_gates_.clear();
  coord_index_.clear();
  numeric_coord_index_.clear();

  try {
    if (config_path.empty()) {
      return;
    }

    // Path validation
    std::filesystem::path path(config_path);
    const bool is_absolute_path = path.is_absolute();
    std::error_code abs_ec;
    if (!is_absolute_path) {
      path = std::filesystem::absolute(path, abs_ec);
      if (abs_ec) {
        return;
      }
    }

    std::error_code canonical_ec;
    path = std::filesystem::canonical(path, canonical_ec);
    if (canonical_ec) {
      return;
    }

    // Relative config paths are restricted to the configured base.
    // Absolute paths are treated as explicit trusted targets from callers.
    if (!is_absolute_path) {
      const auto& allowed_base = GetAllowedConfigBase();
      if (!allowed_base.has_value()) {
        return;
      }

      if (!IsPathWithinBase(path, *allowed_base)) {
        return;
      }
    }

    if (!std::filesystem::exists(path)) {
      return;
    }

    YAML::Node root = YAML::LoadFile(path.string());
    YAML::Node gates_node = root["gates"] ? root["gates"] : root;
    if (!gates_node || !gates_node.IsSequence()) {
      return;
    }

    for (const auto& node : gates_node) {
      if (!node || !node.IsMap()) {
        continue;
      }

      GateInfo gate{};
      gate.gate_id = ReadOrDefault(node, "id", gate.gate_id);
      if (gate.gate_id == 0 && node["gate_id"]) {
        gate.gate_id = node["gate_id"].as<uint32_t>();
      }

      gate.source_map = ReadString(node, "source_map", gate.source_map);
      gate.source_x = ReadOrDefault(node, "source_x", gate.source_x);
      gate.source_y = ReadOrDefault(node, "source_y", gate.source_y);
      gate.target_map = ReadString(node, "target_map", gate.target_map);
      gate.target_x = ReadOrDefault(node, "target_x", gate.target_x);
      gate.target_y = ReadOrDefault(node, "target_y", gate.target_y);
      gate.require_item = ReadOrDefault(node, "require_item", gate.require_item);
      gate.required_item_id = ReadOrDefault(node, "required_item_id", gate.required_item_id);

      if (gate.source_map.empty() || gate.target_map.empty()) {
        continue;
      }

      AddGate(gate);
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("Gate config load failed: {}", ex.what());
  }
}

}  // namespace mir2::game::map
