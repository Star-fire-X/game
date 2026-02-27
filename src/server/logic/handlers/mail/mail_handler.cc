#include "logic/handlers/mail/mail_handler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include "common/enums.h"
#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
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
constexpr int kMaxInventorySlots = mir2::common::constants::MAX_INVENTORY_SIZE;
const char* kMailSelectColumns =
    "id, from_id, to_id, subject, content, gold, items, is_read, claimed, "
    "CAST(EXTRACT(EPOCH FROM send_time) * 1000 AS BIGINT) AS send_time_ms, "
    "CAST(EXTRACT(EPOCH FROM expire_time) * 1000 AS BIGINT) AS expire_time_ms";

struct InventoryDelta {
  entt::entity item_entity = entt::null;
  int old_count = 0;
  int old_slot = -1;
};

struct DeductedAssets {
  uint32_t gold = 0;
  std::vector<InventoryDelta> inventory_deltas;
  std::vector<entt::entity> emptied_items;
};

struct GrantedAssets {
  uint32_t gold = 0;
  std::vector<entt::entity> created_items;
};

void RollbackDeductedAssets(entt::registry& registry,
                            entt::entity sender,
                            const DeductedAssets& deducted);

void MarkDirty(entt::registry& registry,
               entt::entity character,
               bool items_dirty,
               bool attributes_dirty) {
  if (character == entt::null || !registry.valid(character) ||
      (!items_dirty && !attributes_dirty)) {
    return;
  }

  auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(character);
  if (!dirty) {
    dirty = &registry.emplace<mir2::ecs::DirtyComponent>(character);
  }
  if (items_dirty) {
    dirty->items_dirty = true;
  }
  if (attributes_dirty) {
    dirty->attributes_dirty = true;
  }
}

std::optional<entt::entity> ResolveEntityByCharacterId(entt::registry& registry,
                                                       entt::entity hint,
                                                       uint64_t character_id) {
  if (character_id == 0) {
    return std::nullopt;
  }

  if (hint != entt::null && registry.valid(hint)) {
    const auto* identity = registry.try_get<mir2::ecs::CharacterIdentityComponent>(hint);
    if (identity && identity->id == character_id) {
      return hint;
    }
  }

  auto view = registry.view<mir2::ecs::CharacterIdentityComponent>();
  for (const entt::entity entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (identity.id == character_id) {
      return entity;
    }
  }

  return std::nullopt;
}

std::vector<entt::entity> CollectInventoryStacks(entt::registry& registry,
                                                 entt::entity owner,
                                                 uint32_t item_id) {
  std::vector<entt::entity> stacks;
  if (owner == entt::null || !registry.valid(owner) || item_id == 0) {
    return stacks;
  }

  auto view = registry.view<mir2::ecs::ItemComponent, mir2::ecs::InventoryOwnerComponent>();
  for (const entt::entity entity : view) {
    const auto& item = view.get<mir2::ecs::ItemComponent>(entity);
    const auto& owner_comp = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner_comp.owner != owner || owner_comp.slot_index < 0 || item.item_id != item_id ||
        item.count <= 0) {
      continue;
    }
    stacks.push_back(entity);
  }

  std::sort(stacks.begin(), stacks.end(), [&registry](entt::entity lhs, entt::entity rhs) {
    const auto* lhs_owner = registry.try_get<mir2::ecs::InventoryOwnerComponent>(lhs);
    const auto* rhs_owner = registry.try_get<mir2::ecs::InventoryOwnerComponent>(rhs);
    const int lhs_slot = lhs_owner ? lhs_owner->slot_index : std::numeric_limits<int>::max();
    const int rhs_slot = rhs_owner ? rhs_owner->slot_index : std::numeric_limits<int>::max();
    if (lhs_slot != rhs_slot) {
      return lhs_slot < rhs_slot;
    }
    return entt::to_integral(lhs) < entt::to_integral(rhs);
  });
  return stacks;
}

std::array<bool, kMaxInventorySlots> CollectUsedInventorySlots(entt::registry& registry,
                                                               entt::entity owner) {
  std::array<bool, kMaxInventorySlots> used_slots{};
  if (owner == entt::null || !registry.valid(owner)) {
    return used_slots;
  }

  auto view = registry.view<mir2::ecs::InventoryOwnerComponent>();
  for (const entt::entity entity : view) {
    const auto& owner_comp = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner_comp.owner != owner || owner_comp.slot_index < 0 ||
        owner_comp.slot_index >= kMaxInventorySlots) {
      continue;
    }
    used_slots[static_cast<size_t>(owner_comp.slot_index)] = true;
  }
  return used_slots;
}

std::vector<int> CollectFreeInventorySlots(entt::registry& registry,
                                           entt::entity owner,
                                           size_t needed) {
  std::vector<int> free_slots;
  if (needed == 0) {
    return free_slots;
  }

  auto used_slots = CollectUsedInventorySlots(registry, owner);
  for (int slot = 0; slot < kMaxInventorySlots; ++slot) {
    if (used_slots[static_cast<size_t>(slot)]) {
      continue;
    }
    free_slots.push_back(slot);
    if (free_slots.size() >= needed) {
      break;
    }
  }
  return free_slots;
}

bool DeductAssetsFromSender(entt::registry& registry,
                            entt::entity sender,
                            uint32_t gold,
                            const std::vector<MailHandler::MailAttachmentRecord>& attachments,
                            DeductedAssets* deducted) {
  if (!deducted || sender == entt::null || !registry.valid(sender)) {
    return false;
  }

  auto* attributes = registry.try_get<mir2::ecs::CharacterAttributesComponent>(sender);
  if ((gold > 0 && attributes == nullptr) ||
      (gold > 0 &&
       static_cast<int64_t>(attributes->gold) < static_cast<int64_t>(gold))) {
    return false;
  }

  std::unordered_map<uint32_t, uint32_t> required_counts;
  required_counts.reserve(attachments.size());
  for (const auto& attachment : attachments) {
    if (attachment.item_id == 0 || attachment.count == 0 ||
        attachment.count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    auto& required = required_counts[attachment.item_id];
    if (required >
        std::numeric_limits<uint32_t>::max() - attachment.count) {
      return false;
    }
    required += attachment.count;
  }

  std::unordered_map<uint32_t, std::vector<entt::entity>> inventory_by_item;
  inventory_by_item.reserve(required_counts.size());
  for (const auto& [item_id, required] : required_counts) {
    auto stacks = CollectInventoryStacks(registry, sender, item_id);
    uint64_t available = 0;
    for (const entt::entity entity : stacks) {
      const auto* item = registry.try_get<mir2::ecs::ItemComponent>(entity);
      if (!item || item->count <= 0) {
        continue;
      }
      available += static_cast<uint32_t>(item->count);
    }
    if (available < required) {
      return false;
    }
    inventory_by_item.emplace(item_id, std::move(stacks));
  }

  if (gold > 0) {
    attributes->gold -= static_cast<int>(gold);
    deducted->gold = gold;
  }

  deducted->inventory_deltas.clear();
  deducted->emptied_items.clear();
  for (const auto& [item_id, required_total] : required_counts) {
    uint32_t remaining = required_total;
    const auto it = inventory_by_item.find(item_id);
    if (it == inventory_by_item.end()) {
      RollbackDeductedAssets(registry, sender, *deducted);
      return false;
    }

    for (const entt::entity entity : it->second) {
      if (remaining == 0) {
        break;
      }
      auto* item = registry.try_get<mir2::ecs::ItemComponent>(entity);
      auto* owner = registry.try_get<mir2::ecs::InventoryOwnerComponent>(entity);
      if (!item || !owner || owner->owner != sender || owner->slot_index < 0 || item->count <= 0) {
        RollbackDeductedAssets(registry, sender, *deducted);
        return false;
      }

      InventoryDelta delta;
      delta.item_entity = entity;
      delta.old_count = item->count;
      delta.old_slot = owner->slot_index;
      deducted->inventory_deltas.push_back(delta);

      const uint32_t available = static_cast<uint32_t>(item->count);
      const uint32_t consume = std::min<uint32_t>(available, remaining);
      item->count -= static_cast<int>(consume);
      remaining -= consume;
      if (item->count <= 0) {
        item->count = 0;
        owner->slot_index = -1;
        deducted->emptied_items.push_back(entity);
      }
    }

    if (remaining != 0) {
      RollbackDeductedAssets(registry, sender, *deducted);
      return false;
    }
  }

  MarkDirty(registry, sender, !deducted->inventory_deltas.empty(), deducted->gold > 0);
  return true;
}

void RollbackDeductedAssets(entt::registry& registry,
                            entt::entity sender,
                            const DeductedAssets& deducted) {
  if (sender == entt::null || !registry.valid(sender)) {
    return;
  }

  if (deducted.gold > 0) {
    if (auto* attributes = registry.try_get<mir2::ecs::CharacterAttributesComponent>(sender)) {
      attributes->gold += static_cast<int>(deducted.gold);
    }
  }

  for (auto it = deducted.inventory_deltas.rbegin(); it != deducted.inventory_deltas.rend(); ++it) {
    if (!registry.valid(it->item_entity)) {
      continue;
    }
    auto* item = registry.try_get<mir2::ecs::ItemComponent>(it->item_entity);
    auto* owner = registry.try_get<mir2::ecs::InventoryOwnerComponent>(it->item_entity);
    if (!item || !owner) {
      continue;
    }
    item->count = it->old_count;
    owner->slot_index = it->old_slot;
  }

  MarkDirty(registry, sender, !deducted.inventory_deltas.empty(), deducted.gold > 0);
}

void FinalizeDeductedAssets(entt::registry& registry, entt::entity sender, const DeductedAssets& deducted) {
  for (const entt::entity item_entity : deducted.emptied_items) {
    if (registry.valid(item_entity)) {
      registry.destroy(item_entity);
    }
  }
  if (!deducted.emptied_items.empty()) {
    MarkDirty(registry, sender, true, false);
  }
}

bool GrantAssetsToRecipient(entt::registry& registry,
                            entt::entity recipient,
                            uint32_t gold,
                            const std::vector<MailHandler::MailAttachmentRecord>& attachments,
                            GrantedAssets* granted) {
  if (!granted || recipient == entt::null || !registry.valid(recipient)) {
    return false;
  }

  size_t attachment_slots = 0;
  for (const auto& attachment : attachments) {
    if (attachment.item_id == 0 || attachment.count == 0 ||
        attachment.count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    ++attachment_slots;
  }

  if (gold > 0) {
    auto* attributes = registry.try_get<mir2::ecs::CharacterAttributesComponent>(recipient);
    if (!attributes) {
      return false;
    }
    if (static_cast<int64_t>(attributes->gold) + static_cast<int64_t>(gold) >
        static_cast<int64_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    attributes->gold += static_cast<int>(gold);
    granted->gold = gold;
  } else {
    granted->gold = 0;
  }

  granted->created_items.clear();
  const auto free_slots = CollectFreeInventorySlots(registry, recipient, attachment_slots);
  if (free_slots.size() < attachment_slots) {
    if (granted->gold > 0) {
      if (auto* attributes = registry.try_get<mir2::ecs::CharacterAttributesComponent>(recipient)) {
        attributes->gold -= static_cast<int>(granted->gold);
      }
      granted->gold = 0;
    }
    return false;
  }

  size_t slot_index = 0;
  for (const auto& attachment : attachments) {
    const entt::entity item_entity = registry.create();
    auto& item = registry.emplace<mir2::ecs::ItemComponent>(item_entity);
    item.item_id = attachment.item_id;
    item.count = static_cast<int>(attachment.count);
    item.instance_id = static_cast<uint64_t>(entt::to_integral(item_entity));

    auto& owner = registry.emplace<mir2::ecs::InventoryOwnerComponent>(item_entity);
    owner.owner = recipient;
    owner.slot_index = free_slots[slot_index++];
    granted->created_items.push_back(item_entity);
  }

  MarkDirty(registry, recipient, !granted->created_items.empty(), granted->gold > 0);
  return true;
}

void RollbackGrantedAssets(entt::registry& registry,
                           entt::entity recipient,
                           const GrantedAssets& granted) {
  if (recipient == entt::null || !registry.valid(recipient)) {
    return;
  }

  for (const entt::entity item_entity : granted.created_items) {
    if (registry.valid(item_entity)) {
      registry.destroy(item_entity);
    }
  }

  if (granted.gold > 0) {
    if (auto* attributes = registry.try_get<mir2::ecs::CharacterAttributesComponent>(recipient)) {
      attributes->gold -= static_cast<int>(granted.gold);
    }
  }

  MarkDirty(registry, recipient, !granted.created_items.empty(), granted.gold > 0);
}

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

  std::unordered_map<uint32_t, uint32_t> attachment_totals;
  if (const auto* items = req->items()) {
    attachment_totals.reserve(items->size());
    for (const auto* item : *items) {
      if (!item || item->item_id() == 0 || item->count() == 0) {
        co_await SendMailSendRsp(
            ctx.client_id, false, mir2::common::ErrorCode::kMailAttachmentInvalid, 0);
        co_return;
      }
      auto& total = attachment_totals[item->item_id()];
      if (total > std::numeric_limits<uint32_t>::max() - item->count()) {
        co_await SendMailSendRsp(
            ctx.client_id, false, mir2::common::ErrorCode::kMailAttachmentInvalid, 0);
        co_return;
      }
      total += item->count();
    }
  }
  mail.items.reserve(attachment_totals.size());
  for (const auto& [item_id, count] : attachment_totals) {
    MailAttachmentRecord record;
    record.item_id = item_id;
    record.count = count;
    mail.items.push_back(record);
  }
  std::sort(mail.items.begin(), mail.items.end(), [](const MailAttachmentRecord& lhs,
                                                     const MailAttachmentRecord& rhs) {
    return lhs.item_id < rhs.item_id;
  });

  const auto sender_entity = ResolveEntityByCharacterId(ecs_registry_, ctx.entity, *sender_character_id);
  if (!sender_entity.has_value()) {
    co_await SendMailSendRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, 0);
    co_return;
  }

  DeductedAssets deducted_assets;
  if (!DeductAssetsFromSender(
          ecs_registry_, *sender_entity, mail.gold, mail.items, &deducted_assets)) {
    co_await SendMailSendRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kMailAttachmentInvalid, 0);
    co_return;
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
      RollbackDeductedAssets(ecs_registry_, *sender_entity, deducted_assets);
      co_await SendMailSendRsp(ctx.client_id, false, mir2::common::ErrorCode::kUnknown, 0);
      co_return;
    }

    FinalizeDeductedAssets(ecs_registry_, *sender_entity, deducted_assets);
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
  bool enqueue_failed = false;
  try {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    auto& recipient_box = mailbox_by_character_[mail.to_character_id];
    PurgeExpiredMails(&recipient_box, mail.send_time);
    recipient_box.push_back(mail);
    unread_count = CountUnread(recipient_box);
  } catch (const std::exception&) {
    enqueue_failed = true;
  } catch (...) {
    enqueue_failed = true;
  }

  if (enqueue_failed) {
    RollbackDeductedAssets(ecs_registry_, *sender_entity, deducted_assets);
    co_await SendMailSendRsp(ctx.client_id, false, mir2::common::ErrorCode::kUnknown, 0);
    co_return;
  }

  FinalizeDeductedAssets(ecs_registry_, *sender_entity, deducted_assets);
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

  const auto character_entity = ResolveEntityByCharacterId(ecs_registry_, ctx.entity, *character_id);
  if (!character_entity.has_value()) {
    co_await SendMailClaimRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, req->mail_id());
    co_return;
  }

  if (PersistenceEnabled()) {
    mir2::common::ErrorCode claim_code = mir2::common::ErrorCode::kMailNotFound;
    GrantedAssets granted_assets;
    bool granted = false;
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        throw std::runtime_error("mail persistence acquire failed");
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      const pqxx::result rows = txn.exec(
          "SELECT claimed, gold, items FROM mails "
          "WHERE id = $1 AND to_id = $2 AND expire_time > NOW() FOR UPDATE",
          pqxx::params{req->mail_id(), *character_id});

      if (rows.empty()) {
        claim_code = mir2::common::ErrorCode::kMailNotFound;
      } else {
        const pqxx::row row = rows.front();
        if (row["claimed"].as<bool>(false)) {
          claim_code = mir2::common::ErrorCode::kMailAlreadyClaimed;
        } else {
          const uint32_t reward_gold = row["gold"].as<uint32_t>(0);
          const auto reward_items = ParseItemsJson(row["items"].as<std::string>("[]"));
          if (!GrantAssetsToRecipient(
                  ecs_registry_, *character_entity, reward_gold, reward_items, &granted_assets)) {
            claim_code = mir2::common::ErrorCode::kMailAttachmentInvalid;
          } else {
            granted = true;
            txn.exec("UPDATE mails "
                     "SET is_read = TRUE, claimed = TRUE, gold = 0, items = '[]'::jsonb "
                     "WHERE id = $1 AND to_id = $2",
                     pqxx::params{req->mail_id(), *character_id});
            claim_code = mir2::common::ErrorCode::kOk;
          }
        }
      }
      if (claim_code == mir2::common::ErrorCode::kOk) {
        txn.commit();
      }
    } catch (const std::exception& ex) {
      SYSLOG_ERROR("MailHandler persistent claim failed character_id={} mail_id={} error={}",
                   *character_id,
                   req->mail_id(),
                   ex.what());
      if (granted) {
        RollbackGrantedAssets(ecs_registry_, *character_entity, granted_assets);
        granted = false;
      }
      claim_code = mir2::common::ErrorCode::kUnknown;
    }

    if (claim_code != mir2::common::ErrorCode::kOk && granted) {
      RollbackGrantedAssets(ecs_registry_, *character_entity, granted_assets);
    }

    if (claim_code != mir2::common::ErrorCode::kOk) {
      co_await SendMailClaimRsp(ctx.client_id, false, claim_code, req->mail_id());
      co_return;
    }
  } else {
    const uint64_t now_ms = NowMs();
    mir2::common::ErrorCode claim_code = mir2::common::ErrorCode::kMailNotFound;
    GrantedAssets granted_assets;
    bool granted = false;
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
        if (!GrantAssetsToRecipient(
                ecs_registry_, *character_entity, it->gold, it->items, &granted_assets)) {
          claim_code = mir2::common::ErrorCode::kMailAttachmentInvalid;
        } else {
          granted = true;
          it->is_read = true;
          it->claimed = true;
          it->gold = 0;
          it->items.clear();
          claim_code = mir2::common::ErrorCode::kOk;
        }
      }
    }

    if (claim_code != mir2::common::ErrorCode::kOk && granted) {
      RollbackGrantedAssets(ecs_registry_, *character_entity, granted_assets);
    }

    if (claim_code != mir2::common::ErrorCode::kOk) {
      co_await SendMailClaimRsp(ctx.client_id, false, claim_code, req->mail_id());
      co_return;
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
