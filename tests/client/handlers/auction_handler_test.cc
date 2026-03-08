#include "client/handlers/auction_handler.h"

#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "auction_generated.h"
#include "common/enums.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::AuctionHandler;
using mir2::game::handlers::AuctionListingData;

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

class AuctionClientHandlerTest : public ::testing::Test {
 protected:
  AuctionHandler::Callbacks MakeCallbacks() {
    AuctionHandler::Callbacks callbacks;
    callbacks.on_list_response =
        [this](bool success,
               mir2::proto::ErrorCode code,
               uint32_t total_count,
               const std::vector<AuctionListingData>& listings) {
          list_response_ = std::make_tuple(success, code, total_count, listings);
        };
    callbacks.on_notify =
        [this](mir2::proto::AuctionNotifyType notify_type,
               const AuctionListingData& listing) {
          notify_ = std::make_pair(notify_type, listing);
        };
    return callbacks;
  }

  std::optional<std::tuple<bool,
                           mir2::proto::ErrorCode,
                           uint32_t,
                           std::vector<AuctionListingData>>>
      list_response_;
  std::optional<std::pair<mir2::proto::AuctionNotifyType, AuctionListingData>> notify_;
};

TEST_F(AuctionClientHandlerTest, BindHandlersRegistersAuctionMessages) {
  auto handler = std::make_shared<AuctionHandler>(MakeCallbacks());
  MockNetworkManager manager;

  handler->BindHandlers(manager);

  EXPECT_TRUE(manager.HasHandler(MsgId::kAuctionListRsp));
  EXPECT_TRUE(manager.HasHandler(MsgId::kAuctionSellRsp));
  EXPECT_TRUE(manager.HasHandler(MsgId::kAuctionBuyRsp));
  EXPECT_TRUE(manager.HasHandler(MsgId::kAuctionCancelRsp));
  EXPECT_TRUE(manager.HasHandler(MsgId::kAuctionNotify));
}

TEST_F(AuctionClientHandlerTest, HandleAuctionListResponseParsesListings) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::AuctionListing>> listings;
  listings.emplace_back(mir2::proto::CreateAuctionListing(builder,
                                                          77,
                                                          1001,
                                                          3001,
                                                          2,
                                                          500,
                                                          1000,
                                                          1,
                                                          2,
                                                          false,
                                                          false));
  const auto listing_vec = builder.CreateVector(listings);
  const auto rsp = mir2::proto::CreateAuctionListRsp(
      builder,
      true,
      static_cast<int>(mir2::proto::ErrorCode::ERR_OK),
      1,
      listing_vec);
  builder.Finish(rsp);

  auto handler = std::make_shared<AuctionHandler>(MakeCallbacks());
  NetworkPacket packet = MakePacket(MsgId::kAuctionListRsp, builder);
  handler->HandleAuctionListResponse(packet);

  ASSERT_TRUE(list_response_.has_value());
  EXPECT_TRUE(std::get<0>(*list_response_));
  EXPECT_EQ(std::get<1>(*list_response_), mir2::proto::ErrorCode::ERR_OK);
  EXPECT_EQ(std::get<2>(*list_response_), 1u);
  const auto& listings_data = std::get<3>(*list_response_);
  ASSERT_EQ(listings_data.size(), 1u);
  EXPECT_EQ(listings_data[0].listing_id, 77u);
  EXPECT_EQ(listings_data[0].item_id, 3001u);
  EXPECT_EQ(listings_data[0].count, 2u);
  EXPECT_EQ(listings_data[0].total_price, 1000u);
}

TEST_F(AuctionClientHandlerTest, HandleAuctionNotifyParsesPayload) {
  flatbuffers::FlatBufferBuilder builder;
  const auto listing = mir2::proto::CreateAuctionListing(builder,
                                                          88,
                                                          1002,
                                                          4001,
                                                          1,
                                                          700,
                                                          700,
                                                          10,
                                                          20,
                                                          true,
                                                          false);
  const auto notify = mir2::proto::CreateAuctionNotify(
      builder, mir2::proto::AuctionNotifyType::SOLD, listing);
  builder.Finish(notify);

  auto handler = std::make_shared<AuctionHandler>(MakeCallbacks());
  NetworkPacket packet = MakePacket(MsgId::kAuctionNotify, builder);
  handler->HandleAuctionNotify(packet);

  ASSERT_TRUE(notify_.has_value());
  EXPECT_EQ(notify_->first, mir2::proto::AuctionNotifyType::SOLD);
  EXPECT_EQ(notify_->second.listing_id, 88u);
  EXPECT_EQ(notify_->second.item_id, 4001u);
}

TEST_F(AuctionClientHandlerTest, SendAuctionSellRequestEncodesPayload) {
  MockNetworkManager manager;

  AuctionHandler::SendAuctionSellRequest(
      manager, /*inventory_slot=*/3, /*item_id=*/5001, /*count=*/2, /*unit_price=*/999,
      /*duration_sec=*/7200);

  ASSERT_EQ(manager.sent_messages().size(), 1u);
  EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kAuctionSellReq);

  flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                 manager.sent_messages()[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::AuctionSellReq>(nullptr));
  const auto* req = flatbuffers::GetRoot<mir2::proto::AuctionSellReq>(
      manager.sent_messages()[0].payload.data());
  ASSERT_NE(req, nullptr);
  EXPECT_EQ(req->inventory_slot(), 3u);
  EXPECT_EQ(req->item_id(), 5001u);
  EXPECT_EQ(req->count(), 2u);
  EXPECT_EQ(req->unit_price(), 999u);
  EXPECT_EQ(req->duration_sec(), 7200u);
}

}  // namespace
