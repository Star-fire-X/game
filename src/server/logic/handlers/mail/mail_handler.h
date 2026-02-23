/**
 * @file mail_handler.h
 * @brief Mail handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_MAIL_MAIL_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_MAIL_MAIL_HANDLER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::proto {
class MailSendReq;
class MailListReq;
class MailReadReq;
class MailDeleteReq;
class MailClaimReq;
}  // namespace mir2::proto

namespace mir2::db {
class PgConnectionPool;
}  // namespace mir2::db

namespace mir2::logic {

class ClientRegistry;
class ResponseSender;
class RoleStore;

class MailHandler {
 public:
  MailHandler(ResponseSender& response_sender,
              ClientRegistry& client_registry,
              entt::registry& ecs_registry,
              RoleStore* role_store,
              std::shared_ptr<mir2::db::PgConnectionPool> db_pool = nullptr);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 public:
  struct MailAttachmentRecord {
    uint32_t item_id = 0;
    uint32_t count = 0;
  };

  struct MailRecord {
    uint64_t mail_id = 0;
    uint64_t from_character_id = 0;
    uint64_t to_character_id = 0;
    std::string subject;
    std::string content;
    uint32_t gold = 0;
    std::vector<MailAttachmentRecord> items;
    bool is_read = false;
    bool claimed = false;
    uint64_t send_time = 0;
    uint64_t expire_time = 0;
  };

 private:
  Task<void> HandleSend(HandlerContext ctx, const mir2::proto::MailSendReq* req);
  Task<void> HandleList(HandlerContext ctx, const mir2::proto::MailListReq* req);
  Task<void> HandleRead(HandlerContext ctx, const mir2::proto::MailReadReq* req);
  Task<void> HandleDelete(HandlerContext ctx, const mir2::proto::MailDeleteReq* req);
  Task<void> HandleClaim(HandlerContext ctx, const mir2::proto::MailClaimReq* req);

  Task<void> SendMailSendRsp(uint64_t client_id,
                             bool success,
                             mir2::common::ErrorCode code,
                             uint64_t mail_id);
  Task<void> SendMailListRsp(uint64_t client_id,
                             bool success,
                             mir2::common::ErrorCode code,
                             const std::vector<MailRecord>& mails);
  Task<void> SendMailReadRsp(uint64_t client_id,
                             bool success,
                             mir2::common::ErrorCode code,
                             const std::optional<MailRecord>& mail);
  Task<void> SendMailDeleteRsp(uint64_t client_id,
                               bool success,
                               mir2::common::ErrorCode code,
                               uint64_t mail_id);
  Task<void> SendMailClaimRsp(uint64_t client_id,
                              bool success,
                              mir2::common::ErrorCode code,
                              uint64_t mail_id);
  Task<void> SendMailNotify(uint64_t client_id,
                            const MailRecord& mail,
                            uint32_t unread_count);

  std::optional<uint64_t> ResolveCharacterId(HandlerContext ctx) const;
  uint64_t ResolveClientIdByCharacterId(uint64_t character_id) const;
  uint64_t DefaultExpireTime(uint64_t now_ms) const;
  void PurgeExpiredMails(std::vector<MailRecord>* mails, uint64_t now_ms) const;
  uint32_t CountUnread(const std::vector<MailRecord>& mails) const;
  bool PersistenceEnabled() const;
  void BootstrapPersistence();
  uint64_t NowMs() const;

  ResponseSender& response_sender_;
  ClientRegistry& client_registry_;
  entt::registry& ecs_registry_;
  RoleStore* role_store_ = nullptr;
  std::shared_ptr<mir2::db::PgConnectionPool> db_pool_;
  bool persistence_bootstrapped_ = false;

  std::atomic<uint64_t> next_mail_id_{1};
  mutable std::mutex mailbox_mutex_;
  std::unordered_map<uint64_t, std::vector<MailRecord>> mailbox_by_character_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_MAIL_MAIL_HANDLER_H_
