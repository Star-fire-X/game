/**
 * @file mail_handler.h
 * @brief Client mail message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_MAIL_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_MAIL_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
class MailSummary;
class MailDetail;
}

namespace mir2::game::handlers {

struct MailAttachmentData {
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct MailSummaryData {
    uint64_t mail_id = 0;
    uint64_t from_character_id = 0;
    std::string subject;
    bool has_attachment = false;
    bool is_read = false;
    bool claimed = false;
    uint64_t send_time = 0;
    uint64_t expire_time = 0;
    uint32_t gold = 0;
    uint32_t attachment_count = 0;
};

struct MailDetailData {
    uint64_t mail_id = 0;
    uint64_t from_character_id = 0;
    std::string subject;
    std::string content;
    bool is_read = false;
    bool claimed = false;
    uint64_t send_time = 0;
    uint64_t expire_time = 0;
    uint32_t gold = 0;
    std::vector<MailAttachmentData> items;
};

class MailHandler : public std::enable_shared_from_this<MailHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;

        std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t mail_id)>
            on_send_response;
        std::function<void(bool success, mir2::proto::ErrorCode code,
                           const std::vector<MailSummaryData>& mails)> on_list_response;
        std::function<void(bool success, mir2::proto::ErrorCode code,
                           const MailDetailData& mail)> on_read_response;
        std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t mail_id)>
            on_delete_response;
        std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t mail_id)>
            on_claim_response;
        std::function<void(const MailSummaryData& mail, uint32_t unread_count)> on_mail_notify;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit MailHandler(Callbacks callbacks);
    ~MailHandler() = default;

    MailHandler(const MailHandler&) = delete;
    MailHandler& operator=(const MailHandler&) = delete;

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleSendResponse(const NetworkPacket& packet);
    void HandleListResponse(const NetworkPacket& packet);
    void HandleReadResponse(const NetworkPacket& packet);
    void HandleDeleteResponse(const NetworkPacket& packet);
    void HandleClaimResponse(const NetworkPacket& packet);
    void HandleMailNotify(const NetworkPacket& packet);

    static void SendMailSendRequest(mir2::client::INetworkManager& manager,
                                    uint64_t target_character_id,
                                    const std::string& subject,
                                    const std::string& content,
                                    uint32_t gold,
                                    const std::vector<MailAttachmentData>& items = {},
                                    uint64_t expire_time = 0);
    static void SendMailListRequest(mir2::client::INetworkManager& manager);
    static void SendMailReadRequest(mir2::client::INetworkManager& manager, uint64_t mail_id);
    static void SendMailDeleteRequest(mir2::client::INetworkManager& manager, uint64_t mail_id);
    static void SendMailClaimRequest(mir2::client::INetworkManager& manager, uint64_t mail_id);

private:
    static MailSummaryData BuildSummaryView(const mir2::proto::MailSummary* summary);
    static MailDetailData BuildDetailView(const mir2::proto::MailDetail* detail);

    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_MAIL_HANDLER_H
