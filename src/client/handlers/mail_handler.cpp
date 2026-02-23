#include "client/handlers/mail_handler.h"

#include "common/enums.h"
#include "mail_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const MailHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

mir2::proto::ErrorCode ToProtoError(int error_code) {
    return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(error_code));
}

std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

} // namespace

MailHandler::MailHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void MailHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Mail handlers are bound per-instance to capture callbacks.
}

void MailHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id,
                                       auto fn) {
        manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
            if (auto self = weak_self.lock()) {
                (self.get()->*fn)(packet);
            }
        });
    };

    bind(mir2::common::MsgId::kMailSendRsp, &MailHandler::HandleSendResponse);
    bind(mir2::common::MsgId::kMailListRsp, &MailHandler::HandleListResponse);
    bind(mir2::common::MsgId::kMailReadRsp, &MailHandler::HandleReadResponse);
    bind(mir2::common::MsgId::kMailDeleteRsp, &MailHandler::HandleDeleteResponse);
    bind(mir2::common::MsgId::kMailClaimRsp, &MailHandler::HandleClaimResponse);
    bind(mir2::common::MsgId::kMailNotify, &MailHandler::HandleMailNotify);
}

void MailHandler::HandleSendResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailSendRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(packet.payload.data());
    if (rsp && callbacks_.on_send_response) {
        callbacks_.on_send_response(rsp->success(),
                                    ToProtoError(rsp->error_code()),
                                    rsp->mail_id());
    }
}

void MailHandler::HandleListResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MailListRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailListRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::MailListRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailListRsp parse failed");
        }
        return;
    }

    if (callbacks_.on_list_response) {
        std::vector<MailSummaryData> mails;
        if (const auto* summaries = rsp->mails()) {
            mails.reserve(summaries->size());
            for (const auto* summary : *summaries) {
                if (!summary) {
                    continue;
                }
                mails.push_back(BuildSummaryView(summary));
            }
        }
        callbacks_.on_list_response(rsp->success(), ToProtoError(rsp->error_code()), mails);
    }
}

void MailHandler::HandleReadResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MailReadRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailReadRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::MailReadRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailReadRsp parse failed");
        }
        return;
    }

    if (callbacks_.on_read_response) {
        MailDetailData mail;
        if (rsp->mail()) {
            mail = BuildDetailView(rsp->mail());
        }
        callbacks_.on_read_response(rsp->success(), ToProtoError(rsp->error_code()), mail);
    }
}

void MailHandler::HandleDeleteResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MailDeleteRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailDeleteRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::MailDeleteRsp>(packet.payload.data());
    if (rsp && callbacks_.on_delete_response) {
        callbacks_.on_delete_response(rsp->success(),
                                      ToProtoError(rsp->error_code()),
                                      rsp->mail_id());
    }
}

void MailHandler::HandleClaimResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MailClaimRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailClaimRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::MailClaimRsp>(packet.payload.data());
    if (rsp && callbacks_.on_claim_response) {
        callbacks_.on_claim_response(rsp->success(),
                                     ToProtoError(rsp->error_code()),
                                     rsp->mail_id());
    }
}

void MailHandler::HandleMailNotify(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::MailNotify>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("MailNotify verification failed");
        }
        return;
    }

    const auto* notify = flatbuffers::GetRoot<mir2::proto::MailNotify>(packet.payload.data());
    if (!notify || !callbacks_.on_mail_notify || !notify->mail()) {
        return;
    }

    callbacks_.on_mail_notify(BuildSummaryView(notify->mail()), notify->unread_count());
}

void MailHandler::SendMailSendRequest(mir2::client::INetworkManager& manager,
                                      uint64_t target_character_id,
                                      const std::string& subject,
                                      const std::string& content,
                                      uint32_t gold,
                                      const std::vector<MailAttachmentData>& items,
                                      uint64_t expire_time) {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<mir2::proto::MailAttachmentItem>> item_offsets;
    item_offsets.reserve(items.size());
    for (const auto& item : items) {
        item_offsets.emplace_back(
            mir2::proto::CreateMailAttachmentItem(builder, item.item_id, item.count));
    }
    const auto items_vec = builder.CreateVector(item_offsets);
    const auto subject_offset = builder.CreateString(subject);
    const auto content_offset = builder.CreateString(content);
    const auto req = mir2::proto::CreateMailSendReq(builder,
                                                    target_character_id,
                                                    subject_offset,
                                                    content_offset,
                                                    gold,
                                                    items_vec,
                                                    expire_time);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kMailSendReq, BuildPayload(builder));
}

void MailHandler::SendMailListRequest(mir2::client::INetworkManager& manager) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateMailListReq(builder);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kMailListReq, BuildPayload(builder));
}

void MailHandler::SendMailReadRequest(mir2::client::INetworkManager& manager, uint64_t mail_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateMailReadReq(builder, mail_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kMailReadReq, BuildPayload(builder));
}

void MailHandler::SendMailDeleteRequest(mir2::client::INetworkManager& manager,
                                        uint64_t mail_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateMailDeleteReq(builder, mail_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kMailDeleteReq, BuildPayload(builder));
}

void MailHandler::SendMailClaimRequest(mir2::client::INetworkManager& manager, uint64_t mail_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateMailClaimReq(builder, mail_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kMailClaimReq, BuildPayload(builder));
}

MailSummaryData MailHandler::BuildSummaryView(const mir2::proto::MailSummary* summary) {
    MailSummaryData data;
    if (!summary) {
        return data;
    }
    data.mail_id = summary->mail_id();
    data.from_character_id = summary->from_character_id();
    data.subject = summary->subject() ? summary->subject()->str() : std::string{};
    data.has_attachment = summary->has_attachment();
    data.is_read = summary->is_read();
    data.claimed = summary->claimed();
    data.send_time = summary->send_time();
    data.expire_time = summary->expire_time();
    data.gold = summary->gold();
    data.attachment_count = summary->attachment_count();
    return data;
}

MailDetailData MailHandler::BuildDetailView(const mir2::proto::MailDetail* detail) {
    MailDetailData data;
    if (!detail) {
        return data;
    }
    data.mail_id = detail->mail_id();
    data.from_character_id = detail->from_character_id();
    data.subject = detail->subject() ? detail->subject()->str() : std::string{};
    data.content = detail->content() ? detail->content()->str() : std::string{};
    data.is_read = detail->is_read();
    data.claimed = detail->claimed();
    data.send_time = detail->send_time();
    data.expire_time = detail->expire_time();
    data.gold = detail->gold();
    if (const auto* items = detail->items()) {
        data.items.reserve(items->size());
        for (const auto* item : *items) {
            if (!item) {
                continue;
            }
            MailAttachmentData attachment;
            attachment.item_id = item->item_id();
            attachment.count = item->count();
            data.items.push_back(attachment);
        }
    }
    return data;
}

} // namespace mir2::game::handlers
