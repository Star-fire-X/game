#ifndef MIR2_HANDLERS_NPC_COMMAND_HANDLER_H
#define MIR2_HANDLERS_NPC_COMMAND_HANDLER_H

#include <entt/entt.hpp>

#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "game/map/scene_manager.h"
#include "game/map/teleport_command.h"

namespace mir2::handlers {

class NpcCommandHandler {
 public:
  NpcCommandHandler(entt::registry& registry,
                    game::map::SceneManager& scene_manager);

  std::optional<game::map::TeleportCommand> HandleCommand(
      entt::entity player,
      std::string_view command,
      const std::vector<std::string>& args);

 private:
  std::optional<game::map::TeleportCommand> HandleMapMove(
      entt::entity player,
      const std::vector<std::string>& args);

  std::optional<game::map::TeleportCommand> HandleMapRandom(entt::entity player);

  entt::registry& registry_;
  game::map::SceneManager& scene_manager_;
  std::mt19937 rng_;
};

}  // namespace mir2::handlers

#endif  // MIR2_HANDLERS_NPC_COMMAND_HANDLER_H
