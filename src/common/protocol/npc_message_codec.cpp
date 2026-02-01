#include "common/protocol/npc_message_codec.h"

#include <limits>

#include <nlohmann/json.hpp>

namespace mir2::common {

namespace {
using json = nlohmann::json;

MessageCodecStatus SetStatus(MessageCodecStatus status, MessageCodecStatus* out_status) {
    if (out_status) {
        *out_status = status;
    }
    return status;
}

MessageCodecStatus ValidateMsgId(uint16_t msg_id, uint16_t expected) {
    return msg_id == expected ? MessageCodecStatus::kOk : MessageCodecStatus::kInvalidMsgId;
}

MessageCodecStatus ParseJsonObject(const uint8_t* data, size_t size, json* out) {
    if (!out || !data || size == 0) {
        return MessageCodecStatus::kInvalidPayload;
    }

    try {
        *out = json::parse(data, data + size);
    } catch (const json::exception&) {
        return MessageCodecStatus::kInvalidPayload;
    }

    if (!out->is_object()) {
        return MessageCodecStatus::kInvalidPayload;
    }

    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadUnsigned(const json& value, uint64_t* out) {
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        return MessageCodecStatus::kInvalidPayload;
    }

    if (value.is_number_unsigned()) {
        *out = value.get<uint64_t>();
        return MessageCodecStatus::kOk;
    }

    const auto signed_value = value.get<int64_t>();
    if (signed_value < 0) {
        return MessageCodecStatus::kValueOutOfRange;
    }

    *out = static_cast<uint64_t>(signed_value);
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadUInt64(const json& j, const char* key, uint64_t* out) {
    auto it = j.find(key);
    if (it == j.end()) {
        return MessageCodecStatus::kMissingField;
    }
    return ReadUnsigned(*it, out);
}

MessageCodecStatus ReadUInt32(const json& j, const char* key, uint32_t* out) {
    uint64_t value = 0;
    auto status = ReadUInt64(j, key, &value);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (value > std::numeric_limits<uint32_t>::max()) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    *out = static_cast<uint32_t>(value);
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadUInt8(const json& j, const char* key, uint8_t* out) {
    uint64_t value = 0;
    auto status = ReadUInt64(j, key, &value);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (value > std::numeric_limits<uint8_t>::max()) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    *out = static_cast<uint8_t>(value);
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadString(const json& j, const char* key, std::string* out) {
    auto it = j.find(key);
    if (it == j.end()) {
        return MessageCodecStatus::kMissingField;
    }
    if (!it->is_string()) {
        return MessageCodecStatus::kInvalidPayload;
    }
    *out = it->get<std::string>();
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadVersion(const json& j, uint32_t* out_version) {
    if (!out_version) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto it = j.find("version");
    if (it == j.end()) {
        *out_version = 0;
        return MessageCodecStatus::kOk;
    }

    uint64_t value = 0;
    auto status = ReadUnsigned(*it, &value);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (value > std::numeric_limits<uint32_t>::max()) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    *out_version = static_cast<uint32_t>(value);
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateVersion(uint32_t version) {
    if (version > kNpcCodecVersion) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadStringArray(const json& j,
                                   const char* key,
                                   size_t max_items,
                                   size_t max_length,
                                   std::vector<std::string>* out) {
    if (!out) {
        return MessageCodecStatus::kInvalidPayload;
    }

    auto it = j.find(key);
    if (it == j.end()) {
        out->clear();
        return MessageCodecStatus::kOk;
    }
    if (!it->is_array()) {
        return MessageCodecStatus::kInvalidPayload;
    }

    if (it->size() > max_items) {
        return MessageCodecStatus::kValueOutOfRange;
    }

    out->clear();
    out->reserve(it->size());
    for (const auto& entry : *it) {
        if (!entry.is_string()) {
            return MessageCodecStatus::kInvalidPayload;
        }
        auto value = entry.get<std::string>();
        if (value.empty()) {
            return MessageCodecStatus::kMissingField;
        }
        if (value.size() > max_length) {
            return MessageCodecStatus::kStringTooLong;
        }
        out->push_back(std::move(value));
    }

    return MessageCodecStatus::kOk;
}

MessageCodecStatus ReadShopItems(const json& j,
                                 const char* key,
                                 std::vector<NpcShopItem>* out) {
    if (!out) {
        return MessageCodecStatus::kInvalidPayload;
    }

    auto it = j.find(key);
    if (it == j.end()) {
        out->clear();
        return MessageCodecStatus::kOk;
    }
    if (!it->is_array()) {
        return MessageCodecStatus::kInvalidPayload;
    }
    if (it->size() > kMaxNpcShopItems) {
        return MessageCodecStatus::kValueOutOfRange;
    }

    out->clear();
    out->reserve(it->size());
    for (const auto& entry : *it) {
        if (!entry.is_object()) {
            return MessageCodecStatus::kInvalidPayload;
        }

        NpcShopItem item;
        auto status = ReadUInt32(entry, "item_id", &item.item_id);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
        status = ReadUInt32(entry, "price", &item.price);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
        if (entry.contains("stock")) {
            uint64_t stock_value = 0;
            status = ReadUnsigned(entry.at("stock"), &stock_value);
            if (status != MessageCodecStatus::kOk) {
                return status;
            }
            if (stock_value > std::numeric_limits<uint16_t>::max()) {
                return MessageCodecStatus::kValueOutOfRange;
            }
            item.stock = static_cast<uint16_t>(stock_value);
        }
        out->push_back(std::move(item));
    }

    return MessageCodecStatus::kOk;
}

std::vector<uint8_t> BuildJsonPayload(const json& j, MessageCodecStatus* out_status) {
    try {
        const auto dumped = j.dump();
        SetStatus(MessageCodecStatus::kOk, out_status);
        return std::vector<uint8_t>(dumped.begin(), dumped.end());
    } catch (const json::exception&) {
        SetStatus(MessageCodecStatus::kInvalidPayload, out_status);
        return {};
    }
}

}  // namespace

MessageCodecStatus ValidateNpcInteractReq(const NpcInteractReq& request) {
    if (request.npc_id == 0 || request.player_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateNpcDialogShowMsg(const NpcDialogShowMsg& msg) {
    if (msg.npc_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    if (msg.text.empty()) {
        return MessageCodecStatus::kMissingField;
    }
    if (msg.text.size() > kMaxNpcDialogTextLength) {
        return MessageCodecStatus::kStringTooLong;
    }
    if (msg.options.size() > kMaxNpcDialogOptions) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    for (const auto& option : msg.options) {
        if (option.empty()) {
            return MessageCodecStatus::kMissingField;
        }
        if (option.size() > kMaxNpcDialogOptionLength) {
            return MessageCodecStatus::kStringTooLong;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateNpcMenuSelectReq(const NpcMenuSelectReq& request) {
    if (request.npc_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateNpcShopOpenMsg(const NpcShopOpenMsg& msg) {
    if (msg.shop_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    if (msg.items.size() > kMaxNpcShopItems) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    for (const auto& item : msg.items) {
        if (item.item_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateNpcQuestMsg(const NpcQuestMsg& msg) {
    if (msg.quest_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    auto status_value = static_cast<uint32_t>(msg.status);
    if (status_value > static_cast<uint32_t>(NpcQuestStatus::kCompleted)) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    return MessageCodecStatus::kOk;
}

std::vector<uint8_t> EncodeNpcInteractReq(const NpcInteractReq& request,
                                          MessageCodecStatus* out_status) {
    auto status = ValidateNpcInteractReq(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    json j;
    j["version"] = kNpcCodecVersion;
    j["npc_id"] = request.npc_id;
    j["player_id"] = request.player_id;

    return BuildJsonPayload(j, out_status);
}

std::vector<uint8_t> EncodeNpcMenuSelect(const NpcMenuSelectReq& request,
                                         MessageCodecStatus* out_status) {
    auto status = ValidateNpcMenuSelectReq(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    json j;
    j["version"] = kNpcCodecVersion;
    j["npc_id"] = request.npc_id;
    j["option_index"] = request.option_index;

    return BuildJsonPayload(j, out_status);
}

MessageCodecStatus DecodeNpcDialogShow(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       NpcDialogShowMsg* out_msg) {
    if (!out_msg) {
        return MessageCodecStatus::kInvalidPayload;
    }

    auto status = ValidateMsgId(msg_id, kNpcDialogShowMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    json j;
    status = ParseJsonObject(data, size, &j);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    uint32_t version = 0;
    status = ReadVersion(j, &version);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ValidateVersion(version);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    status = ReadUInt64(j, "npc_id", &out_msg->npc_id);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ReadString(j, "text", &out_msg->text);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ReadStringArray(j, "options", kMaxNpcDialogOptions, kMaxNpcDialogOptionLength,
                             &out_msg->options);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    return ValidateNpcDialogShowMsg(*out_msg);
}

MessageCodecStatus DecodeNpcDialogShow(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       NpcDialogShowMsg* out_msg) {
    return DecodeNpcDialogShow(msg_id, payload.data(), payload.size(), out_msg);
}

MessageCodecStatus DecodeNpcShopOpen(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     NpcShopOpenMsg* out_msg) {
    if (!out_msg) {
        return MessageCodecStatus::kInvalidPayload;
    }

    auto status = ValidateMsgId(msg_id, kNpcShopOpenMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    json j;
    status = ParseJsonObject(data, size, &j);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    uint32_t version = 0;
    status = ReadVersion(j, &version);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ValidateVersion(version);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    if (j.contains("shop_id")) {
        status = ReadUInt32(j, "shop_id", &out_msg->shop_id);
    } else {
        status = ReadUInt32(j, "store_id", &out_msg->shop_id);
    }
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    if (j.contains("npc_id")) {
        uint64_t npc_id = 0;
        status = ReadUInt64(j, "npc_id", &npc_id);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
        out_msg->npc_id = npc_id;
    } else {
        out_msg->npc_id = 0;
    }

    status = ReadShopItems(j, "items", &out_msg->items);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    return ValidateNpcShopOpenMsg(*out_msg);
}

MessageCodecStatus DecodeNpcShopOpen(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     NpcShopOpenMsg* out_msg) {
    return DecodeNpcShopOpen(msg_id, payload.data(), payload.size(), out_msg);
}

MessageCodecStatus DecodeNpcQuestMsg(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     NpcQuestMsg* out_msg) {
    if (!out_msg) {
        return MessageCodecStatus::kInvalidPayload;
    }

    if (msg_id != kNpcQuestAcceptMsgId && msg_id != kNpcQuestCompleteMsgId) {
        return MessageCodecStatus::kInvalidMsgId;
    }

    json j;
    auto status = ParseJsonObject(data, size, &j);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    uint32_t version = 0;
    status = ReadVersion(j, &version);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ValidateVersion(version);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    status = ReadUInt32(j, "quest_id", &out_msg->quest_id);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }

    out_msg->status = (msg_id == kNpcQuestCompleteMsgId) ? NpcQuestStatus::kCompleted
                                                         : NpcQuestStatus::kAccepted;

    if (j.contains("status")) {
        uint8_t raw_status = 0;
        status = ReadUInt8(j, "status", &raw_status);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
        out_msg->status = static_cast<NpcQuestStatus>(raw_status);
    }

    if (j.contains("npc_id")) {
        uint64_t npc_id = 0;
        status = ReadUInt64(j, "npc_id", &npc_id);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
        out_msg->npc_id = npc_id;
    } else {
        out_msg->npc_id = 0;
    }

    return ValidateNpcQuestMsg(*out_msg);
}

MessageCodecStatus DecodeNpcQuestMsg(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     NpcQuestMsg* out_msg) {
    return DecodeNpcQuestMsg(msg_id, payload.data(), payload.size(), out_msg);
}

}  // namespace mir2::common
