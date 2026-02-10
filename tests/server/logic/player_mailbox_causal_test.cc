#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <flatbuffers/flatbuffers.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define private public
#include "logic/logic_server.h"
#undef private

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/packet_codec.h"
#include "chat_generated.h"
#include "ecs/registry_manager.h"
#include "logic/coroutine_executor.h"
#include "logic/events/hot_event.h"
#include "logic/events/hot_event_pipeline.h"
#include "logic/handler_registry.h"
#include "logic/handlers/skill_handler.h"
#include "logic/mock_response_sender.h"
#include "mocks/mock_socket.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "logic/services/combat_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

using namespace std::chrono_literals;

class ScopedEnv {
 public:
  ScopedEnv(const char* key, const char* value) : key_(key) {
    const char* old = std::getenv(key_);
    if (old) {
      had_old_ = true;
      old_value_ = old;
    }
    if (value) {
      setenv(key_, value, 1);
    } else {
      unsetenv(key_);
    }
  }

  ~ScopedEnv() {
    if (had_old_) {
      setenv(key_, old_value_.c_str(), 1);
    } else {
      unsetenv(key_);
    }
  }

 private:
  const char* key_ = nullptr;
  bool had_old_ = false;
  std::string old_value_;
};

std::shared_ptr<network::TcpSession> MakeTcpSession(asio::io_context& io_context,
                                                     uint64_t session_id) {
  auto socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto connection = std::make_shared<network::TcpConnection>(std::move(socket), session_id);
  return std::make_shared<network::TcpSession>(connection);
}

struct SessionBundle {
  std::shared_ptr<network::TcpSession> session;
  network::MockSocket* socket = nullptr;
};

SessionBundle MakeActiveTcpSession(asio::io_context& io_context, uint64_t session_id) {
  auto socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto* socket_ptr = socket.get();
  auto connection = std::make_shared<network::TcpConnection>(std::move(socket), session_id);
  auto session = std::make_shared<network::TcpSession>(connection);
  session->SetProtocolVersion(common::ProtocolVersion::kV1);
  session->Start();
  return SessionBundle{std::move(session), socket_ptr};
}

struct OrderState {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> events;
  bool equip_finished = false;
  bool skill_called = false;
  bool skill_before_equip_finish = false;
};

class OrderingCombatService final : public CombatService {
 public:
  explicit OrderingCombatService(OrderState& state) : state_(state) {}

  CombatResult Attack(uint64_t, uint64_t) override { return {}; }

  CombatResult UseSkill(uint64_t, uint64_t, uint32_t) override {
    std::lock_guard<std::mutex> lock(state_.mutex);
    state_.events.emplace_back("skill");
    state_.skill_called = true;
    if (!state_.equip_finished) {
      state_.skill_before_equip_finish = true;
    }
    state_.cv.notify_all();
    return {};
  }

 private:
  OrderState& state_;
};

class PlayerMailboxCausalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    work_guard_ = std::make_unique<WorkGuard>(asio::make_work_guard(io_context_));
    io_thread_ = std::thread([this]() { io_context_.run(); });

    response_sender_ = std::make_unique<MockResponseSender>();
    server_ = std::make_unique<LogicServer>();
    server_->executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    server_->handler_registry_ = std::make_unique<HandlerRegistry>(*server_->executor_);
    server_->role_store_ = std::make_unique<RoleStore>();
    server_->logic_thread_id_ = io_thread_.get_id();

    combat_service_ = std::make_unique<OrderingCombatService>(order_state_);
    server_->skill_handler_ = std::make_unique<SkillHandler>(*server_->executor_,
                                                             *response_sender_,
                                                             *combat_service_,
                                                             *server_->role_store_);
  }

  void TearDown() override {
    if (work_guard_) {
      work_guard_->reset();
      work_guard_.reset();
    }
    io_context_.stop();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }

    server_.reset();
    response_sender_.reset();
    combat_service_.reset();
  }

  using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

  bool WaitForCount(const std::atomic<int>& counter,
                    int expected,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline &&
           counter.load(std::memory_order_relaxed) < expected) {
      std::this_thread::sleep_for(5ms);
    }
    return counter.load(std::memory_order_relaxed) >= expected;
  }

  bool WaitForSocketWrites(const network::MockSocket* socket,
                           size_t expected,
                           std::chrono::milliseconds timeout =
                               std::chrono::milliseconds(2000)) {
    if (!socket) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline &&
           socket->GetWrites().size() < expected) {
      std::this_thread::sleep_for(5ms);
    }
    return socket->GetWrites().size() >= expected;
  }

  bool FillHotQueueToFull() {
    if (!server_ || !server_->hot_event_pipeline_) {
      return false;
    }

    HandlerContext context;
    context.client_id = 99999;
    const auto msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
    for (size_t i = 0; i < events::HotEventPipeline::kQueueCapacity + 64; ++i) {
      const auto result = server_->hot_event_pipeline_->TryEnqueue(
          context, msg_id, nullptr, 0);
      if (result == events::HotEventPipeline::EnqueueResult::kQueueFull) {
        return true;
      }
      if (result != events::HotEventPipeline::EnqueueResult::kEnqueued) {
        return false;
      }
    }

    return false;
  }

  void DispatchHotEventOnIo(events::HotEvent event) {
    auto done = std::make_shared<std::promise<void>>();
    auto done_future = done->get_future();
    asio::post(io_context_, [this, event = std::move(event), done]() mutable {
      server_->DispatchHotEvent(event);
      done->set_value();
    });
    done_future.wait();
  }

  asio::io_context io_context_;
  std::unique_ptr<WorkGuard> work_guard_;
  std::thread io_thread_;
  OrderState order_state_;
  std::unique_ptr<OrderingCombatService> combat_service_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<LogicServer> server_;
};

// 同一玩家的 Equip 与 Skill 事件必须串行执行，Skill 不得早于 Equip 完成。
TEST_F(PlayerMailboxCausalTest, EquipThenSkillIsStrictlySequential) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);
  ASSERT_NE(server_->executor_, nullptr);

  const uint64_t client_id = 10001;
  server_->client_registry_.Track(client_id);

  auto* executor = server_->executor_.get();
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kEquipReq),
      [this, executor](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        {
          std::lock_guard<std::mutex> lock(order_state_.mutex);
          order_state_.events.emplace_back("equip_begin");
        }
        co_await executor->Async([]() {
          std::this_thread::sleep_for(120ms);
        });
        {
          std::lock_guard<std::mutex> lock(order_state_.mutex);
          order_state_.equip_finished = true;
          order_state_.events.emplace_back("equip_end");
          order_state_.cv.notify_all();
        }
        co_return;
      });

  events::HotEvent equip_event{};
  equip_event.client_id = client_id;
  equip_event.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kEquipReq);
  equip_event.type = events::HotEventType::kGeneric;

  events::HotEvent skill_event{};
  skill_event.client_id = client_id;
  skill_event.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSkillReq);
  skill_event.type = events::HotEventType::kSkill;
  skill_event.data.skill.target_id = 90001;
  skill_event.data.skill.skill_id = 7;

  DispatchHotEventOnIo(equip_event);
  DispatchHotEventOnIo(skill_event);

  std::unique_lock<std::mutex> lock(order_state_.mutex);
  ASSERT_TRUE(order_state_.cv.wait_for(lock, 5s, [this]() {
    return order_state_.skill_called;
  }));

  ASSERT_GE(order_state_.events.size(), 3u);
  EXPECT_EQ(order_state_.events[0], "equip_begin");
  EXPECT_EQ(order_state_.events[1], "equip_end");
  EXPECT_EQ(order_state_.events[2], "skill");
  EXPECT_FALSE(order_state_.skill_before_equip_finish);
}

// 未鉴权的通用消息应被统一鉴权中间层拦截。
TEST_F(PlayerMailboxCausalTest, UnauthenticatedGenericEventIsDropped) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kEquipReq),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  events::HotEvent event{};
  event.client_id = 20001;
  event.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kEquipReq);
  event.type = events::HotEventType::kGeneric;
  DispatchHotEventOnIo(event);

  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(handled.load(std::memory_order_relaxed), 0);
}

// 白名单消息（登录）在未鉴权时仍应允许进入处理链路。
TEST_F(PlayerMailboxCausalTest, LoginEventBypassesAuthMiddleware) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  events::HotEvent event{};
  event.client_id = 20002;
  event.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
  event.type = events::HotEventType::kGeneric;
  DispatchHotEventOnIo(event);

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline &&
         handled.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(5ms);
  }

  EXPECT_EQ(handled.load(std::memory_order_relaxed), 1);
}

TEST_F(PlayerMailboxCausalTest, LegacyDispatchAllowsWhitelistedLogin) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  EXPECT_TRUE(server_->DispatchRoutedMessageLegacy(
      30001, static_cast<uint16_t>(mir2::common::MsgId::kLoginReq), {}));

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline &&
         handled.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(5ms);
  }

  EXPECT_EQ(handled.load(std::memory_order_relaxed), 1);
}

TEST_F(PlayerMailboxCausalTest, LegacyDispatchDropsUnauthenticatedGenericMessage) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kEquipReq),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  EXPECT_TRUE(server_->DispatchRoutedMessageLegacy(
      30002, static_cast<uint16_t>(mir2::common::MsgId::kEquipReq), {}));
  std::this_thread::sleep_for(200ms);

  EXPECT_EQ(handled.load(std::memory_order_relaxed), 0);
}

TEST_F(PlayerMailboxCausalTest, BuildHandlerContextDoesNotFallbackToClientIdWhenRoleMissing) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->role_store_, nullptr);

  server_->registry_manager_ = &ecs::RegistryManager::Instance();

  constexpr uint64_t kClientId = (1ULL << 32) + 12345ULL;
  struct ProbeResult {
    bool has_entity = false;
    bool has_version = false;
    bool has_cache = false;
    bool fallback_entity_exists = false;
  };

  auto promise = std::make_shared<std::promise<ProbeResult>>();
  auto future = promise->get_future();
  asio::post(io_context_, [this, promise, kClientId]() mutable {
    auto& character_manager =
        server_->registry_manager_->GetCharacterManager();
    character_manager.BindToCurrentThread();
    server_->registry_manager_->CreateWorld(1);

    ProbeResult result;
    const HandlerContext context = server_->BuildHandlerContext(kClientId);
    result.has_entity = (context.entity != entt::null);
    result.has_version = (context.entity_version != 0);
    result.has_cache = context.HasCache();
    const uint32_t truncated_id = static_cast<uint32_t>(kClientId);
    result.fallback_entity_exists = character_manager.TryGet(truncated_id).has_value();
    promise->set_value(result);
  });

  const ProbeResult result = future.get();
  EXPECT_FALSE(result.has_entity);
  EXPECT_FALSE(result.has_version);
  EXPECT_FALSE(result.has_cache);
  EXPECT_FALSE(result.fallback_entity_exists);
}

TEST_F(PlayerMailboxCausalTest, HandleRoutedMessageUsesLegacyFallbackWhenPipelineDisabled) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "0");
  server_->hot_event_pipeline_ = std::make_unique<events::HotEventPipeline>();
  server_->hot_event_pipeline_->InitializeFromEnv();

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kLogout),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  constexpr uint64_t kClientId = 30003;
  const auto routed_payload = mir2::common::BuildRoutedMessage(
      kClientId, static_cast<uint16_t>(mir2::common::MsgId::kLogout), {});
  auto session = MakeTcpSession(io_context_, 9901);
  ASSERT_NE(session, nullptr);

  server_->HandleRoutedMessage(session, routed_payload);

  EXPECT_TRUE(WaitForCount(handled, 1));
}

TEST_F(PlayerMailboxCausalTest, QueueFullCriticalMessageFallsBackToLegacyDispatch) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  server_->hot_event_pipeline_ = std::make_unique<events::HotEventPipeline>();
  server_->hot_event_pipeline_->InitializeFromEnv();
  ASSERT_TRUE(FillHotQueueToFull());

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kEquipReq),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  constexpr uint64_t kClientId = 30004;
  server_->client_registry_.Track(kClientId);
  const auto routed_payload = mir2::common::BuildRoutedMessage(
      kClientId, static_cast<uint16_t>(mir2::common::MsgId::kEquipReq), {});
  auto session = MakeTcpSession(io_context_, 9902);
  ASSERT_NE(session, nullptr);

  server_->HandleRoutedMessage(session, routed_payload);
  EXPECT_TRUE(WaitForCount(handled, 1));
}

TEST_F(PlayerMailboxCausalTest, QueueFullChatMessageStillDropsAsNonCritical) {
  ASSERT_NE(server_, nullptr);
  ASSERT_NE(server_->handler_registry_, nullptr);

  ScopedEnv env("LEGEND2_HOT_EVENT_PIPELINE", "1");
  server_->hot_event_pipeline_ = std::make_unique<events::HotEventPipeline>();
  server_->hot_event_pipeline_->InitializeFromEnv();
  ASSERT_TRUE(FillHotQueueToFull());

  std::atomic<int> handled{0};
  server_->handler_registry_->RegisterHandler(
      static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
      [&handled](HandlerContext, const uint8_t*, size_t) -> Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
      });

  flatbuffers::FlatBufferBuilder builder;
  const auto content = builder.CreateString("queue-full-chat");
  const auto req =
      mir2::proto::CreateChatReq(builder, mir2::proto::ChatChannel::WORLD, content, 0);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> chat_payload(data, data + builder.GetSize());

  constexpr uint64_t kClientId = 30005;
  server_->client_registry_.Track(kClientId);
  const auto routed_payload = mir2::common::BuildRoutedMessage(
      kClientId, static_cast<uint16_t>(mir2::common::MsgId::kChatReq), chat_payload);
  auto session = MakeTcpSession(io_context_, 9903);
  ASSERT_NE(session, nullptr);

  server_->HandleRoutedMessage(session, routed_payload);
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(handled.load(std::memory_order_relaxed), 0);
}

TEST_F(PlayerMailboxCausalTest, ResponseSenderPrefersGatewaySessionWhenAvailable) {
  ASSERT_NE(server_, nullptr);

  server_->network_ = std::make_unique<network::NetworkManager>(io_context_);
  server_->RegisterHandlers();
  ASSERT_NE(server_->response_sender_, nullptr);

  const auto gateway = MakeActiveTcpSession(io_context_, 61001);
  ASSERT_NE(gateway.socket, nullptr);
  {
    std::lock_guard<std::mutex> lock(server_->gateway_mutex_);
    server_->gateway_session_ = gateway.session;
  }

  const uint64_t client_id = 88001;
  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kChatRsp);
  const std::vector<uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  server_->response_sender_->Send(client_id, msg_id, payload);

  ASSERT_TRUE(WaitForSocketWrites(gateway.socket, 1));
  ASSERT_EQ(gateway.socket->GetWrites().size(), 1u);

  common::NetworkPacket packet;
  const auto& wire = gateway.socket->GetWrites().front();
  ASSERT_EQ(common::DecodePacket(wire.data(), wire.size(), &packet), common::DecodeStatus::kOk);
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage));

  common::RoutedMessageData routed;
  ASSERT_TRUE(common::ParseRoutedMessage(packet.payload, &routed));
  EXPECT_EQ(routed.client_id, client_id);
  EXPECT_EQ(routed.msg_id, msg_id);
  EXPECT_EQ(routed.payload, payload);
}

}  // namespace
}  // namespace mir2::logic::test
