#include "handlers/chat/chat_handler.h"

#include <chrono>
#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "common/enums.h"
#include "handlers/handler_utils.h"

namespace legend2::handlers {

namespace {

std::vector<uint8_t> BuildChatRsp(mir2::common::ErrorCode code) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateChatRsp(builder, ToProtoError(code));
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildChatMessage(mir2::proto::ChatChannel channel,
                                      uint64_t from_id,
                                      const std::string& from_name,
                                      uint64_t to_id,
                                      const std::string& content,
                                      uint32_t timestamp) {
    flatbuffers::FlatBufferBuilder builder;
    const auto name_offset = builder.CreateString(from_name);
    const auto content_offset = builder.CreateString(content);
    const auto rsp = mir2::proto::CreateChatMessage(
        builder, channel, from_id, name_offset, to_id, content_offset, timestamp);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

uint32_t NowSeconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

}  // namespace

ChatHandler::ChatHandler(ClientRegistry& registry)
    : BaseHandler(mir2::log::LogCategory::kGame),
      client_registry_(registry) {}

void ChatHandler::DoHandle(const HandlerContext& context,
                           uint16_t msg_id,
                           const std::vector<uint8_t>& payload,
                           ResponseCallback callback) {
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kChatReq)) {
        HandleChat(context, payload, std::move(callback));
        return;
    }

    OnError(context, msg_id, mir2::common::ErrorCode::kInvalidAction, std::move(callback));
}

void ChatHandler::OnError(const HandlerContext& context,
                          uint16_t msg_id,
                          mir2::common::ErrorCode error_code,
                          ResponseCallback callback) {
    BaseHandler::OnError(context, msg_id, error_code, callback);

    if (msg_id != static_cast<uint16_t>(mir2::common::MsgId::kChatReq)) {
        if (callback) {
            callback(ResponseList{});
        }
        return;
    }

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kChatRsp),
                         BuildChatRsp(error_code)});
    if (callback) {
        callback(responses);
    }
}

void ChatHandler::HandleChat(const HandlerContext& context,
                             const std::vector<uint8_t>& payload,
                             ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ChatReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::ChatReq>(payload.data());
    if (!req || !req->content()) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const std::string content = req->content()->str();
    if (content.empty()) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto channel = req->channel();
    const uint64_t target_id = req->target_id();
    const std::string from_name = std::string("Player") + std::to_string(context.client_id);

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kChatRsp),
                         BuildChatRsp(mir2::common::ErrorCode::kOk)});

    uint16_t message_type = static_cast<uint16_t>(mir2::common::MsgId::kChatReq);
    std::vector<uint64_t> targets;

    if (channel == mir2::proto::ChatChannel::PRIVATE) {
        message_type = static_cast<uint16_t>(mir2::common::MsgId::kPrivateChat);
        if (!client_registry_.Contains(target_id)) {
            OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                    mir2::common::ErrorCode::kTargetNotFound, std::move(callback));
            return;
        }
        targets.push_back(target_id);
    } else if (channel == mir2::proto::ChatChannel::GUILD) {
        message_type = static_cast<uint16_t>(mir2::common::MsgId::kGuildChat);
        targets = client_registry_.GetAll();
    } else {
        message_type = static_cast<uint16_t>(mir2::common::MsgId::kChatReq);
        targets = client_registry_.GetAll();
    }

    const auto chat_payload = BuildChatMessage(
        channel, context.client_id, from_name, target_id, content, NowSeconds());
    for (const auto client_id : targets) {
        responses.push_back({client_id, message_type, chat_payload});
    }

    if (callback) {
        callback(responses);
    }
}

}  // namespace legend2::handlers
