#include "logic/handlers/mail/mail_handler.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"
#include "mail_generated.h"

namespace mir2::logic {

namespace {

constexpr uint64_t kDefaultMailExpireMs = 7ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
const char* kMailSelectColumns =
    "id, from_id, to_id, subject, content, gold, items, is_read, claimed, "
    "CAST(EXTRACT(EPOCH FROM send_time) * 1000 AS BIGINT) AS send_time_ms, "
    "CAST(EXTRACT(EPOCH FROM expire_time) * 1000 AS BIGINT) AS expire_time_ms";

std::string SerializeItemsJson(const std::vector<MailHandler::MailAttachmentRecord>& items) {
  nlohmann::json serialized = nlohmann::json::array();
  for (const auto& item : items) {
    if (item.item_id == 0 || item.count == 0) {
      continue;
    }
    serialized.push_back({{"item_id", item.item_id}, {"count", item.count}});
  }
  return serialized.dump();
}

std::vector<MailHandler::MailAttachmentRecord> ParseItemsJson(const std::string& json_text) {
  std::vector<MailHandler::MailAttachmentRecord> items;
  if (json_text.empty()) {
    return items;
  }

  const nlohmann::json parsed = nlohmann::json::parse(json_text, nullptr, false);
  if (!parsed.is_array()) {
    return items;
  }

  for (const auto& node : parsed) {
    if (!node.is_object()) {
      continue;
    }
    const uint32_t item_id = node.value("item_id", 0U);
    const uint32_t count = node.value("count", 0U);
    if (item_id == 0 || count == 0) {
      continue;
    }
    MailHandler::MailAttachmentRecord record;
    record.item_id = item_id;
    record.count = count;
    items.push_back(record);
  }
  return items;
}

std::optional<MailHandler::MailRecord> ParseMailRow(const pqxx::row& row) {
  MailHandler::MailRecord mail;
  mail.mail_id = row["id"].as<uint64_t>(0);
  if (mail.mail_id == 0) {
    return std::nullopt;
  }

  mail.from_character_id = row["from_id"].as<uint64_t>(0);
  mail.to_character_id = row["to_id"].as<uint64_t>(0);
  mail.subject = row["subject"].as<std::string>("");
  mail.content = row["content"].as<std::string>("");
  mail.gold = row["gold"].as<uint32_t>(0);
  mail.items = ParseItemsJson(row["items"].as<std::string>("[]"));
  mail.is_read = row["is_read"].as<bool>(false);
  mail.claimed = row["claimed"].as<bool>(false);
  mail.send_time = row["send_time_ms"].as<uint64_t>(0);
  mail.expire_time = row["expire_time_ms"].as<uint64_t>(0);
  return mail;
}

flatbuffers::Offset<mir2::proto::MailSummary> BuildSummary(
    flatbuffers::FlatBufferBuilder& builder,
    const MailHandler::MailRecord& mail) {
  const auto subject = builder.CreateString(mail.subject);
  return mir2::proto::CreateMailSummary(
      builder,
      mail.mail_id,
      mail.from_character_id,
      subject,
      mail.gold > 0 || !mail.items.empty(),
      mail.is_read,
      mail.claimed,
      mail.send_time,
      mail.expire_time,
      mail.gold,
      static_cast<uint32_t>(mail.items.size()));
}

flatbuffers::Offset<mir2::proto::MailDetail> BuildDetail(
    flatbuffers::FlatBufferBuilder& builder,
    const MailHandler::MailRecord& mail) {
  std::vector<flatbuffers::Offset<mir2::proto::MailAttachmentItem>> item_offsets;
  item_offsets.reserve(mail.items.size());
  for (const auto& item : mail.items) {
    item_offsets.emplace_back(
        mir2::proto::CreateMailAttachmentItem(builder, item.item_id, item.count));
  }
  const auto items_vec = builder.CreateVector(item_offsets);
  const auto subject = builder.CreateString(mail.subject);
  const auto content = builder.CreateString(mail.content);
  return mir2::proto::CreateMailDetail(builder,
                                       mail.mail_id,
                                       mail.from_character_id,
                                       subject,
                                       content,
                                       mail.is_read,
                                       mail.claimed,
                                       mail.send_time,
                                       mail.expire_time,
                                       mail.gold,
                                       items_vec);
}

std::vector<uint8_t> BuildMailSendRspPayload(bool success,
                                             mir2::common::ErrorCode code,
                                             uint64_t mail_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateMailSendRsp(
      builder, success, static_cast<int>(ToProtoError(code)), mail_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildMailListRspPayload(
    bool success,
    mir2::common::ErrorCode code,
    const std::vector<MailHandler::MailRecord>& mails) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::MailSummary>> summaries;
  summaries.reserve(mails.size());
  for (const auto& mail : mails) {
    summaries.push_back(BuildSummary(builder, mail));
  }
  const auto summaries_vec = builder.CreateVector(summaries);
  const auto rsp = mir2::proto::CreateMailListRsp(
      builder, success, static_cast<int>(ToProtoError(code)), summaries_vec);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildMailReadRspPayload(
    bool success,
    mir2::common::ErrorCode code,
    const std::optional<MailHandler::MailRecord>& mail) {
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<mir2::proto::MailDetail> mail_offset = 0;
  if (mail.has_value()) {
    mail_offset = BuildDetail(builder, *mail);
  }
  const auto rsp = mir2::proto::CreateMailReadRsp(
      builder, success, static_cast<int>(ToProtoError(code)), mail_offset);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildMailDeleteRspPayload(bool success,
                                               mir2::common::ErrorCode code,
                                               uint64_t mail_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateMailDeleteRsp(
      builder, success, static_cast<int>(ToProtoError(code)), mail_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildMailClaimRspPayload(bool success,
                                              mir2::common::ErrorCode code,
                                              uint64_t mail_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateMailClaimRsp(
      builder, success, static_cast<int>(ToProtoError(code)), mail_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildMailNotifyPayload(const MailHandler::MailRecord& mail,
                                            uint32_t unread_count) {
  flatbuffers::FlatBufferBuilder builder;
  const auto summary = BuildSummary(builder, mail);
  const auto notify = mir2::proto::CreateMailNotify(builder, summary, unread_count);
  builder.Finish(notify);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

}  // namespace

MailHandler::MailHandler(ResponseSender& response_sender,
                         ClientRegistry& client_registry,
                         entt::registry& ecs_registry,
                         RoleStore* role_store,
                         std::shared_ptr<mir2::db::PgConnectionPool> db_pool)
    : response_sender_(response_sender),
      client_registry_(client_registry),
      ecs_registry_(ecs_registry),
      role_store_(role_store),
      db_pool_(std::move(db_pool)) {
  BootstrapPersistence();
}

Task<void> MailHandler::HandleMessage(HandlerContext ctx,
                                      const uint8_t* payload,
                                      size_t payload_size) {
  try {
    if (!payload || payload_size == 0) {
      co_await SendMailListRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {});
      co_return;
    }

    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kMailSendReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::MailSendReq>(nullptr)) {
          co_await SendMailSendRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::MailSendReq>(payload);
        co_await HandleSend(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kMailListReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::MailListReq>(nullptr)) {
          co_await SendMailListRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {});
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::MailListReq>(payload);
        co_await HandleList(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kMailReadReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::MailReadReq>(nullptr)) {
          co_await SendMailReadRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, std::nullopt);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::MailReadReq>(payload);
        co_await HandleRead(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kMailDeleteReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::MailDeleteReq>(nullptr)) {
          co_await SendMailDeleteRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::MailDeleteReq>(payload);
        co_await HandleDelete(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kMailClaimReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::MailClaimReq>(nullptr)) {
          co_await SendMailClaimRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::MailClaimReq>(payload);
        co_await HandleClaim(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("MailHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("MailHandler exception msg_id={} client_id={} error={}",
                 ctx.msg_id,
                 ctx.client_id,
                 ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("MailHandler exception msg_id={} client_id={} error=unknown",
                 ctx.msg_id,
                 ctx.client_id);
    co_return;
  }
}

Task<void> MailHandler::HandleSend(HandlerContext ctx,
                                   const mir2::proto::MailSendReq* req) {
  if (!req || req->target_character_id() == 0) {
    co_await SendMailSendRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const auto sender_character_id = ResolveCharacterId(ctx);
  if (!sender_character_id.has_value()) {
    co_await SendMailSendRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, 0);
    co_return;
  }

  const std::string subject = req->subject() ? req->subject()->str() : std::string{};
  const std::string content = req->content() ? req->content()->str() : std::string{};
  if (subject.empty() || content.empty()) {
    co_await SendMailSendRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  MailRecord mail;
  mail.mail_id = next_mail_id_.fetch_add(1, std::memory_order_relaxed);
  mail.from_character_id = *sender_character_id;
  mail.to_character_id = req->target_character_id();
  mail.subject = subject;
  mail.content = content;
  mail.gold = req->gold();
  mail.send_time = NowMs();
  mail.expire_time = req->expire_time() > mail.send_time
                         ? req->expire_time()
                         : DefaultExpireTime(mail.send_time);

  if (const auto* items = req->items()) {
    mail.items.reserve(items->size());
    for (const auto* item : *items) {
      if (!item) {
        continue;
      }
      MailAttachmentRecord record;
      record.item_id = item->item_id();
      record.count = item->count();
      mail.items.push_back(record);
    }
  }

  if (PersistenceEnabled()) {
    uint32_t unread_count = 0;
    bool db_failed = false;
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        throw std::runtime_error("mail persistence acquire failed");
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      txn.exec("DELETE FROM mails WHERE to_id = $1 AND expire_time <= NOW()",
               pqxx::params{mail.to_character_id});

      const std::string items_json = SerializeItemsJson(mail.items);
      const double send_time_sec = static_cast<double>(mail.send_time) / 1000.0;
      const double expire_time_sec = static_cast<double>(mail.expire_time) / 1000.0;
      const pqxx::result inserted_rows = txn.exec(
          "INSERT INTO mails "
          "(from_id, to_id, subject, content, gold, items, is_read, claimed, send_time, "
          "expire_time) "
          "VALUES ($1, $2, $3, $4, $5, $6::jsonb, FALSE, FALSE, TO_TIMESTAMP($7), "
          "TO_TIMESTAMP($8)) "
          "RETURNING id",
          pqxx::params{mail.from_character_id,
                       mail.to_character_id,
                       mail.subject,
                       mail.content,
                       mail.gold,
                       items_json,
                       send_time_sec,
                       expire_time_sec});
      if (inserted_rows.empty()) {
        throw std::runtime_error("mail persistence insert returned no row");
      }
      mail.mail_id = inserted_rows.front()["id"].as<uint64_t>(0);

      const pqxx::result unread_rows = txn.exec(
          "SELECT COUNT(*) AS unread_count "
          "FROM mails WHERE to_id = $1 AND is_read = FALSE AND expire_time > NOW()",
          pqxx::params{mail.to_character_id});
      if (!unread_rows.empty()) {
        unread_count = unread_rows.front()["unread_count"].as<uint32_t>(0);
      }
      txn.commit();
    } catch (const std::exception& ex) {
      SYSLOG_ERROR("MailHandler persistent send failed sender={} receiver={} error={}",
                   *sender_character_id,
                   mail.to_character_id,
                   ex.what());
      db_failed = true;
    }

    if (db_failed) {
      co_await SendMailSendRsp(ctx.client_id, false, mir2::common::ErrorCode::kUnknown, 0);
      co_return;
    }

    co_await SendMailSendRsp(
        ctx.client_id, true, mir2::common::ErrorCode::kOk, mail.mail_id);

    const uint64_t recipient_client_id = ResolveClientIdByCharacterId(mail.to_character_id);
    if (recipient_client_id != 0 && recipient_client_id != ctx.client_id &&
        client_registry_.Contains(recipient_client_id)) {
      co_await SendMailNotify(recipient_client_id, mail, unread_count);
    }
    co_return;
  }

  uint32_t unread_count = 0;
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    auto& recipient_box = mailbox_by_character_[mail.to_character_id];
    PurgeExpiredMails(&recipient_box, mail.send_time);
    recipient_box.push_back(mail);
    unread_count = CountUnread(recipient_box);
  }

  co_await SendMailSendRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, mail.mail_id);

  const uint64_t recipient_client_id = ResolveClientIdByCharacterId(mail.to_character_id);
  if (recipient_client_id != 0 && recipient_client_id != ctx.client_id &&
      client_registry_.Contains(recipient_client_id)) {
    co_await SendMailNotify(recipient_client_id, mail, unread_count);
  }
}

Task<void> MailHandler::HandleList(HandlerContext ctx,
                                   const mir2::proto::MailListReq* /*req*/) {
  const auto character_id = ResolveCharacterId(ctx);
  if (!character_id.has_value()) {
    co_await SendMailListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, {});
    co_return;
  }

  if (PersistenceEnabled()) {
    std::vector<MailRecord> mails;
    bool db_failed = false;
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        throw std::runtime_error("mail persistence acquire failed");
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);

      txn.exec("DELETE FROM mails WHERE to_id = $1 AND expire_time <= NOW()",
               pqxx::params{*character_id});
      const pqxx::result rows = txn.exec(
          std::string("SELECT ") + kMailSelectColumns +
              " FROM mails WHERE to_id = $1 AND expire_time > NOW() "
              "ORDER BY send_time DESC, id DESC",
          pqxx::params{*character_id});
      mails.reserve(rows.size());
      for (const auto& row : rows) {
        const auto parsed = ParseMailRow(row);
        if (!parsed.has_value()) {
          continue;
        }
        mails.push_back(*parsed);
      }
      txn.commit();
    } catch (const std::exception& ex) {
      SYSLOG_ERROR("MailHandler persistent list failed character_id={} error={}",
                   *character_id,
                   ex.what());
      db_failed = true;
    }

    if (db_failed) {
      co_await SendMailListRsp(ctx.client_id, false, mir2::common::ErrorCode::kUnknown, {});
      co_return;
    }

    co_await SendMailListRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk, mails);
    co_return;
  }

  const uint64_t now_ms = NowMs();
  std::vector<MailRecord> mails;
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    auto& box = mailbox_by_character_[*character_id];
    PurgeExpiredMails(&box, now_ms);
    mails = box;
  }
  std::sort(mails.begin(), mails.end(), [](const MailRecord& lhs, const MailRecord& rhs) {
    return lhs.send_time > rhs.send_time;
  });

  co_await SendMailListRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk, mails);
}

Task<void> MailHandler::HandleRead(HandlerContext ctx,
                                   const mir2::proto::MailReadReq* req) {
  if (!req || req->mail_id() == 0) {
    co_await SendMailReadRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, std::nullopt);
    co_return;
  }

  const auto character_id = ResolveCharacterId(ctx);
  if (!character_id.has_value()) {
    co_await SendMailReadRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, std::nullopt);
    co_return;
  }

  if (PersistenceEnabled()) {
    std::optional<MailRecord> parsed_mail;
    mir2::common::ErrorCode read_code = mir2::common::ErrorCode::kOk;
    bool db_failed = false;
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        throw std::runtime_error("mail persistence acquire failed");
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      const pqxx::result rows = txn.exec(
          std::string("UPDATE mails SET is_read = TRUE WHERE id = $1 AND to_id = $2 "
                      "AND expire_time > NOW() RETURNING ") +
              kMailSelectColumns,
          pqxx::params{req->mail_id(), *character_id});
      txn.commit();

      if (rows.empty()) {
        read_code = mir2::common::ErrorCode::kMailNotFound;
      } else {
        const auto parsed = ParseMailRow(rows.front());
        if (!parsed.has_value()) {
          read_code = mir2::common::ErrorCode::kUnknown;
        } else {
          parsed_mail = *parsed;
        }
      }
    } catch (const std::exception& ex) {
      SYSLOG_ERROR("MailHandler persistent read failed character_id={} mail_id={} error={}",
                   *character_id,
                   req->mail_id(),
                   ex.what());
      db_failed = true;
    }

    if (db_failed) {
      co_await SendMailReadRsp(ctx.client_id, false, mir2::common::ErrorCode::kUnknown, std::nullopt);
      co_return;
    }

    if (read_code != mir2::common::ErrorCode::kOk || !parsed_mail.has_value()) {
      co_await SendMailReadRsp(ctx.client_id, false, read_code, std::nullopt);
      co_return;
    }

    co_await SendMailReadRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk, parsed_mail);
    co_return;
  }

  const uint64_t now_ms = NowMs();
  std::optional<MailRecord> found;
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    auto& box = mailbox_by_character_[*character_id];
    PurgeExpiredMails(&box, now_ms);
    auto it = std::find_if(box.begin(), box.end(),
                           [mail_id = req->mail_id()](const MailRecord& mail) {
                             return mail.mail_id == mail_id;
                           });
    if (it != box.end()) {
      it->is_read = true;
      found = *it;
    }
  }

  if (!found.has_value()) {
    co_await SendMailReadRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kMailNotFound, std::nullopt);
    co_return;
  }

  co_await SendMailReadRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk, found);
}

Task<void> MailHandler::HandleDelete(HandlerContext ctx,
                                     const mir2::proto::MailDeleteReq* req) {
  if (!req || req->mail_id() == 0) {
    co_await SendMailDeleteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const auto character_id = ResolveCharacterId(ctx);
  if (!character_id.has_value()) {
    co_await SendMailDeleteRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, req->mail_id());
    co_return;
  }

  if (PersistenceEnabled()) {
    mir2::common::ErrorCode delete_code = mir2::common::ErrorCode::kMailNotFound;
    bool db_failed = false;
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        throw std::runtime_error("mail persistence acquire failed");
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      const pqxx::result rows = txn.exec(
          "SELECT claimed, gold, jsonb_array_length(items) AS item_count "
          "FROM mails WHERE id = $1 AND to_id = $2 AND expire_time > NOW() FOR UPDATE",
          pqxx::params{req->mail_id(), *character_id});
      if (rows.empty()) {
        delete_code = mir2::common::ErrorCode::kMailNotFound;
      } else {
        const pqxx::row row = rows.front();
        const bool claimed = row["claimed"].as<bool>(false);
        const uint32_t gold = row["gold"].as<uint32_t>(0);
        const uint32_t item_count = row["item_count"].as<uint32_t>(0);
        if (!claimed && (gold > 0 || item_count > 0)) {
          delete_code = mir2::common::ErrorCode::kMailAttachmentInvalid;
        } else {
          txn.exec("DELETE FROM mails WHERE id = $1 AND to_id = $2",
                   pqxx::params{req->mail_id(), *character_id});
          delete_code = mir2::common::ErrorCode::kOk;
        }
      }
      txn.commit();
    } catch (const std::exception& ex) {
      SYSLOG_ERROR("MailHandler persistent delete failed character_id={} mail_id={} error={}",
                   *character_id,
                   req->mail_id(),
                   ex.what());
      db_failed = true;
    }

    if (db_failed) {
      co_await SendMailDeleteRsp(ctx.client_id,
                                 false,
                                 mir2::common::ErrorCode::kUnknown,
                                 req->mail_id());
      co_return;
    }

    if (delete_code != mir2::common::ErrorCode::kOk) {
      co_await SendMailDeleteRsp(ctx.client_id, false, delete_code, req->mail_id());
      co_return;
    }

    co_await SendMailDeleteRsp(
        ctx.client_id, true, mir2::common::ErrorCode::kOk, req->mail_id());
    co_return;
  }

  const uint64_t now_ms = NowMs();
  mir2::common::ErrorCode delete_code = mir2::common::ErrorCode::kMailNotFound;
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    auto& box = mailbox_by_character_[*character_id];
    PurgeExpiredMails(&box, now_ms);
    auto it = std::find_if(box.begin(), box.end(),
                           [mail_id = req->mail_id()](const MailRecord& mail) {
                             return mail.mail_id == mail_id;
                           });
    if (it == box.end()) {
      delete_code = mir2::common::ErrorCode::kMailNotFound;
    } else if (!it->claimed && (it->gold > 0 || !it->items.empty())) {
      delete_code = mir2::common::ErrorCode::kMailAttachmentInvalid;
    } else {
      box.erase(it);
      delete_code = mir2::common::ErrorCode::kOk;
    }
  }

  if (delete_code != mir2::common::ErrorCode::kOk) {
    co_await SendMailDeleteRsp(ctx.client_id, false, delete_code, req->mail_id());
    co_return;
  }
  co_await SendMailDeleteRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, req->mail_id());
}

Task<void> MailHandler::HandleClaim(HandlerContext ctx,
                                    const mir2::proto::MailClaimReq* req) {
  if (!req || req->mail_id() == 0) {
    co_await SendMailClaimRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const auto character_id = ResolveCharacterId(ctx);
  if (!character_id.has_value()) {
    co_await SendMailClaimRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, req->mail_id());
    co_return;
  }

  uint32_t reward_gold = 0;
  if (PersistenceEnabled()) {
    mir2::common::ErrorCode claim_code = mir2::common::ErrorCode::kMailNotFound;
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        throw std::runtime_error("mail persistence acquire failed");
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      const pqxx::result rows = txn.exec(
          "SELECT claimed, gold FROM mails "
          "WHERE id = $1 AND to_id = $2 AND expire_time > NOW() FOR UPDATE",
          pqxx::params{req->mail_id(), *character_id});

      if (rows.empty()) {
        claim_code = mir2::common::ErrorCode::kMailNotFound;
      } else {
        const pqxx::row row = rows.front();
        if (row["claimed"].as<bool>(false)) {
          claim_code = mir2::common::ErrorCode::kMailAlreadyClaimed;
        } else {
          reward_gold = row["gold"].as<uint32_t>(0);
          txn.exec("UPDATE mails "
                   "SET is_read = TRUE, claimed = TRUE, gold = 0, items = '[]'::jsonb "
                   "WHERE id = $1 AND to_id = $2",
                   pqxx::params{req->mail_id(), *character_id});
          claim_code = mir2::common::ErrorCode::kOk;
        }
      }
      txn.commit();
    } catch (const std::exception& ex) {
      SYSLOG_ERROR("MailHandler persistent claim failed character_id={} mail_id={} error={}",
                   *character_id,
                   req->mail_id(),
                   ex.what());
      claim_code = mir2::common::ErrorCode::kUnknown;
    }

    if (claim_code != mir2::common::ErrorCode::kOk) {
      co_await SendMailClaimRsp(ctx.client_id, false, claim_code, req->mail_id());
      co_return;
    }
  } else {
    const uint64_t now_ms = NowMs();
    mir2::common::ErrorCode claim_code = mir2::common::ErrorCode::kMailNotFound;
    {
      std::lock_guard<std::mutex> lock(mailbox_mutex_);
      auto& box = mailbox_by_character_[*character_id];
      PurgeExpiredMails(&box, now_ms);
      auto it = std::find_if(box.begin(), box.end(),
                             [mail_id = req->mail_id()](const MailRecord& mail) {
                               return mail.mail_id == mail_id;
                             });
      if (it == box.end()) {
        claim_code = mir2::common::ErrorCode::kMailNotFound;
      } else if (it->claimed) {
        claim_code = mir2::common::ErrorCode::kMailAlreadyClaimed;
      } else {
        it->is_read = true;
        it->claimed = true;
        reward_gold = it->gold;
        it->gold = 0;
        it->items.clear();
        claim_code = mir2::common::ErrorCode::kOk;
      }
    }

    if (claim_code != mir2::common::ErrorCode::kOk) {
      co_await SendMailClaimRsp(ctx.client_id, false, claim_code, req->mail_id());
      co_return;
    }
  }

  if (reward_gold > 0 && ctx.entity != entt::null && ecs_registry_.valid(ctx.entity)) {
    if (auto* attributes =
            ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(ctx.entity)) {
      attributes->gold += static_cast<int>(reward_gold);
    }
  }

  co_await SendMailClaimRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, req->mail_id());
}

Task<void> MailHandler::SendMailSendRsp(uint64_t client_id,
                                        bool success,
                                        mir2::common::ErrorCode code,
                                        uint64_t mail_id) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp),
      BuildMailSendRspPayload(success, code, mail_id));
}

Task<void> MailHandler::SendMailListRsp(uint64_t client_id,
                                        bool success,
                                        mir2::common::ErrorCode code,
                                        const std::vector<MailRecord>& mails) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMailListRsp),
      BuildMailListRspPayload(success, code, mails));
}

Task<void> MailHandler::SendMailReadRsp(uint64_t client_id,
                                        bool success,
                                        mir2::common::ErrorCode code,
                                        const std::optional<MailRecord>& mail) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMailReadRsp),
      BuildMailReadRspPayload(success, code, mail));
}

Task<void> MailHandler::SendMailDeleteRsp(uint64_t client_id,
                                          bool success,
                                          mir2::common::ErrorCode code,
                                          uint64_t mail_id) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMailDeleteRsp),
      BuildMailDeleteRspPayload(success, code, mail_id));
}

Task<void> MailHandler::SendMailClaimRsp(uint64_t client_id,
                                         bool success,
                                         mir2::common::ErrorCode code,
                                         uint64_t mail_id) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMailClaimRsp),
      BuildMailClaimRspPayload(success, code, mail_id));
}

Task<void> MailHandler::SendMailNotify(uint64_t client_id,
                                       const MailRecord& mail,
                                       uint32_t unread_count) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMailNotify),
      BuildMailNotifyPayload(mail, unread_count));
}

std::optional<uint64_t> MailHandler::ResolveCharacterId(HandlerContext ctx) const {
  if (ctx.entity != entt::null && ecs_registry_.valid(ctx.entity)) {
    if (const auto* identity =
            ecs_registry_.try_get<mir2::ecs::CharacterIdentityComponent>(ctx.entity)) {
      if (identity->id != mir2::ecs::kInvalidCharacterId) {
        return identity->id;
      }
    }
  }
  if (role_store_) {
    return role_store_->GetRoleId(ctx.client_id);
  }
  return std::nullopt;
}

uint64_t MailHandler::ResolveClientIdByCharacterId(uint64_t character_id) const {
  if (!role_store_ || character_id == 0) {
    return 0;
  }
  return role_store_->GetClientIdByRoleId(character_id).value_or(0);
}

uint64_t MailHandler::DefaultExpireTime(uint64_t now_ms) const {
  return now_ms + kDefaultMailExpireMs;
}

void MailHandler::PurgeExpiredMails(std::vector<MailRecord>* mails, uint64_t now_ms) const {
  if (!mails) {
    return;
  }
  mails->erase(std::remove_if(mails->begin(),
                              mails->end(),
                              [now_ms](const MailRecord& mail) {
                                return mail.expire_time != 0 && mail.expire_time <= now_ms;
                              }),
               mails->end());
}

uint32_t MailHandler::CountUnread(const std::vector<MailRecord>& mails) const {
  uint32_t unread = 0;
  for (const auto& mail : mails) {
    if (!mail.is_read) {
      ++unread;
    }
  }
  return unread;
}

bool MailHandler::PersistenceEnabled() const {
  return db_pool_ != nullptr && db_pool_->IsReady() && db_pool_->PoolSize() > 0;
}

void MailHandler::BootstrapPersistence() {
  if (!PersistenceEnabled() || persistence_bootstrapped_) {
    return;
  }

  try {
    const auto conn = db_pool_->Acquire();
    if (!conn) {
      return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);
    txn.exec(
        "CREATE TABLE IF NOT EXISTS mails ("
        "id BIGSERIAL PRIMARY KEY, "
        "from_id BIGINT NOT NULL, "
        "to_id BIGINT NOT NULL, "
        "subject VARCHAR(128) NOT NULL, "
        "content TEXT NOT NULL, "
        "gold INTEGER NOT NULL DEFAULT 0, "
        "items JSONB NOT NULL DEFAULT '[]'::jsonb, "
        "is_read BOOLEAN NOT NULL DEFAULT FALSE, "
        "claimed BOOLEAN NOT NULL DEFAULT FALSE, "
        "send_time TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
        "expire_time TIMESTAMPTZ NOT NULL)");
    txn.exec("CREATE INDEX IF NOT EXISTS idx_mails_to_id ON mails (to_id)");
    txn.exec("CREATE INDEX IF NOT EXISTS idx_mails_expire_time ON mails (expire_time)");
    txn.commit();
    persistence_bootstrapped_ = true;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("MailHandler persistence bootstrap failed: {}", ex.what());
  }
}

uint64_t MailHandler::NowMs() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace mir2::logic
