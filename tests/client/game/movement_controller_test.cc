#include <gtest/gtest.h>

#include "client/game/movement_controller.h"

#include <vector>

namespace {

class FakeWalkabilityProvider : public mir2::game::map::IWalkabilityProvider {
public:
    bool walkable = true;
    bool valid = true;

    bool is_walkable(int /*x*/, int /*y*/) const override { return walkable; }
    bool is_valid_position(int /*x*/, int /*y*/) const override { return valid; }
    mir2::common::Size get_map_size() const override { return {0, 0}; }
    std::vector<mir2::common::Position> get_neighbors(
        const mir2::common::Position& /*pos*/) const override {
        return {};
    }
};

class FakeNetworkManager : public mir2::client::INetworkManager {
public:
    bool connected = true;
    int send_calls = 0;
    mir2::common::MsgId last_msg_id = mir2::common::MsgId::kNone;
    std::vector<uint8_t> last_payload;

    bool connect(const std::string& /*host*/, uint16_t /*port*/) override {
        connected = true;
        return true;
    }
    void disconnect() override { connected = false; }
    bool is_connected() const override { return connected; }

    void send_message(mir2::common::MsgId msg_id, const std::vector<uint8_t>& payload) override {
        ++send_calls;
        last_msg_id = msg_id;
        last_payload = payload;
    }

    void register_handler(mir2::common::MsgId /*msg_id*/, HandlerFunc /*handler*/) override {}
    void set_default_handler(HandlerFunc /*handler*/) override {}
    void set_on_connect(EventCallback /*callback*/) override {}
    void set_on_disconnect(EventCallback /*callback*/) override {}

    mir2::client::ConnectionState get_state() const override {
        return connected ? mir2::client::ConnectionState::CONNECTED : mir2::client::ConnectionState::DISCONNECTED;
    }

    mir2::client::ErrorCode get_last_error() const override { return mir2::client::ErrorCode::SUCCESS; }

    void update() override {}
};

} // namespace

TEST(MovementControllerTest, RejectsNonWalkableTarget) {
    FakeWalkabilityProvider walkability;
    walkability.walkable = false;
    FakeNetworkManager network;
    legend2::PositionInterpolator interpolator;
    interpolator.set_immediate(mir2::common::Position{0, 0});

    mir2::game::MovementController controller(walkability, network, interpolator);

    const bool accepted = controller.request_move({0, 0}, {5, 5});
    EXPECT_FALSE(accepted);
    EXPECT_EQ(network.send_calls, 0);
}

TEST(MovementControllerTest, SendsMoveRequestForWalkableTarget) {
    FakeWalkabilityProvider walkability;
    walkability.walkable = true;
    FakeNetworkManager network;
    legend2::PositionInterpolator interpolator;
    interpolator.set_immediate(mir2::common::Position{0, 0});

    mir2::game::MovementController controller(walkability, network, interpolator);

    const bool accepted = controller.request_move({0, 0}, {7, 9});
    EXPECT_TRUE(accepted);
    EXPECT_EQ(network.send_calls, 1);
    EXPECT_EQ(network.last_msg_id, mir2::common::MsgId::kMoveReq);

    mir2::common::MoveRequest decoded;
    const auto status = mir2::common::DecodeMoveRequest(
        mir2::common::kMoveRequestMsgId,
        network.last_payload,
        &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.target_x, 7);
    EXPECT_EQ(decoded.target_y, 9);
}

TEST(MovementControllerTest, MoveResponseUpdatesInterpolator) {
    FakeWalkabilityProvider walkability;
    FakeNetworkManager network;
    legend2::PositionInterpolator interpolator;
    interpolator.set_immediate(mir2::common::Position{0, 0});

    mir2::game::MovementController controller(walkability, network, interpolator);
    controller.on_move_response({3, 4});
    controller.update(500.0f);

    auto pos = interpolator.get_tile_position();
    EXPECT_EQ(pos.x, 3);
    EXPECT_EQ(pos.y, 4);
}

TEST(MovementControllerTest, MoveFailedRollsBackToLastConfirmed) {
    FakeWalkabilityProvider walkability;
    FakeNetworkManager network;
    legend2::PositionInterpolator interpolator;
    interpolator.set_immediate(mir2::common::Position{0, 0});

    mir2::game::MovementController controller(walkability, network, interpolator);
    controller.on_move_response({2, 2});
    controller.request_move({2, 2}, {10, 10});
    controller.on_move_failed();
    controller.update(500.0f);

    auto pos = interpolator.get_tile_position();
    EXPECT_EQ(pos.x, 2);
    EXPECT_EQ(pos.y, 2);
}
