#ifndef MIR2_TESTS_INTEGRATION_MOCK_GAME_SERVER_H_
#define MIR2_TESTS_INTEGRATION_MOCK_GAME_SERVER_H_

#include <asio.hpp>
#include <flatbuffers/flatbuffers.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/enums.h"
#include "common/network/i_channel.h"
#include "common/protocol/message_codec.h"
#include "common/time_utils.h"
#include "network/dual_channel_manager.h"
#include "network/kcp_server.h"
#include "network/network_manager.h"
#include "system_generated.h"

namespace mir2::test::integration {

class MockGameServer {
 public:
  // Configuration for the test server endpoints.
  struct Config {
    std::string bind_ip = "127.0.0.1";
    uint16_t tcp_port = 7000;
    uint16_t udp_port = 7001;
    int max_connections = 128;
    mir2::common::KcpConfig kcp_config{};
    bool enable_tcp_heartbeat = true;
  };

  using MessageHook = std::function<void(mir2::common::ChannelType,
                                         const std::shared_ptr<mir2::network::TcpSession>&,
                                         uint16_t,
                                         const std::vector<uint8_t>&)>;

  // Creates a TCP/KCP-enabled server backed by DualChannelManager.
  explicit MockGameServer(asio::io_context& io_context)
      : MockGameServer(io_context, Config()) {}

  explicit MockGameServer(asio::io_context& io_context, Config config)
      : config_(config),
        io_context_(io_context) {
    auto tcp_manager = std::make_unique<InterceptingNetworkManager>(
        io_context_,
        [this](mir2::common::ChannelType channel,
               const std::shared_ptr<mir2::network::TcpSession>& session,
               uint16_t msg_id,
               const std::vector<uint8_t>& payload) {
          HandleInbound(channel, session, msg_id, payload);
        });
    tcp_manager_ = tcp_manager.get();

    auto kcp_server = std::make_unique<InterceptingKcpServer>(
        io_context_,
        config_.kcp_config,
        [this](mir2::common::ChannelType channel,
               const std::shared_ptr<mir2::network::TcpSession>& session,
               uint16_t msg_id,
               const std::vector<uint8_t>& payload) {
          HandleInbound(channel, session, msg_id, payload);
        });
    kcp_server_ = kcp_server.get();

    manager_ = std::make_unique<mir2::network::DualChannelManager>(
        io_context_, std::move(tcp_manager), std::move(kcp_server));
  }

  ~MockGameServer() {
    Stop();
  }

  bool Start() {
    if (!manager_) {
      return false;
    }

    const bool started = manager_->Start(
        config_.bind_ip,
        config_.tcp_port,
        config_.udp_port,
        config_.max_connections);
    if (started && config_.enable_tcp_heartbeat) {
      RegisterTcpHeartbeatHandler();
    }
    return started;
  }

  void Stop() {
    if (manager_) {
      manager_->Stop();
    }
  }

  void Tick() {
    if (manager_) {
      manager_->Tick();
    }
  }

  void RegisterHandler(uint16_t msg_id, mir2::network::MessageHandler handler) {
    if (manager_) {
      manager_->RegisterHandler(msg_id, std::move(handler));
    }
  }

  void RegisterStaticResponse(uint16_t request_msg_id,
                              uint16_t response_msg_id,
                              std::vector<uint8_t> payload) {
    RegisterHandler(request_msg_id,
                    [response_msg_id, payload = std::move(payload)](
                        const std::shared_ptr<mir2::network::TcpSession>& session,
                        const std::vector<uint8_t>&) {
                      if (!session) {
                        return;
                      }
                      session->Send(response_msg_id, payload);
                    });
  }

  // Enables a basic login success response for tests that need a TCP login step.
  void EnableLoginResponse() {
    EnableLoginResponse(DefaultLoginResponse());
  }

  void EnableLoginResponse(const mir2::common::LoginResponse& response) {
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginResponse(response, &status);
    if (status != mir2::common::MessageCodecStatus::kOk) {
      return;
    }
    RegisterStaticResponse(static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                           static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
                           std::move(payload));
  }

  void SetMessageHook(MessageHook hook) {
    std::lock_guard<std::mutex> lock(hook_mutex_);
    message_hook_ = std::move(hook);
  }

  void SetRoute(uint16_t msg_id, mir2::common::ChannelType channel) {
    if (manager_) {
      manager_->SetRoute(msg_id, channel);
    }
  }

  void SetDefaultChannel(mir2::common::ChannelType channel) {
    if (manager_) {
      manager_->SetDefaultChannel(channel);
    }
  }

  void Send(uint64_t session_id,
            uint16_t msg_id,
            const std::vector<uint8_t>& payload) {
    if (manager_) {
      manager_->Send(session_id, msg_id, payload);
    }
  }

  void Broadcast(uint16_t msg_id, const std::vector<uint8_t>& payload) {
    if (manager_) {
      manager_->Broadcast(msg_id, payload);
    }
  }

  std::shared_ptr<mir2::network::TcpSession> GetSession(uint64_t session_id) const {
    if (!manager_) {
      return nullptr;
    }
    return manager_->GetSession(session_id);
  }

  std::shared_ptr<mir2::network::KcpSession> GetKcpSessionByConv(uint32_t conv_id) const {
    if (!manager_) {
      return nullptr;
    }
    return manager_->GetKcpServer().GetSession(conv_id);
  }

  size_t GetReceivedCount(uint16_t msg_id) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto it = received_counts_.find(msg_id);
    return it == received_counts_.end() ? 0u : it->second;
  }

  size_t GetReceivedCount(mir2::common::ChannelType channel, uint16_t msg_id) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    const auto& map = (channel == mir2::common::ChannelType::kTcp)
                          ? tcp_counts_
                          : kcp_counts_;
    auto it = map.find(msg_id);
    return it == map.end() ? 0u : it->second;
  }

  void StopKcpResponses() {
    if (kcp_server_) {
      kcp_server_->SetDropAll(true);
    }
  }

  void ResumeKcpResponses() {
    if (kcp_server_) {
      kcp_server_->SetDropAll(false);
    }
  }

  void StopHeartbeatResponses() {
    if (kcp_server_) {
      kcp_server_->SetDropHeartbeat(true);
    }
  }

  void ResumeHeartbeatResponses() {
    if (kcp_server_) {
      kcp_server_->SetDropHeartbeat(false);
    }
  }

 private:
  class InterceptingNetworkManager : public mir2::network::INetworkManager {
   public:
    using MessageHook = MockGameServer::MessageHook;

    InterceptingNetworkManager(asio::io_context& io_context, MessageHook hook)
        : manager_(io_context), hook_(std::move(hook)) {
    }

    bool Start(const std::string& bind_ip, uint16_t port, int max_connections) override {
      return manager_.Start(bind_ip, port, max_connections);
    }

    void Stop() override {
      manager_.Stop();
    }

    void RegisterHandler(uint16_t msg_id, mir2::network::MessageHandler handler) override {
      manager_.RegisterHandler(
          msg_id,
          [hook = hook_, msg_id, handler = std::move(handler)](
              const std::shared_ptr<mir2::network::TcpSession>& session,
              const std::vector<uint8_t>& payload) {
            if (hook) {
              hook(mir2::common::ChannelType::kTcp, session, msg_id, payload);
            }
            if (handler) {
              handler(session, payload);
            }
          });
    }

    void Send(uint64_t connection_id,
              uint16_t msg_id,
              const std::vector<uint8_t>& payload) override {
      manager_.Send(connection_id, msg_id, payload);
    }

    std::shared_ptr<mir2::network::TcpSession> GetSession(uint64_t session_id) const override {
      return manager_.GetSession(session_id);
    }

    std::vector<std::shared_ptr<mir2::network::TcpSession>> GetAllSessions() const override {
      return manager_.GetAllSessions();
    }

    size_t GetConnectionCount() const override {
      return manager_.GetConnectionCount();
    }

    void Tick() override {
      manager_.Tick();
    }

   private:
    mir2::network::NetworkManager manager_;
    MessageHook hook_;
  };

  class InterceptingKcpServer : public mir2::network::IKcpServer {
   public:
    using MessageHook = MockGameServer::MessageHook;

    InterceptingKcpServer(asio::io_context& io_context,
                          mir2::common::KcpConfig config,
                          MessageHook hook)
        : server_(io_context, config), hook_(std::move(hook)) {
    }

    bool Start(const std::string& bind_ip, uint16_t port) override {
      return server_.Start(bind_ip, port);
    }

    void Stop() override {
      server_.Stop();
    }

    bool IsRunning() const override {
      return server_.IsRunning();
    }

    uint32_t AllocateConvId() override {
      return server_.AllocateConvId();
    }

    std::shared_ptr<mir2::network::KcpSession> CreateSession(
        uint32_t conv_id,
        const std::array<uint8_t, mir2::network::KcpSession::kTokenSize>& token) override {
      return server_.CreateSession(conv_id, token);
    }

    bool AddSession(const std::shared_ptr<mir2::network::KcpSession>& session) override {
      return server_.AddSession(session);
    }

    void RemoveSession(uint32_t conv_id) override {
      server_.RemoveSession(conv_id);
    }

    std::shared_ptr<mir2::network::KcpSession> GetSession(uint32_t conv_id) const override {
      return server_.GetSession(conv_id);
    }

    void SetMessageHandler(mir2::network::KcpSession::MessageHandler handler) override {
      handler_ = std::move(handler);
      server_.SetMessageHandler(
          [this](const std::shared_ptr<mir2::network::KcpSession>& session,
                 const mir2::network::KcpSession::Packet& packet) {
            if (hook_) {
              hook_(mir2::common::ChannelType::kKcp,
                    session ? session->GetTcpSession() : nullptr,
                    packet.msg_id,
                    packet.payload);
            }
            if (ShouldDrop(packet.msg_id)) {
              return;
            }
            if (handler_) {
              handler_(session, packet);
            }
          });
    }

    void SetDropAll(bool drop) {
      drop_all_.store(drop, std::memory_order_relaxed);
    }

    void SetDropHeartbeat(bool drop) {
      drop_heartbeat_.store(drop, std::memory_order_relaxed);
    }

   private:
    bool ShouldDrop(uint16_t msg_id) const {
      if (drop_all_.load(std::memory_order_relaxed)) {
        return true;
      }
      if (!drop_heartbeat_.load(std::memory_order_relaxed)) {
        return false;
      }
      return msg_id == static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeat);
    }

    mir2::network::KcpServer server_;
    MessageHook hook_;
    mir2::network::KcpSession::MessageHandler handler_;
    std::atomic<bool> drop_all_{false};
    std::atomic<bool> drop_heartbeat_{false};
  };

  void HandleInbound(mir2::common::ChannelType channel,
                     const std::shared_ptr<mir2::network::TcpSession>& session,
                     uint16_t msg_id,
                     const std::vector<uint8_t>& payload) {
    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      received_counts_[msg_id]++;
      if (channel == mir2::common::ChannelType::kTcp) {
        tcp_counts_[msg_id]++;
      } else {
        kcp_counts_[msg_id]++;
      }
    }

    MessageHook hook_copy;
    {
      std::lock_guard<std::mutex> lock(hook_mutex_);
      hook_copy = message_hook_;
    }
    if (hook_copy) {
      hook_copy(channel, session, msg_id, payload);
    }
  }

  void RegisterTcpHeartbeatHandler() {
    const uint16_t heartbeat_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
    const uint16_t heartbeat_rsp_id =
        static_cast<uint16_t>(mir2::common::MsgId::kHeartbeatRsp);

    RegisterHandler(
        heartbeat_id,
        [heartbeat_rsp_id](const std::shared_ptr<mir2::network::TcpSession>& session,
                           const std::vector<uint8_t>& payload) {
          if (!session) {
            return;
          }

          uint32_t seq = 0;
          uint32_t timestamp = static_cast<uint32_t>(mir2::common::now_ms());
          if (!payload.empty()) {
            flatbuffers::Verifier verifier(payload.data(), payload.size());
            if (verifier.VerifyBuffer<mir2::proto::Heartbeat>(nullptr)) {
              const auto* heartbeat = flatbuffers::GetRoot<mir2::proto::Heartbeat>(
                  payload.data());
              if (heartbeat) {
                seq = heartbeat->seq();
                timestamp = heartbeat->client_time();
              }
            }
          }

          flatbuffers::FlatBufferBuilder builder;
          const auto rsp = mir2::proto::CreateHeartbeatRsp(builder, seq, timestamp);
          builder.Finish(rsp);
          const uint8_t* data = builder.GetBufferPointer();
          std::vector<uint8_t> rsp_payload(data, data + builder.GetSize());
          session->Send(heartbeat_rsp_id, rsp_payload);
        });
  }

  static mir2::common::LoginResponse DefaultLoginResponse() {
    mir2::common::LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.account_id = 1;
    response.session_token = "test_session";
    return response;
  }

  Config config_;
  asio::io_context& io_context_;
  std::unique_ptr<mir2::network::DualChannelManager> manager_;
  InterceptingNetworkManager* tcp_manager_ = nullptr;
  InterceptingKcpServer* kcp_server_ = nullptr;

  mutable std::mutex stats_mutex_;
  std::unordered_map<uint16_t, size_t> received_counts_;
  std::unordered_map<uint16_t, size_t> tcp_counts_;
  std::unordered_map<uint16_t, size_t> kcp_counts_;

  mutable std::mutex hook_mutex_;
  MessageHook message_hook_;
};

}  // namespace mir2::test::integration

#endif  // MIR2_TESTS_INTEGRATION_MOCK_GAME_SERVER_H_
