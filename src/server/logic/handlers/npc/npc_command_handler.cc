#include "handlers/npc/npc_command_handler.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>

#include "ecs/components/character_components.h"
#include "log/logger.h"

namespace mir2::handlers {
namespace {

constexpr int kMaxRandomAttempts = 64;

std::string ToUpper(std::string_view value) {
  std::string result(value.begin(), value.end());
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return result;
}

bool ParseInt32(std::string_view value, int32_t* out) {
  if (!out) {
    return false;
  }
  int32_t parsed = 0;
  const char* start = value.data();
  const char* end = start + value.size();
  auto [ptr, ec] = std::from_chars(start, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return false;
  }
  *out = parsed;
  return true;
}

}  // namespace

NpcCommandHandler::NpcCommandHandler(entt::registry& registry,
                                     game::map::SceneManager& scene_manager)
    : registry_(registry),
      scene_manager_(scene_manager),
      rng_(std::random_device{}()) {}

std::optional<game::map::TeleportCommand> NpcCommandHandler::HandleCommand(
    entt::entity player,
    std::string_view command,
    const std::vector<std::string>& args) {
  const std::string op = ToUpper(command);
  if (op == "QA_MAPMOVE") {
    return HandleMapMove(player, args);
  }
  if (op == "QA_MAPRANDOM") {
    return HandleMapRandom(player);
  }
  return std::nullopt;
}

std::optional<game::map::TeleportCommand> NpcCommandHandler::HandleMapMove(
    entt::entity player,
    const std::vector<std::string>& args) {
  if (args.size() < 3) {
    SYSLOG_WARN("NpcCommandHandler: QA_MAPMOVE missing args");
    return std::nullopt;
  }

  int32_t map_id = 0;
  int32_t x = 0;
  int32_t y = 0;
  if (!ParseInt32(args[0], &map_id) ||
      !ParseInt32(args[1], &x) ||
      !ParseInt32(args[2], &y)) {
    SYSLOG_WARN("NpcCommandHandler: QA_MAPMOVE invalid args");
    return std::nullopt;
  }

  return game::map::TeleportCommand{player, map_id, x, y};
}

std::optional<game::map::TeleportCommand> NpcCommandHandler::HandleMapRandom(
    entt::entity player) {
  auto* map = scene_manager_.GetMapByEntity(player);
  if (!map) {
    SYSLOG_WARN("NpcCommandHandler: QA_MAPRANDOM player not in map");
    return std::nullopt;
  }

  const int32_t width = map->GetMapWidth();
  const int32_t height = map->GetMapHeight();
  if (width <= 0 || height <= 0) {
    SYSLOG_WARN("NpcCommandHandler: QA_MAPRANDOM invalid map bounds");
    return std::nullopt;
  }

  std::uniform_int_distribution<int32_t> dist_x(0, width - 1);
  std::uniform_int_distribution<int32_t> dist_y(0, height - 1);

  for (int attempt = 0; attempt < kMaxRandomAttempts; ++attempt) {
    const int32_t x = dist_x(rng_);
    const int32_t y = dist_y(rng_);
    if (map->IsWalkable(x, y)) {
      return game::map::TeleportCommand{player, map->GetMapId(), x, y};
    }
  }

  auto* state = registry_.try_get<ecs::CharacterStateComponent>(player);
  if (state) {
    return game::map::TeleportCommand{
        player,
        static_cast<int32_t>(state->map_id),
        state->position.x,
        state->position.y};
  }

  SYSLOG_WARN("NpcCommandHandler: QA_MAPRANDOM failed to find walkable tile");
  return std::nullopt;
}

}  // namespace mir2::handlers
