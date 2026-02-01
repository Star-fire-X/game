/**
 * @file npc_message_codec.h
 * @brief NPC protocol JSON message codec helpers.
 */

#ifndef LEGEND2_COMMON_PROTOCOL_NPC_MESSAGE_CODEC_H
#define LEGEND2_COMMON_PROTOCOL_NPC_MESSAGE_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/enums.h"
#include "common/protocol/message_codec.h"

namespace mir2::common {

constexpr uint32_t kNpcCodecVersion = 1;

constexpr uint16_t kNpcInteractRequestMsgId = static_cast<uint16_t>(MsgId::kNpcInteractReq);
constexpr uint16_t kNpcDialogShowMsgId = static_cast<uint16_t>(MsgId::kNpcDialogShow);
constexpr uint16_t kNpcMenuSelectMsgId = static_cast<uint16_t>(MsgId::kNpcMenuSelect);
constexpr uint16_t kNpcShopOpenMsgId = static_cast<uint16_t>(MsgId::kNpcShopOpen);
constexpr uint16_t kNpcQuestAcceptMsgId = static_cast<uint16_t>(MsgId::kNpcQuestAccept);
constexpr uint16_t kNpcQuestCompleteMsgId = static_cast<uint16_t>(MsgId::kNpcQuestComplete);

constexpr size_t kMaxNpcDialogTextLength = 4096;
constexpr size_t kMaxNpcDialogOptionLength = 256;
constexpr size_t kMaxNpcDialogOptions = 64;
constexpr size_t kMaxNpcShopItems = 256;

struct NpcInteractReq {
    uint64_t npc_id = 0;
    uint64_t player_id = 0;
};

struct NpcDialogShowMsg {
    uint64_t npc_id = 0;
    std::string text;
    std::vector<std::string> options;
};

struct NpcMenuSelectReq {
    uint64_t npc_id = 0;
    uint8_t option_index = 0;
};

struct NpcShopItem {
    uint32_t item_id = 0;
    uint32_t price = 0;
    uint16_t stock = 0;
};

struct NpcShopOpenMsg {
    uint32_t shop_id = 0;
    std::vector<NpcShopItem> items;
    uint64_t npc_id = 0;  // Optional (compatibility with legacy payloads).
};

enum class NpcQuestStatus : uint8_t {
    kUnknown = 0,
    kAccepted = 1,
    kCompleted = 2
};

struct NpcQuestMsg {
    uint32_t quest_id = 0;
    NpcQuestStatus status = NpcQuestStatus::kUnknown;
    uint64_t npc_id = 0;  // Optional (compatibility with legacy payloads).
};

MessageCodecStatus ValidateNpcInteractReq(const NpcInteractReq& request);
MessageCodecStatus ValidateNpcDialogShowMsg(const NpcDialogShowMsg& msg);
MessageCodecStatus ValidateNpcMenuSelectReq(const NpcMenuSelectReq& request);
MessageCodecStatus ValidateNpcShopOpenMsg(const NpcShopOpenMsg& msg);
MessageCodecStatus ValidateNpcQuestMsg(const NpcQuestMsg& msg);

std::vector<uint8_t> EncodeNpcInteractReq(const NpcInteractReq& request,
                                          MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeNpcMenuSelect(const NpcMenuSelectReq& request,
                                         MessageCodecStatus* out_status = nullptr);

MessageCodecStatus DecodeNpcDialogShow(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       NpcDialogShowMsg* out_msg);
MessageCodecStatus DecodeNpcDialogShow(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       NpcDialogShowMsg* out_msg);

MessageCodecStatus DecodeNpcShopOpen(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     NpcShopOpenMsg* out_msg);
MessageCodecStatus DecodeNpcShopOpen(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     NpcShopOpenMsg* out_msg);

MessageCodecStatus DecodeNpcQuestMsg(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     NpcQuestMsg* out_msg);
MessageCodecStatus DecodeNpcQuestMsg(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     NpcQuestMsg* out_msg);

}  // namespace mir2::common

#endif  // LEGEND2_COMMON_PROTOCOL_NPC_MESSAGE_CODEC_H
