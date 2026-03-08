#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "network/handlers/kcp_upgrade_handler.h"
#include "network/dual_channel_manager.h"
#include "network/kcp_server.h"
#include "network/kcp_session.h"
#include "network/tcp_session.h"
#include "system_generated.h"

namespace mir2::network {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::StrictMock;

using KcpToken = std::array<uint8_t, KcpSession::kTokenSize>;

constexpr uint16_t kMsgIdKcpUpgradeResponse =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpUpgradeResponse);
constexpr uint16_t kErrorUnsupported = 1;
constexpr uint16_t kErrorInvalidRequest = 2;
constexpr uint16_t kErrorServerFailed = 3;
constexpr uint16_t kErrorAlreadyBound = 4;

class MockKcpServer : public IKcpServer {
 public:
  MOCK_METHOD(bool, Start, (const std::string& bind_ip, uint16_t port), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
  MOCK_METHOD(uint32_t, AllocateConvId, (), (override));
  MOCK_METHOD(std::shared_ptr<KcpSession>, CreateSession,
              (uint32_t conv_id, const KcpToken& token),
              (override));
  MOCK_METHOD(bool, AddSession, (const std::shared_ptr<KcpSession>& session), (override));
  MOCK_METHOD(void, RemoveSession, (uint32_t conv_id), (override));
  MOCK_METHOD(std::shared_ptr<KcpSession>, GetSession, (uint32_t conv_id), (const, override));
  MOCK_METHOD(void, SetMessageHandler, (KcpSession::MessageHandler handler), (override));
};

class MockDualChannelManager : public IDualChannelManager {
 public:
  MOCK_METHOD(IKcpServer&, GetKcpServer, (), (override));
  MOCK_METHOD(std::shared_ptr<KcpSession>, GetKcpSession, (uint64_t session_id),
              (const, override));
  MOCK_METHOD(bool, BindKcpSession,
              (uint64_t session_id, const std::shared_ptr<KcpSession>& kcp_session),
              (override));
  MOCK_METHOD(bool, TryBindKcpSessionIfAbsent,
              (uint64_t session_id, const std::shared_ptr<KcpSession>& kcp_session),
              (override));
};

class MockTcpSession : public ITcpSession {
 public:
  MOCK_METHOD(void, Send, (uint16_t msg_id, const std::vector<uint8_t>& payload), (override));
  MOCK_METHOD(uint64_t, GetSessionId, (), (const, override));
  MOCK_METHOD(bool, IsKcpUpgradeAllowed, (), (const, override));
};

std::vector<uint8_t> BuildValidRequest(uint16_t client_udp_port) {
  flatbuffers::FlatBufferBuilder builder;
  const auto request = mir2::proto::CreateKcpUpgradeRequest(builder, client_udp_port);
  builder.Finish(request);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

struct ParsedResponse {
  bool success = false;
  uint32_t conv_id = 0;
  uint16_t server_udp_port = 0;
  std::string session_token;
  uint16_t error_code = 0;
};

ParsedResponse ParseResponse(const std::vector<uint8_t>& payload) {
  ParsedResponse parsed;
  flatbuffers::Verifier verifier(payload.data(), payload.size());
  EXPECT_TRUE(verifier.VerifyBuffer<mir2::proto::KcpUpgradeResponse>(nullptr));

  const auto* response =
      flatbuffers::GetRoot<mir2::proto::KcpUpgradeResponse>(payload.data());
  if (!response) {
    return parsed;
  }

  parsed.success = response->success();
  parsed.conv_id = response->conv_id();
  parsed.server_udp_port = response->server_udp_port();
  parsed.error_code = response->error_code();
  const auto* token = response->session_token();
  if (token) {
    parsed.session_token = token->str();
  }
  return parsed;
}

}  // namespace

TEST(KcpUpgradeHandlerTest, KcpNotRunning_ReturnsServerFailed) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 7777);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(false));

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, {});

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorServerFailed);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 7777);
  EXPECT_TRUE(response.session_token.empty());
}

TEST(KcpUpgradeHandlerTest, V1Protocol_Disallowed_ReturnsUnsupported) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9001);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(false));
  EXPECT_CALL(kcp_server, AllocateConvId()).Times(0);
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(_, _)).Times(0);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, {});

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorUnsupported);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 9001);
}

TEST(KcpUpgradeHandlerTest, InvalidPayload_ReturnsInvalidRequest) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9002);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*session, GetSessionId()).WillOnce(Return(5100));
  EXPECT_CALL(manager, GetKcpSession(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(kcp_server, AllocateConvId()).Times(0);
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(_, _)).Times(0);

  std::vector<uint8_t> invalid_payload = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, invalid_payload);

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorInvalidRequest);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 9002);
}

TEST(KcpUpgradeHandlerTest, CreateSessionFailure_ReturnsServerFailed) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9100);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*session, GetSessionId()).WillOnce(Return(5200));
  EXPECT_CALL(manager, GetKcpSession(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(kcp_server, AllocateConvId()).WillOnce(Return(42));
  EXPECT_CALL(kcp_server, CreateSession(42, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(_, _)).Times(0);
  EXPECT_CALL(kcp_server, RemoveSession(_)).Times(0);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(2000));

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorServerFailed);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 9100);
}

TEST(KcpUpgradeHandlerTest, BindFailure_RemovesSessionAndReturnsServerFailed) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9101);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();

  const auto kcp_session = std::make_shared<KcpSession>(
      100, std::array<uint8_t, KcpSession::kTokenSize>{});

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(manager, GetKcpSession(_))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*session, GetSessionId()).WillRepeatedly(Return(5000));
  EXPECT_CALL(kcp_server, AllocateConvId()).WillOnce(Return(100));
  EXPECT_CALL(kcp_server, CreateSession(100, _)).WillOnce(Return(kcp_session));
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(5000, kcp_session))
      .WillOnce(Return(false));
  EXPECT_CALL(kcp_server, RemoveSession(100)).Times(1);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(2001));

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorServerFailed);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 9101);
}

TEST(KcpUpgradeHandlerTest, BindRaceFailure_ReturnsAlreadyBound) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9102);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();
  auto existing_kcp_session = std::make_shared<KcpSession>(
      101, std::array<uint8_t, KcpSession::kTokenSize>{});
  auto created_kcp_session = std::make_shared<KcpSession>(
      102, std::array<uint8_t, KcpSession::kTokenSize>{});

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*session, GetSessionId()).WillRepeatedly(Return(5001));
  EXPECT_CALL(manager, GetKcpSession(_))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(existing_kcp_session));
  EXPECT_CALL(kcp_server, AllocateConvId()).WillOnce(Return(102));
  EXPECT_CALL(kcp_server, CreateSession(102, _)).WillOnce(Return(created_kcp_session));
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(5001, created_kcp_session))
      .WillOnce(Return(false));
  EXPECT_CALL(kcp_server, RemoveSession(102)).Times(1);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(2002));

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorAlreadyBound);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 9102);
}

TEST(KcpUpgradeHandlerTest, V2Protocol_Success_AllocatesConvAndToken) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9200);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();

  std::array<uint8_t, KcpSession::kTokenSize> captured_token{};
  const auto kcp_session = std::make_shared<KcpSession>(
      321, std::array<uint8_t, KcpSession::kTokenSize>{});

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(manager, GetKcpSession(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(*session, GetSessionId()).WillRepeatedly(Return(7000));
  EXPECT_CALL(kcp_server, AllocateConvId()).WillOnce(Return(321));
  EXPECT_CALL(kcp_server, CreateSession(321, _))
      .WillOnce(DoAll(SaveArg<1>(&captured_token), Return(kcp_session)));
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(7000, kcp_session))
      .WillOnce(Return(true));

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(3000));

  const auto response = ParseResponse(payload);
  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.error_code, 0u);
  EXPECT_EQ(response.conv_id, 321u);
  EXPECT_EQ(response.server_udp_port, 9200);

  const std::string expected_token(reinterpret_cast<const char*>(captured_token.data()),
                                   captured_token.size());
  EXPECT_EQ(response.session_token, expected_token);
  EXPECT_EQ(response.session_token.size(), captured_token.size());
}

TEST(KcpUpgradeHandlerTest, RepeatedUpgradeRequest_ReturnsAlreadyBound) {
  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9300);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();
  auto existing_kcp_session = std::make_shared<KcpSession>(
      911, std::array<uint8_t, KcpSession::kTokenSize>{});

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*session, GetSessionId()).WillOnce(Return(8000));
  EXPECT_CALL(manager, GetKcpSession(8000)).WillOnce(Return(existing_kcp_session));
  EXPECT_CALL(kcp_server, AllocateConvId()).Times(0);
  EXPECT_CALL(kcp_server, CreateSession(_, _)).Times(0);
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(_, _)).Times(0);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(3001));

  const auto response = ParseResponse(payload);
  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.error_code, kErrorAlreadyBound);
  EXPECT_EQ(response.conv_id, 0u);
  EXPECT_EQ(response.server_udp_port, 9300);
}

TEST(KcpUpgradeHandlerTest, StaleUnconfirmedBinding_AllowsRebind) {
  using namespace std::chrono_literals;

  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9400);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();
  auto existing_kcp_session = std::make_shared<KcpSession>(
      1001, std::array<uint8_t, KcpSession::kTokenSize>{});
  auto replacement_kcp_session = std::make_shared<KcpSession>(
      1002, std::array<uint8_t, KcpSession::kTokenSize>{});

  std::this_thread::sleep_for(1100ms);

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*session, GetSessionId()).WillRepeatedly(Return(8100));
  EXPECT_CALL(manager, GetKcpSession(8100)).WillOnce(Return(existing_kcp_session));
  EXPECT_CALL(kcp_server, AllocateConvId()).WillOnce(Return(1002));
  EXPECT_CALL(kcp_server, CreateSession(1002, _)).WillOnce(Return(replacement_kcp_session));
  EXPECT_CALL(manager, BindKcpSession(8100, replacement_kcp_session)).WillOnce(Return(true));
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(_, _)).Times(0);
  EXPECT_CALL(kcp_server, RemoveSession(_)).Times(0);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(3002));

  const auto response = ParseResponse(payload);
  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.error_code, 0u);
  EXPECT_EQ(response.conv_id, 1002u);
  EXPECT_EQ(response.server_udp_port, 9400);
}

TEST(KcpUpgradeHandlerTest, StaleConfirmedBinding_AllowsRebind) {
  using namespace std::chrono_literals;

  StrictMock<MockDualChannelManager> manager;
  StrictMock<MockKcpServer> kcp_server;
  KcpUpgradeHandler handler(manager, 9401);
  auto session = std::make_shared<StrictMock<MockTcpSession>>();
  auto existing_kcp_session = std::make_shared<KcpSession>(
      1003, std::array<uint8_t, KcpSession::kTokenSize>{});
  existing_kcp_session->SetRemoteEndpoint(
      asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 9001));
  auto replacement_kcp_session = std::make_shared<KcpSession>(
      1004, std::array<uint8_t, KcpSession::kTokenSize>{});

  std::this_thread::sleep_for(1100ms);

  EXPECT_CALL(manager, GetKcpServer())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(kcp_server));
  EXPECT_CALL(kcp_server, IsRunning()).WillOnce(Return(true));
  EXPECT_CALL(*session, IsKcpUpgradeAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*session, GetSessionId()).WillRepeatedly(Return(8101));
  EXPECT_CALL(manager, GetKcpSession(8101)).WillOnce(Return(existing_kcp_session));
  EXPECT_CALL(kcp_server, AllocateConvId()).WillOnce(Return(1004));
  EXPECT_CALL(kcp_server, CreateSession(1004, _)).WillOnce(Return(replacement_kcp_session));
  EXPECT_CALL(manager, BindKcpSession(8101, replacement_kcp_session)).WillOnce(Return(true));
  EXPECT_CALL(manager, TryBindKcpSessionIfAbsent(_, _)).Times(0);
  EXPECT_CALL(kcp_server, RemoveSession(_)).Times(0);

  std::vector<uint8_t> payload;
  EXPECT_CALL(*session, Send(kMsgIdKcpUpgradeResponse, _))
      .WillOnce(SaveArg<1>(&payload));

  handler.HandleKcpUpgradeRequest(session, BuildValidRequest(3003));

  const auto response = ParseResponse(payload);
  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.error_code, 0u);
  EXPECT_EQ(response.conv_id, 1004u);
  EXPECT_EQ(response.server_udp_port, 9401);
}

}  // namespace mir2::network
