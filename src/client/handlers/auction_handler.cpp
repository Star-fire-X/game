#include "client/handlers/auction_handler.h"

#include "auction_generated.h"
#include "common/enums.h"

#include <flatbuffers/flatbuffers.h>

#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const AuctionHandler::Callbacks& callbacks,
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

}  // namespace

AuctionHandler::AuctionHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void AuctionHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
  // Auction handlers are bound per-instance to capture callbacks.
}

void AuctionHandler::BindHandlers(mir2::client::INetworkManager& manager) {
  const auto weak_self = weak_from_this();

  auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id, auto fn) {
    manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
      if (auto self = weak_self.lock()) {
        (self.get()->*fn)(packet);
      }
    });
  };

  bind(mir2::common::MsgId::kAuctionListRsp, &AuctionHandler::HandleAuctionListResponse);
  bind(mir2::common::MsgId::kAuctionSellRsp, &AuctionHandler::HandleAuctionSellResponse);
  bind(mir2::common::MsgId::kAuctionBuyRsp, &AuctionHandler::HandleAuctionBuyResponse);
  bind(mir2::common::MsgId::kAuctionCancelRsp, &AuctionHandler::HandleAuctionCancelResponse);
  bind(mir2::common::MsgId::kAuctionNotify, &AuctionHandler::HandleAuctionNotify);
}

void AuctionHandler::HandleAuctionListResponse(const NetworkPacket& packet) {
  std::shared_ptr<void> owner_guard;
  if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
    return;
  }

  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::AuctionListRsp>(nullptr)) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionListRsp verification failed");
    }
    return;
  }

  const auto* rsp = flatbuffers::GetRoot<mir2::proto::AuctionListRsp>(packet.payload.data());
  if (!rsp) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionListRsp parse failed");
    }
    return;
  }

  if (callbacks_.on_list_response) {
    std::vector<AuctionListingData> listings;
    if (const auto* listing_vector = rsp->listings()) {
      listings.reserve(listing_vector->size());
      for (const auto* listing : *listing_vector) {
        if (!listing) {
          continue;
        }
        listings.push_back(ParseListing(*listing));
      }
    }

    callbacks_.on_list_response(
        rsp->success(), ToProtoError(rsp->error_code()), rsp->total_count(), listings);
  }
}

void AuctionHandler::HandleAuctionSellResponse(const NetworkPacket& packet) {
  std::shared_ptr<void> owner_guard;
  if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
    return;
  }

  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr)) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionSellRsp verification failed");
    }
    return;
  }

  const auto* rsp = flatbuffers::GetRoot<mir2::proto::AuctionSellRsp>(packet.payload.data());
  if (rsp && callbacks_.on_sell_response) {
    callbacks_.on_sell_response(
        rsp->success(), ToProtoError(rsp->error_code()), rsp->listing_id());
  }
}

void AuctionHandler::HandleAuctionBuyResponse(const NetworkPacket& packet) {
  std::shared_ptr<void> owner_guard;
  if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
    return;
  }

  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::AuctionBuyRsp>(nullptr)) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionBuyRsp verification failed");
    }
    return;
  }

  const auto* rsp = flatbuffers::GetRoot<mir2::proto::AuctionBuyRsp>(packet.payload.data());
  if (rsp && callbacks_.on_buy_response) {
    callbacks_.on_buy_response(
        rsp->success(), ToProtoError(rsp->error_code()), rsp->listing_id());
  }
}

void AuctionHandler::HandleAuctionCancelResponse(const NetworkPacket& packet) {
  std::shared_ptr<void> owner_guard;
  if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
    return;
  }

  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::AuctionCancelRsp>(nullptr)) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionCancelRsp verification failed");
    }
    return;
  }

  const auto* rsp = flatbuffers::GetRoot<mir2::proto::AuctionCancelRsp>(packet.payload.data());
  if (rsp && callbacks_.on_cancel_response) {
    callbacks_.on_cancel_response(
        rsp->success(), ToProtoError(rsp->error_code()), rsp->listing_id());
  }
}

void AuctionHandler::HandleAuctionNotify(const NetworkPacket& packet) {
  std::shared_ptr<void> owner_guard;
  if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
    return;
  }

  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::AuctionNotify>(nullptr)) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionNotify verification failed");
    }
    return;
  }

  const auto* notify = flatbuffers::GetRoot<mir2::proto::AuctionNotify>(packet.payload.data());
  if (!notify || !notify->listing()) {
    if (callbacks_.on_parse_error) {
      callbacks_.on_parse_error("AuctionNotify parse failed");
    }
    return;
  }

  if (callbacks_.on_notify) {
    callbacks_.on_notify(notify->notify_type(), ParseListing(*notify->listing()));
  }
}

void AuctionHandler::SendAuctionListRequest(mir2::client::INetworkManager& manager,
                                            uint32_t page,
                                            uint32_t page_size,
                                            bool seller_only) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionListReq(builder, page, page_size, seller_only);
  builder.Finish(req);
  manager.send_message(mir2::common::MsgId::kAuctionListReq, BuildPayload(builder));
}

void AuctionHandler::SendAuctionSellRequest(mir2::client::INetworkManager& manager,
                                            uint16_t inventory_slot,
                                            uint32_t item_id,
                                            uint32_t count,
                                            uint32_t unit_price,
                                            uint32_t duration_sec) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionSellReq(
      builder, inventory_slot, item_id, count, unit_price, duration_sec);
  builder.Finish(req);
  manager.send_message(mir2::common::MsgId::kAuctionSellReq, BuildPayload(builder));
}

void AuctionHandler::SendAuctionBuyRequest(mir2::client::INetworkManager& manager,
                                           uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionBuyReq(builder, listing_id);
  builder.Finish(req);
  manager.send_message(mir2::common::MsgId::kAuctionBuyReq, BuildPayload(builder));
}

void AuctionHandler::SendAuctionCancelRequest(mir2::client::INetworkManager& manager,
                                              uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionCancelReq(builder, listing_id);
  builder.Finish(req);
  manager.send_message(mir2::common::MsgId::kAuctionCancelReq, BuildPayload(builder));
}

AuctionListingData AuctionHandler::ParseListing(const mir2::proto::AuctionListing& listing) {
  AuctionListingData data;
  data.listing_id = listing.listing_id();
  data.seller_character_id = listing.seller_character_id();
  data.item_id = listing.item_id();
  data.count = listing.count();
  data.unit_price = listing.unit_price();
  data.total_price = listing.total_price();
  data.created_at_ms = listing.created_at_ms();
  data.expires_at_ms = listing.expires_at_ms();
  data.sold = listing.sold();
  data.cancelled = listing.cancelled();
  return data;
}

}  // namespace mir2::game::handlers
