/**
 * @file logic_server.h
 * @brief 逻辑服务器
 */

#ifndef MIR2_LOGIC_LOGIC_SERVER_H_
#define MIR2_LOGIC_LOGIC_SERVER_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>

#include "core/application.h"
#include "ecs/id_types.h"
#include "game/map/gate_manager.h"
#include "logic/services/client_registry.h"
#include "logic/task.h"

namespace mir2::network {
class NetworkManager;
class TcpSession;
}  // namespace mir2::network

namespace mir2::ecs {
class RegistryManager;
class World;
class GuildSystem;
class EffectSystem;
class SkillSystem;
class MonsterAISystem;
class MonsterDropSystem;
class TeleportSystem;
}  // namespace mir2::ecs

namespace mir2::storage_engine {
class StorageEngine;
}  // namespace mir2::storage_engine

namespace mir2::db {
class PgConnectionPool;
}  // namespace mir2::db

namespace mir2::game::map {
class AOIManager;
class SceneManager;
class MapContextService;
}  // namespace mir2::game::map

namespace mir2::game::ports {
class IGuildPort;
class IWorldMapPort;
}  // namespace mir2::game::ports

namespace mir2::logic {

namespace events {
class HotEventPipeline;
struct HotEvent;
}  // namespace events

enum class SpawnResult : uint8_t;

class CoroutineExecutor;
class EntityLaneScheduler;
class PrewarmManager;
struct PrewarmEntry;
class HandlerRegistry;
class AttackHandler;
class LoginHandler;
class LoginService;
class MovementHandler;
class SkillHandler;
class ResponseSender;
class CharacterHandler;
class ChatHandler;
class PlayerPresenceService;
class EcsCombatService;
class EcsInventoryService;
class WorldSyncBroadcastService;
class ItemHandler;
class GuildHandler;
class TradeHandler;
class PartyHandler;
class MailHandler;
class RankingHandler;
class AchievementHandler;
class AuctionHandler;
class NpcCommandHandler;
class RoleStore;
class RankingService;
struct HandlerContext;

struct WorldSystemBundle {
 public:
  WorldSystemBundle();
  ~WorldSystemBundle();
  WorldSystemBundle(WorldSystemBundle&&) noexcept;
  WorldSystemBundle& operator=(WorldSystemBundle&&) noexcept;

  WorldSystemBundle(const WorldSystemBundle&) = delete;
  WorldSystemBundle& operator=(const WorldSystemBundle&) = delete;

  bool ecs_systems_registered = false;
  std::unique_ptr<ecs::EffectSystem> effect_system;
  std::unique_ptr<ecs::SkillSystem> skill_system;
  std::unique_ptr<ecs::MonsterAISystem> monster_ai_system;
  std::unique_ptr<ecs::MonsterDropSystem> monster_drop_system;
  std::unique_ptr<WorldSyncBroadcastService> world_sync_broadcast_service;
  ecs::TeleportSystem* teleport_system = nullptr;
};

/**
 * @brief 逻辑服务器
 *
 * 负责逻辑主循环、指标暴露与进程信号处理。
 */
class LogicServer {
 public:
  LogicServer();
  ~LogicServer();

  LogicServer(const LogicServer&) = delete;
  LogicServer& operator=(const LogicServer&) = delete;

  bool Initialize(const std::string& config_path);
  void Run();
  void Shutdown();

 private:
  void StartTick();
  void ScheduleNextTick();
  void OnTick(const asio::error_code& ec);
  void MaybeScanHungCoroutines(std::chrono::steady_clock::time_point now);
  void DumpActiveCoroutines(const char* reason) const;
  bool ReloadStorageRuntimeConfig();
  void RegisterSignalHandlers();
  void OnSignal(const asio::error_code& ec, int signal);
  void RequestStop();
  void Tick(float delta_time);
  uint32_t ResolveDefaultMapId() const;
  bool BootstrapMapRuntime(uint32_t default_map_id);
  bool BindWorldMapContext(uint32_t map_id, ecs::World& world, bool require_map);
  WorldSystemBundle& EnsureWorldSystems(uint32_t map_id, ecs::World& world);
  void TickWorldSystems(ecs::World& world,
                        WorldSystemBundle& bundle,
                        float delta_time,
                        int64_t now_ms);
  void RegisterHandlers();
  bool IsLegacyBypassAllowed(uint16_t msg_id) const;
  HandlerContext BuildHandlerContext(uint64_t client_id);
  void DispatchHotEvent(const events::HotEvent& event);
  void DispatchHotEventsBatch(std::vector<events::HotEvent> events);
  bool DispatchRoutedMessageLegacy(uint64_t client_id,
                                   uint16_t msg_id,
                                   const std::vector<uint8_t>& payload);
  Task<void> RunPlayerMailbox(uint64_t client_id);
  Task<void> ExecuteQueuedEvent(const events::HotEvent& event);
  bool CancelMailbox(uint64_t client_id, const char* reason);
  void HandleMailboxSpawnRejected(uint64_t client_id, SpawnResult reason);
  void PublishMailboxQueueMetrics() const;
  bool TryReserveBackpressureSignal(uint64_t client_id,
                                    int64_t now_ms,
                                    int64_t cooldown_ms);
  void PruneBackpressureStateLocked(int64_t now_ms);
  bool MaybeSendBackpressurePause(uint64_t client_id,
                                  uint32_t duration_ms,
                                  int64_t cooldown_ms);
  void KickMailboxOverflow(uint64_t client_id);
  void KickMailboxException(uint64_t client_id);
  void OnGatewayDisconnected();
  void CleanupAllClientSessions(const char* reason);
  bool CleanupClientSession(uint64_t client_id, const char* reason);
  void ReconcileWithGatewaySnapshot(const std::unordered_set<uint64_t>& restored_client_ids,
                                    uint32_t request_id);
  void MarkClientActivity(uint64_t client_id, int64_t now_ms);
  void ScanZombieSessions(int64_t now_ms);
  void HandleServiceHello(const std::shared_ptr<network::TcpSession>& session,
                          const std::vector<uint8_t>& payload);
  void HandleRoutedMessage(const std::shared_ptr<network::TcpSession>& session,
                           const std::vector<uint8_t>& payload);
  void SendBackpressurePause(const std::shared_ptr<network::TcpSession>& session,
                             uint64_t client_id,
                             uint32_t duration_ms);
  void HandleContextRestoreResponse(const std::shared_ptr<network::TcpSession>& session,
                                    const std::vector<uint8_t>& payload);
  Task<void> RunPrewarm(std::vector<PrewarmEntry> entries);
  bool RestoreSessionFromPrewarm(uint64_t client_id,
                                 ecs::CharacterId player_id,
                                 ecs::AccountId account_id);
  void TriggerImmediateStateSync(uint64_t player_id);
  bool PostToMainThread(std::function<void()> fn);
  void SendContextRestore();
  void SendLogicReady(uint32_t prewarm_count, uint64_t duration_ms);
  std::shared_ptr<network::TcpSession> GetGatewaySession() const;

  core::Application app_;
  asio::io_context* io_context_ = nullptr;
  std::unique_ptr<network::NetworkManager> network_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<EntityLaneScheduler> entity_lane_scheduler_;
  std::unique_ptr<PrewarmManager> prewarm_manager_;
  std::unique_ptr<events::HotEventPipeline> hot_event_pipeline_;
  std::unique_ptr<HandlerRegistry> handler_registry_;
  std::unique_ptr<AttackHandler> attack_handler_;
  std::unique_ptr<SkillHandler> skill_handler_;
  std::unique_ptr<ResponseSender> response_sender_;
  std::unique_ptr<LoginService> login_service_;
  std::unique_ptr<LoginHandler> login_handler_;
  std::unique_ptr<MovementHandler> movement_handler_;
  std::unique_ptr<CharacterHandler> character_handler_;
  std::unique_ptr<ChatHandler> chat_handler_;
  std::unique_ptr<PlayerPresenceService> player_presence_service_;
  std::unique_ptr<EcsCombatService> ecs_combat_service_;
  std::unique_ptr<ItemHandler> item_handler_;
  std::unique_ptr<EcsInventoryService> ecs_inventory_service_;
  std::unique_ptr<GuildHandler> guild_handler_;
  std::unique_ptr<TradeHandler> trade_handler_;
  std::unique_ptr<PartyHandler> party_handler_;
  std::unique_ptr<MailHandler> mail_handler_;
  std::unique_ptr<RankingHandler> ranking_handler_;
  std::unique_ptr<AchievementHandler> achievement_handler_;
  std::unique_ptr<AuctionHandler> auction_handler_;
  std::unique_ptr<NpcCommandHandler> npc_command_handler_;
  std::unique_ptr<RankingService> ranking_service_;
  std::unique_ptr<game::map::AOIManager> chat_aoi_manager_;
  std::shared_ptr<db::PgConnectionPool> db_pool_;
  ecs::GuildSystem* guild_system_ = nullptr;
  std::unique_ptr<game::ports::IGuildPort> guild_port_;
  std::unique_ptr<RoleStore> role_store_;
  ClientRegistry client_registry_;
  game::map::GateManager gate_manager_;
  ecs::RegistryManager* registry_manager_ = nullptr;
  std::unique_ptr<game::map::SceneManager> scene_manager_;
  std::unique_ptr<game::ports::IWorldMapPort> world_map_port_;
  std::unique_ptr<game::map::MapContextService> map_context_service_;
  std::unordered_map<uint32_t, std::unique_ptr<WorldSystemBundle>> world_systems_;
  std::string config_path_;
  std::unique_ptr<asio::steady_timer> tick_timer_;
  std::unique_ptr<asio::signal_set> signal_set_;
  std::chrono::steady_clock::time_point last_tick_time_;
  std::chrono::steady_clock::time_point next_tick_time_;
  std::thread::id logic_thread_id_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> shutdown_called_{false};
  mutable std::mutex run_state_mutex_;
  std::condition_variable run_state_cv_;
  std::atomic<uint32_t> context_request_id_{0};
  std::atomic<uint32_t> last_context_request_id_{0};
  std::atomic<bool> prewarm_running_{false};
  struct PlayerMailbox {
    std::deque<events::HotEvent> high_priority_events;
    std::deque<events::HotEvent> low_priority_events;
    uint8_t high_priority_budget = 0;
    uint8_t consecutive_overflow_count = 0;
    bool executing = false;
  };
  // Accessed only on the logic io_context thread.
  std::unordered_map<uint64_t, PlayerMailbox> player_mailboxes_;
  std::unordered_set<uint64_t> cancelled_mailbox_clients_;
  size_t mailbox_pending_events_total_ = 0;
  size_t mailbox_active_runners_ = 0;
  std::mutex backpressure_mutex_;
  std::unordered_map<uint64_t, int64_t> backpressure_until_ms_;
  std::chrono::milliseconds tick_interval_{50};
  std::chrono::milliseconds coroutine_hung_threshold_{5000};
  std::chrono::milliseconds coroutine_hung_scan_interval_{500};
  size_t coroutine_dump_max_entries_ = 64;
  std::chrono::steady_clock::time_point next_coroutine_scan_time_{};
  size_t hot_event_max_drain_per_tick_ = 2048;
  std::chrono::milliseconds hot_event_max_drain_duration_per_tick_{5};
  bool session_cleanup_on_gateway_disconnect_ = true;
  bool reconcile_cleanup_enabled_ = true;
  bool zombie_detection_enabled_ = true;
  int64_t zombie_scan_interval_ms_ = 5000;
  int64_t zombie_idle_timeout_ms_ = 120000;
  bool legacy_fallback_enabled_ = true;
  bool legacy_fallback_allow_auth_whitelist_ = true;
  bool legacy_fallback_allow_critical_msgs_ = true;
  bool legacy_fallback_allow_normal_msgs_ = false;
  bool queue_full_fallback_non_best_effort_enabled_ = true;
  bool chat_batch_send_enabled_ = true;
  uint32_t backpressure_pause_ms_ = 100;
  int64_t backpressure_signal_cooldown_ms_ = 100;
  uint32_t mailbox_soft_backpressure_pause_ms_ = 100;
  uint32_t mailbox_hard_backpressure_pause_ms_ = 300;
  int64_t mailbox_soft_backpressure_cooldown_ms_ = 100;
  int64_t mailbox_hard_backpressure_cooldown_ms_ = 25;
  size_t player_mailbox_max_high_pending_ = 100;
  size_t player_mailbox_max_low_pending_ = 64;
  size_t player_mailbox_max_pending_total_ = 164;
  uint8_t mailbox_high_priority_burst_ = 4;
  uint8_t mailbox_overflow_kick_threshold_ = 3;
  size_t mailbox_global_pending_hard_limit_ = 20000;
  size_t mailbox_global_pending_soft_limit_ = 15000;
  std::unordered_map<uint64_t, int64_t> client_last_activity_ms_;
  std::unordered_set<uint64_t> last_reconciled_client_ids_;
  int64_t last_zombie_scan_ms_ = 0;
  mutable std::mutex gateway_mutex_;
  std::shared_ptr<network::TcpSession> gateway_session_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_LOGIC_SERVER_H_
