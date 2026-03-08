#include "client/handlers/achievement_handler.h"

#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "achievement_generated.h"
#include "common/enums.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::AchievementHandler;
using mir2::game::handlers::AchievementProgressData;

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

class AchievementClientHandlerTest : public ::testing::Test {
protected:
    AchievementHandler::Callbacks MakeCallbacks() {
        AchievementHandler::Callbacks callbacks;
        callbacks.on_update = [this](const AchievementProgressData& progress) {
            update_ = progress;
        };
        return callbacks;
    }

    std::optional<AchievementProgressData> update_;
};

TEST_F(AchievementClientHandlerTest, BindHandlersRegistersAchievementMessages) {
    auto handler = std::make_shared<AchievementHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_TRUE(manager.HasHandler(MsgId::kAchievementListRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kAchievementClaimRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kAchievementUpdate));
}

TEST_F(AchievementClientHandlerTest, HandleUpdateParsesProgress) {
    flatbuffers::FlatBufferBuilder builder;
    const auto progress = mir2::proto::CreateAchievementProgress(
        builder, 10, 5, 10, false, false, 0, 500);
    const auto update = mir2::proto::CreateAchievementUpdate(builder, progress);
    builder.Finish(update);

    auto handler = std::make_shared<AchievementHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kAchievementUpdate, builder);
    handler->HandleUpdate(packet);

    ASSERT_TRUE(update_.has_value());
    EXPECT_EQ(update_->achievement_id, 10u);
    EXPECT_EQ(update_->progress, 5u);
    EXPECT_EQ(update_->target, 10u);
    EXPECT_EQ(update_->reward_gold, 500u);
}

TEST_F(AchievementClientHandlerTest, SendClaimRequestEncodesPayload) {
    MockNetworkManager manager;

    AchievementHandler::SendClaimRequest(manager, 42);

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kAchievementClaimReq);
    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::AchievementClaimReq>(nullptr));
    const auto* req = flatbuffers::GetRoot<mir2::proto::AchievementClaimReq>(
        manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->achievement_id(), 42u);
}

} // namespace
