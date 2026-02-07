#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <asio/io_context.hpp>

#include "common/enums.h"
#include "gateway/message_router.h"
#include "guild_generated.h"

#define private public
#include "gateway/gateway_server.h"
#include "network/dual_channel_manager.h"
#include "network/kcp_server.h"
#undef private

namespace mir2::gateway {

namespace {

class NullKcpServer : public network::IKcpServer {
 public:
  bool Start(const std::string&, uint16_t) override { return false; }
  void Stop() override {}
  bool IsRunning() const override { return false; }
  uint32_t AllocateConvId() override { return 0; }
  std::shared_ptr<network::KcpSession> CreateSession(
      uint32_t,
      const std::array<uint8_t, network::KcpSession::kTokenSize>&) override {
    return nullptr;
  }
  bool AddSession(const std::shared_ptr<network::KcpSession>&) override { return false; }
  void RemoveSession(uint32_t) override {}
  std::shared_ptr<network::KcpSession> GetSession(uint32_t) const override { return nullptr; }
  void SetMessageHandler(network::KcpSession::MessageHandler handler) override {
    handler_ = std::move(handler);
  }

 private:
  network::KcpSession::MessageHandler handler_;
};

class CountingNetworkManager : public network::INetworkManager {
 public:
  bool Start(const std::string&, uint16_t, int) override { return true; }
  void Stop() override {}

  void RegisterHandler(uint16_t msg_id, network::MessageHandler) override {
    registered_ids_.insert(msg_id);
  }

  void Send(uint64_t, uint16_t, const std::vector<uint8_t>&) override {}
  std::shared_ptr<network::TcpSession> GetSession(uint64_t) const override { return nullptr; }
  std::vector<std::shared_ptr<network::TcpSession>> GetAllSessions() const override {
    return {};
  }
  size_t GetConnectionCount() const override { return 0u; }
  void Tick() override {}

  bool HasHandler(uint16_t msg_id) const {
    return registered_ids_.find(msg_id) != registered_ids_.end();
  }

  size_t HandlerCount() const { return registered_ids_.size(); }

 private:
  std::unordered_set<uint16_t> registered_ids_;
};

template <typename T, typename = void>
struct HasRequiresAuth : std::false_type {};

template <typename T>
struct HasRequiresAuth<T, std::void_t<decltype(&T::RequiresAuth)>> : std::true_type {};

}  // namespace

TEST(GatewayRoutingTest, MessageRouterDeprecated) {
  // MessageRouter 仅保留空壳以兼容旧接口，不再提供RequiresAuth。
  EXPECT_TRUE(std::is_empty_v<MessageRouter>);
  EXPECT_FALSE(std::is_copy_constructible_v<MessageRouter>);
  EXPECT_FALSE((HasRequiresAuth<MessageRouter>::value));
}

TEST(GatewayRoutingTest, UniversalForwardModeRegistersAllMessages) {
  asio::io_context io_context;
  GatewayServer server;

  auto manager = std::make_unique<CountingNetworkManager>();
  auto* manager_ptr = manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(manager), std::move(kcp_server));

  server.RegisterHandlers();

  const std::vector<uint16_t> expected_ids = {
      static_cast<uint16_t>(common::MsgId::kHeartbeat),
      static_cast<uint16_t>(common::MsgId::kLoginReq),
      static_cast<uint16_t>(common::MsgId::kLogout),
      static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
      static_cast<uint16_t>(common::MsgId::kSelectRoleReq),
      static_cast<uint16_t>(common::MsgId::kRoleListReq),
      static_cast<uint16_t>(common::MsgId::kMoveReq),
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      static_cast<uint16_t>(common::MsgId::kSkillReq),
      static_cast<uint16_t>(common::MsgId::kChatReq),
      static_cast<uint16_t>(common::MsgId::kUseItemReq),
      static_cast<uint16_t>(common::MsgId::kDropItemReq),
      static_cast<uint16_t>(common::MsgId::kPickupItemReq),
      static_cast<uint16_t>(common::MsgId::kEquipReq),
      static_cast<uint16_t>(common::MsgId::kUnequipReq),
      static_cast<uint16_t>(common::MsgId::kNpcInteractReq),
      static_cast<uint16_t>(common::MsgId::kNpcMenuSelect),
      static_cast<uint16_t>(common::MsgId::kGuildChat),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK),
  };

  EXPECT_EQ(manager_ptr->HandlerCount(), expected_ids.size());
  for (auto msg_id : expected_ids) {
    EXPECT_TRUE(manager_ptr->HasHandler(msg_id));
  }
}

}  // namespace mir2::gateway
