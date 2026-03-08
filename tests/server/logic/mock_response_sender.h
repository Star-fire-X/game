/**
 * @file mock_response_sender.h
 * @brief Mock ResponseSender for coroutine handler tests.
 */

#ifndef MIR2_TESTS_LOGIC_MOCK_RESPONSE_SENDER_H_
#define MIR2_TESTS_LOGIC_MOCK_RESPONSE_SENDER_H_

#include <cstdint>
#include <mutex>
#include <vector>

#include <asio/io_context.hpp>

#include "logic/response_sender.h"
#include "logic/task.h"
#include "network/network_manager.h"

namespace mir2::logic::test {

struct CapturedResponse {
  uint64_t client_id = 0;
  uint16_t msg_id = 0;
  std::vector<uint8_t> payload;
};

class MockResponseSender : public ResponseSender {
 public:
  MockResponseSender();

  void Send(uint64_t client_id,
            uint16_t msg_id,
            const std::vector<uint8_t>& payload) override;

  Task<void> SendAsync(uint64_t client_id,
                       uint16_t msg_id,
                       std::vector<uint8_t> payload) override;

  // 测试辅助方法
  std::vector<CapturedResponse> GetCapturedResponses() const;
  void Clear();
  size_t ResponseCount() const;
  CapturedResponse GetLastResponse() const;

 private:
  static mir2::network::NetworkManager& GetNetworkManager();

  mutable std::mutex mutex_;
  std::vector<CapturedResponse> captured_responses_;
};

}  // namespace mir2::logic::test

#endif  // MIR2_TESTS_LOGIC_MOCK_RESPONSE_SENDER_H_
