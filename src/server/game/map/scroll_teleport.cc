#include "game/map/scroll_teleport.h"

#include <limits>
#include <random>
#include <string>
#include <utility>

#include "core/utils.h"

namespace mir2::game::map {
namespace {

constexpr int32_t kMaxRandomAttempts = 30;
constexpr int64_t kSiegeCooldownMs = 10000;

bool TryParseMapId(const std::string& value, int32_t* out) {
  if (!out) {
    return false;
  }
  try {
    size_t pos = 0;
    const long long parsed = std::stoll(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    if (parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max()) {
      return false;
    }
    *out = static_cast<int32_t>(parsed);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::mt19937& ScrollRng() {
  static thread_local std::mt19937 rng(std::random_device{}());
  return rng;
}

std::optional<std::pair<int32_t, int32_t>> FindRandomWalkablePosition(
    int32_t map_width,
    int32_t map_height,
    const ScrollTeleport::WalkableChecker& walkable_checker) {
  if (map_width <= 0 || map_height <= 0 || !walkable_checker) {
    return std::nullopt;
  }

  std::uniform_int_distribution<int32_t> dist_x(0, map_width - 1);
  std::uniform_int_distribution<int32_t> dist_y(0, map_height - 1);

  auto& rng = ScrollRng();
  for (int32_t attempt = 0; attempt < kMaxRandomAttempts; ++attempt) {
    const int32_t x = dist_x(rng);
    const int32_t y = dist_y(rng);
    if (walkable_checker(x, y)) {
      return std::make_pair(x, y);
    }
  }

  for (int32_t y = 0; y < map_height; ++y) {
    for (int32_t x = 0; x < map_width; ++x) {
      if (walkable_checker(x, y)) {
        return std::make_pair(x, y);
      }
    }
  }

  return std::nullopt;
}

}  // namespace

std::optional<TeleportCommand> ScrollTeleport::UseTownScroll(
    entt::entity entity,
    const std::string& current_map,
    int32_t pk_level,
    const MapAttributes& attrs) {
  const bool use_pk_village = pk_level >= 2;
  const std::string& target_map =
      use_pk_village ? attrs.pk_village_map : attrs.home_map;
  const int32_t target_x = use_pk_village ? attrs.pk_village_x : attrs.home_x;
  const int32_t target_y = use_pk_village ? attrs.pk_village_y : attrs.home_y;

  int32_t map_id = 0;
  if (!TryParseMapId(target_map, &map_id)) {
    (void)current_map;
    return std::nullopt;
  }

  return TeleportCommand{entity, map_id, target_x, target_y};
}

std::optional<TeleportCommand> ScrollTeleport::UseDungeonScroll(
    entt::entity entity,
    const std::string& current_map,
    uint64_t last_use_time,
    bool in_siege,
    const MapAttributes& attrs,
    int32_t map_width,
    int32_t map_height,
    WalkableChecker walkable_checker) {
  if (attrs.no_random_move) {
    return std::nullopt;
  }

  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  if (in_siege) {
    const int64_t elapsed = now_ms - static_cast<int64_t>(last_use_time);
    if (elapsed < kSiegeCooldownMs) {
      return std::nullopt;
    }
  }

  int32_t map_id = 0;
  if (!TryParseMapId(current_map, &map_id)) {
    return std::nullopt;
  }

  auto position_opt =
      FindRandomWalkablePosition(map_width, map_height, walkable_checker);
  if (!position_opt) {
    return std::nullopt;
  }

  return TeleportCommand{entity, map_id, position_opt->first, position_opt->second};
}

}  // namespace mir2::game::map
