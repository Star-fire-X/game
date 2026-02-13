/**
 * @file guild_handler.h
 * @brief Guild handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_GUILD_GUILD_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_GUILD_GUILD_HANDLER_H_

#include <cstddef>
#include <cstdint>

#include <entt/entt.hpp>

#include "logic/services/client_registry.h"
#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::ecs {
class GuildSystem;
}  // namespace mir2::ecs

namespace mir2::logic {

class CoroutineExecutor;
class PlayerPresenceService;
class ResponseSender;

class GuildHandler {
 public:
  GuildHandler(CoroutineExecutor& executor,
               ResponseSender& response_sender,
               ClientRegistry& registry,
               PlayerPresenceService& player_presence_service,
               mir2::ecs::GuildSystem& guild_system,
               entt::registry& ecs_registry);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  Task<void> HandleCreateGuild(HandlerContext ctx,
                               const uint8_t* payload,
                               size_t payload_size);
  Task<void> HandleJoinGuild(HandlerContext ctx,
                             const uint8_t* payload,
                             size_t payload_size);
  Task<void> HandleLeaveGuild(HandlerContext ctx,
                              const uint8_t* payload,
                              size_t payload_size);
  Task<void> HandleDeclareWar(HandlerContext ctx,
                              const uint8_t* payload,
                              size_t payload_size);
  Task<void> HandleCancelWar(HandlerContext ctx,
                             const uint8_t* payload,
                             size_t payload_size);

  Task<void> SendCreateGuildResponse(HandlerContext ctx,
                                     bool success,
                                     int error_code,
                                     uint32_t guild_id = 0);
  Task<void> SendJoinGuildResponse(HandlerContext ctx,
                                   bool success,
                                   int error_code);
  Task<void> SendLeaveGuildResponse(HandlerContext ctx,
                                    bool success,
                                    int error_code);
  Task<void> SendDeclareWarResponse(HandlerContext ctx,
                                    bool success,
                                    int error_code);
  Task<void> SendCancelWarResponse(HandlerContext ctx,
                                   bool success,
                                   int error_code);

  ResponseSender& response_sender_;
  PlayerPresenceService& player_presence_service_;
  mir2::ecs::GuildSystem& guild_system_;
  entt::registry& ecs_registry_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_GUILD_GUILD_HANDLER_H_
