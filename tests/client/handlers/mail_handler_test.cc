#include "client/handlers/mail_handler.h"

#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/enums.h"
#include "mail_generated.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::MailHandler;
using mir2::game::handlers::MailSummaryData;

class MockNetworkManager : public INetworkManager {
public:
    bool connect(const std::string&, uint16_t) override { return false; }
    void disconnect() override {}
    bool is_connected() const override { return false; }

    void send_message(mir2::common::MsgId msg_id,
                      const std::vector<uint8_t>& payload) override {
        sent_messages_.push_back({msg_id, payload});
    }

    void register_handler(mir2::common::MsgId msg_id, HandlerFunc handler) override {
        handlers_[msg_id] = std::move(handler);
    }

    void set_default_handler(HandlerFunc handler) override {
        default_handler_ = std::move(handler);
    }

    void set_on_connect(EventCallback callback) override {
        on_connect_ = std::move(callback);
    }

    void set_on_disconnect(EventCallback callback) override {
        on_disconnect_ = std::move(callback);
    }

    ConnectionState get_state() const override {
        return ConnectionState::DISCONNECTED;
    }

    mir2::common::ErrorCode get_last_error() const override {
        return mir2::common::ErrorCode::SUCCESS;
    }

    void update() override {}

    bool HasHandler(MsgId msg_id) const {
        return handlers_.find(msg_id) != handlers_.end();
    }

    struct SentMessage {
        mir2::common::MsgId msg_id = mir2::common::MsgId::kNone;
        std::vector<uint8_t> payload;
    };

    const std::vector<SentMessage>& sent_messages() const { return sent_messages_; }

private:
    std::map<mir2::common::MsgId, HandlerFunc> handlers_;
    std::vector<SentMessage> sent_messages_;
    HandlerFunc default_handler_;
    EventCallback on_connect_;
    EventCallback on_disconnect_;
};

NetworkPacket MakePacket(MsgId msg_id, flatbuffers::FlatBufferBuilder& builder) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    const uint8_t* data = builder.GetBufferPointer();
    packet.payload.assign(data, data + builder.GetSize());
    return packet;
}

class MailClientHandlerTest : public ::testing::Test {
protected:
    MailHandler::Callbacks MakeCallbacks() {
        MailHandler::Callbacks callbacks;
        callbacks.on_mail_notify =
            [this](const MailSummaryData& mail, uint32_t unread_count) {
                notified_mail_ = mail;
                unread_count_ = unread_count;
            };
        return callbacks;
    }

    std::optional<MailSummaryData> notified_mail_;
    uint32_t unread_count_ = 0;
};

TEST_F(MailClientHandlerTest, BindHandlersRegistersMailMessages) {
    auto handler = std::make_shared<MailHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_TRUE(manager.HasHandler(MsgId::kMailSendRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kMailListRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kMailReadRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kMailDeleteRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kMailClaimRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kMailNotify));
}

TEST_F(MailClientHandlerTest, HandleMailNotifyParsesSummary) {
    flatbuffers::FlatBufferBuilder builder;
    const auto subject = builder.CreateString("Subject");
    const auto summary = mir2::proto::CreateMailSummary(
        builder, 99, 1001, subject, true, false, false, 1, 2, 0, 0);
    const auto notify = mir2::proto::CreateMailNotify(builder, summary, 3);
    builder.Finish(notify);

    auto handler = std::make_shared<MailHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kMailNotify, builder);
    handler->HandleMailNotify(packet);

    ASSERT_TRUE(notified_mail_.has_value());
    EXPECT_EQ(notified_mail_->mail_id, 99u);
    EXPECT_EQ(notified_mail_->from_character_id, 1001u);
    EXPECT_EQ(notified_mail_->subject, "Subject");
    EXPECT_EQ(unread_count_, 3u);
}

TEST_F(MailClientHandlerTest, SendMailReadRequestEncodesPayload) {
    MockNetworkManager manager;

    MailHandler::SendMailReadRequest(manager, 1234);

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kMailReadReq);
    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::MailReadReq>(nullptr));
    const auto* req =
        flatbuffers::GetRoot<mir2::proto::MailReadReq>(manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->mail_id(), 1234u);
}

} // namespace
