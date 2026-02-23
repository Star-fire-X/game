/**
 * @file party_handler.h
 * @brief Party handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_PARTY_PARTY_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_PARTY_PARTY_HANDLER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::ecs {
struct PartyComponent;
}  // namespace mir2::ecs

namespace mir2::proto {
class PartyInviteReq;
class PartyJoinReq;
class PartyLeaveReq;
class PartyKickReq;
}  // namespace mir2::proto

namespace mir2::logic {

class ClientRegistry;
class ResponseSender;
class RoleStore;

class PartyHandler {
 public:
  PartyHandler(ResponseSender& response_sender,
               ClientRegistry& client_registry,
               entt::registry& ecs_registry,
               RoleStore* role_store);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  struct PartyMemberView {
    entt::entity entity = entt::null;
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    std::string name;
    uint32_t hp = 0;
    uint32_t max_hp = 0;
    uint32_t map_id = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    bool online = false;
  };

  struct PartySnapshot {
    entt::entity party_entity = entt::null;
    uint32_t party_id = 0;
    uint64_t leader_character_id = 0;
    std::vector<PartyMemberView> members;
  };

  Task<void> HandleInvite(HandlerContext ctx,
                          const mir2::proto::PartyInviteReq* req);
  Task<void> HandleJoin(HandlerContext ctx,
                        const mir2::proto::PartyJoinReq* req);
  Task<void> HandleLeave(HandlerContext ctx,
                         const mir2::proto::PartyLeaveReq* req);
  Task<void> HandleKick(HandlerContext ctx,
                        const mir2::proto::PartyKickReq* req);

  Task<void> SendInviteRsp(uint64_t client_id,
                           bool success,
                           mir2::common::ErrorCode code);
  Task<void> SendJoinRsp(uint64_t client_id,
                         bool success,
                         mir2::common::ErrorCode code);
  Task<void> SendLeaveRsp(uint64_t client_id,
                          bool success,
                          mir2::common::ErrorCode code);
  Task<void> SendKickRsp(uint64_t client_id,
                         bool success,
                         mir2::common::ErrorCode code);
  Task<void> SendPartyUpdate(const PartySnapshot& snapshot);
  Task<void> SendPartyClear(uint64_t client_id);

  std::optional<uint64_t> GetCharacterId(entt::entity entity) const;
  std::optional<uint64_t> GetClientIdByCharacterId(uint64_t character_id) const;
  std::optional<entt::entity> FindEntityByCharacterId(uint64_t character_id,
                                                       bool require_online) const;
  std::optional<entt::entity> FindPartyEntityById(uint32_t party_id) const;
  std::optional<entt::entity> FindPartyEntityByMember(entt::entity member) const;
  std::optional<PartySnapshot> BuildPartySnapshot(entt::entity party_entity) const;
  entt::entity EnsurePartyForLeader(entt::entity leader);
  void RemovePartyMember(entt::entity member);
  void ReassignLeaderIfNeeded(entt::entity party_entity);

  ResponseSender& response_sender_;
  ClientRegistry& client_registry_;
  entt::registry& ecs_registry_;
  RoleStore* role_store_ = nullptr;
  std::atomic<uint32_t> next_party_id_{1};
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_PARTY_PARTY_HANDLER_H_
