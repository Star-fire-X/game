/**
 * @file npc_command_handler.h
 * @brief NPC command handler(s).
 */

#ifndef MIR2_LOGIC_HANDLERS_NPC_NPC_COMMAND_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_NPC_NPC_COMMAND_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "game/map/scene_manager.h"
#include "game/map/teleport_command.h"
#include "logic/handler_context.h"
#include "logic/task.h"

namespace mir2::logic {

class CoroutineExecutor;
class ResponseSender;

class NpcCommandHandler {
 public:
  NpcCommandHandler(CoroutineExecutor& executor,
                    ResponseSender& response_sender,
                    entt::registry& registry,
                    game::map::SceneManager& scene_manager);

  Task<void> HandleMessage(HandlerContext ctx,
                           uint16_t msg_id,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  Task<void> HandleNpcInteract(HandlerContext ctx,
                               const uint8_t* payload,
                               size_t payload_size);
  Task<void> HandleNpcMenuSelect(HandlerContext ctx,
                                 const uint8_t* payload,
                                 size_t payload_size);
  Task<void> SendNpcInteractResponse(HandlerContext ctx,
                                     uint64_t npc_id,
                                     uint8_t result,
                                     const std::string& npc_name,
                                     uint8_t npc_type);

  CoroutineExecutor& executor_;
  ResponseSender& response_sender_;
  entt::registry& registry_;
  game::map::SceneManager& scene_manager_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_NPC_NPC_COMMAND_HANDLER_H_
