/**
 * @file mock_response_sender.cc
 * @brief Mock ResponseSender implementation.
 */

#include "logic/mock_response_sender.h"

#include <utility>

#include "network/network_manager.h"

namespace mir2::logic::test {

namespace {

struct StaticNetworkContext {
  asio::io_context io_context;
  mir2::network::NetworkManager network{io_context};
};

StaticNetworkContext& GetStaticNetworkContext() {
  static StaticNetworkContext context;
  return context;
}

}  // namespace

mir2::network::NetworkManager& MockResponseSender::GetNetworkManager() {
  return GetStaticNetworkContext().network;
}

MockResponseSender::MockResponseSender()
    : ResponseSender(GetNetworkManager()) {}

void MockResponseSender::Send(uint64_t client_id,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  captured_responses_.push_back({client_id, msg_id, payload});
}

Task<void> MockResponseSender::SendAsync(uint64_t client_id,
                                         uint16_t msg_id,
                                         std::vector<uint8_t> payload) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    captured_responses_.push_back({client_id, msg_id, std::move(payload)});
  }
  co_return;
}

std::vector<CapturedResponse> MockResponseSender::GetCapturedResponses() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return captured_responses_;
}

void MockResponseSender::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  captured_responses_.clear();
}

size_t MockResponseSender::ResponseCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return captured_responses_.size();
}

CapturedResponse MockResponseSender::GetLastResponse() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (captured_responses_.empty()) {
    return {};
  }
  return captured_responses_.back();
}

}  // namespace mir2::logic::test
