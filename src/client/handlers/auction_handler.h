/**
 * @file auction_handler.h
 * @brief Client auction message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_AUCTION_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_AUCTION_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
enum class AuctionNotifyType : uint8_t;
struct AuctionListing;
}

namespace mir2::game::handlers {

struct AuctionListingData {
  uint64_t listing_id = 0;
  uint64_t seller_character_id = 0;
  uint32_t item_id = 0;
  uint32_t count = 0;
  uint32_t unit_price = 0;
  uint32_t total_price = 0;
  uint64_t created_at_ms = 0;
  uint64_t expires_at_ms = 0;
  bool sold = false;
  bool cancelled = false;
};

class AuctionHandler : public std::enable_shared_from_this<AuctionHandler> {
 public:
  using NetworkPacket = mir2::common::NetworkPacket;

  struct Callbacks {
    std::optional<std::weak_ptr<void>> owner;

    std::function<void(bool success,
                       mir2::proto::ErrorCode code,
                       uint32_t total_count,
                       const std::vector<AuctionListingData>& listings)>
        on_list_response;
    std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t listing_id)>
        on_sell_response;
    std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t listing_id)>
        on_buy_response;
    std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t listing_id)>
        on_cancel_response;
    std::function<void(mir2::proto::AuctionNotifyType notify_type,
                       const AuctionListingData& listing)>
        on_notify;
    std::function<void(const std::string& error)> on_parse_error;
  };

  explicit AuctionHandler(Callbacks callbacks);
  ~AuctionHandler() = default;

  AuctionHandler(const AuctionHandler&) = delete;
  AuctionHandler& operator=(const AuctionHandler&) = delete;

  void BindHandlers(mir2::client::INetworkManager& manager);
  static void RegisterHandlers(mir2::client::INetworkManager& manager);

  void HandleAuctionListResponse(const NetworkPacket& packet);
  void HandleAuctionSellResponse(const NetworkPacket& packet);
  void HandleAuctionBuyResponse(const NetworkPacket& packet);
  void HandleAuctionCancelResponse(const NetworkPacket& packet);
  void HandleAuctionNotify(const NetworkPacket& packet);

  static void SendAuctionListRequest(mir2::client::INetworkManager& manager,
                                     uint32_t page,
                                     uint32_t page_size,
                                     bool seller_only);
  static void SendAuctionSellRequest(mir2::client::INetworkManager& manager,
                                     uint16_t inventory_slot,
                                     uint32_t item_id,
                                     uint32_t count,
                                     uint32_t unit_price,
                                     uint32_t duration_sec);
  static void SendAuctionBuyRequest(mir2::client::INetworkManager& manager,
                                    uint64_t listing_id);
  static void SendAuctionCancelRequest(mir2::client::INetworkManager& manager,
                                       uint64_t listing_id);

 private:
  static AuctionListingData ParseListing(const mir2::proto::AuctionListing& listing);

  Callbacks callbacks_;
};

}  // namespace mir2::game::handlers

#endif  // LEGEND2_CLIENT_HANDLERS_AUCTION_HANDLER_H
