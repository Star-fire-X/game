/**
 * @file chat_handler.h
 * @brief Chat handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_CHAT_CHAT_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_CHAT_CHAT_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <entt/entt.hpp>

#include "chat_generated.h"
#include "game/chat/chat_service.h"
#include "logic/services/client_registry.h"
#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::game::map {
class AOIManager;
}

namespace mir2::logic {

class CoroutineExecutor;
class PlayerPresenceService;
class ResponseSender;

class ChatHandler {
 public:
  ChatHandler(CoroutineExecutor& executor,
              ResponseSender& response_sender,
              ClientRegistry& registry,
              PlayerPresenceService& player_presence_service,
              mir2::game::map::AOIManager& aoi_mgr,
              entt::registry& ecs_registry);
  ~ChatHandler();

  Task<void> HandleMessage(HandlerContext ctx, const uint8_t* payload, size_t payload_size);
  Task<void> HandleHot(HandlerContext ctx,
                       mir2::proto::ChatChannel channel,
                       uint64_t target_id,
                       std::string content);

 private:
  Task<void> HandleWorldChat(HandlerContext ctx, const std::string& content);
  Task<void> HandlePrivateChat(HandlerContext ctx, uint64_t target_id, const std::string& content);
  Task<void> HandleTeamChat(HandlerContext ctx, const std::string& content);
  Task<void> HandleAreaChat(HandlerContext ctx, const std::string& content);
  Task<void> HandleGuildChat(HandlerContext ctx, const std::string& content);
  Task<void> HandleChatCommand(HandlerContext ctx, const std::string& content);

  Task<void> SendChatResponse(HandlerContext ctx, mir2::common::ErrorCode code);
  Task<void> SendChatDispatches(uint16_t msg_id,
                                const mir2::game::chat::ChatDispatchList& dispatches);

  CoroutineExecutor& executor_;
  ResponseSender& response_sender_;
  ClientRegistry& client_registry_;
  mir2::game::map::AOIManager& aoi_mgr_;
  entt::registry& ecs_registry_;
  std::unique_ptr<mir2::game::chat::ChatService> chat_service_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_CHAT_CHAT_HANDLER_H_
