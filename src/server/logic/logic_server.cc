#include "logic/logic_server.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <future>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/message_codec.h"
#include "config/config_manager.h"
#include "core/utils.h"
#include "storage_engine/backends/account_storage_backend.h"
#include "storage_engine/backends/storage_engine_backend.h"
#include "ecs/components/entity_version_component.h"
#include "ecs/character_snapshot_codec.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/effect_system.h"
#include "ecs/systems/inventory_system.h"
#include "ecs/systems/level_up_system.h"
#include "ecs/systems/attribute_recalc_system.h"
#include "ecs/systems/recovery_system.h"
#include "ecs/systems/luck_system.h"
#include "ecs/systems/monster_ai_system.h"
#include "ecs/systems/monster_drop_system.h"
#include "ecs/systems/skill_system.h"
#include "ecs/systems/teleport_system.h"
#include "ecs/systems/trade_system.h"
#include "log/logger.h"
#include "logic/crash_handler.h"
#include "logic/coroutine_executor.h"
#include "logic/events/hot_event_pipeline.h"
#include "logic/handler_registry.h"
#include "logic/handlers/attack_handler.h"
#include "logic/handlers/character/character_handler.h"
#include "logic/handlers/chat/chat_handler.h"
#include "logic/handlers/guild/guild_handler.h"
#include "logic/handlers/item/item_handler.h"
#include "logic/handlers/login/login_handler.h"
#include "logic/handlers/movement/movement_handler.h"
#include "logic/handlers/npc/npc_command_handler.h"
#include "logic/handlers/skill_handler.h"
#include "logic/prewarm_manager.h"
#include "logic/response_sender.h"
#include "logic/services/ecs_combat_service.h"
#include "logic/services/ecs_inventory_service.h"
#include "logic/services/player_presence_service.h"
#include "logic/services/session_role_store.h"
#include "logic/services/storage_login_service.h"
#include "monitor/metrics.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "storage_engine/storage_engine.h"
#include "ecs/systems/guild_system.h"
#include "game/guild/guild_manager.h"
#include "game/map/aoi_manager.h"
#include "guild_generated.h"
#include "chat_generated.h"
#include "game/map/scene_manager.h"
#include "system_generated.h"

namespace mir2::logic {

WorldSystemBundle::WorldSystemBundle() = default;
WorldSystemBundle::~WorldSystemBundle() = default;
WorldSystemBundle::WorldSystemBundle(WorldSystemBundle&&) noexcept = default;
WorldSystemBundle& WorldSystemBundle::operator=(WorldSystemBundle&&) noexcept = default;

namespace {
constexpr uint16_t kMetricsPort = 9091;
constexpr uint32_t kDefaultChatAoiWidth = 1024;
constexpr uint32_t kDefaultChatAoiHeight = 1024;
constexpr const char* kMonsterDropTablePath = "config/tables/monsters.yaml";
constexpr size_t kBackpressureStateMaxEntries = 8192;
constexpr size_t kBackpressurePruneBatchSize = 256;
constexpr size_t kBackpressureStateHardCapEntries = 16384;
constexpr auto kExecutorDrainTimeout = std::chrono::seconds(10);
constexpr const char* kMetricLegacyFallbackTotal =
    "logic.hot_event.legacy_fallback_total";
constexpr const char* kMetricArenaFallbackTotal =
    "logic.hot_event.arena_fallback_total";
constexpr const char* kMetricQueueFullFallbackTotal =
    "logic.hot_event.queue_full_fallback_total";
constexpr const char* kMetricMailboxBatchSize = "logic.mailbox.batch_size";
constexpr const char* kMetricMailboxBatchPlayers = "logic.mailbox.batch_players";
constexpr const char* kMetricMailboxOverflowTotal = "logic.mailbox.overflow_total";
constexpr const char* kMetricMailboxGlobalSoftLimit = "logic.mailbox.global_soft_limit";
constexpr const char* kMetricMailboxGlobalHardLimit = "logic.mailbox.global_hard_limit";
constexpr const char* kMetricMailboxGlobalUtilization =
    "logic.mailbox.global_pending_utilization";
constexpr const char* kMetricMailboxGlobalOverflowTotal =
    "logic.mailbox.global_overflow_total";
constexpr const char* kMetricMailboxSoftBackpressureTotal =
    "logic.mailbox.soft_backpressure_total";
constexpr const char* kMetricMailboxHardBackpressureTotal =
    "logic.mailbox.hard_backpressure_total";
constexpr const char* kMetricBackpressureQueueFullSignalTotal =
    "logic.backpressure.queue_full_signal_total";
constexpr const char* kMetricMailboxSpawnRejectedNotAcceptingTotal =
    "logic.mailbox.spawn_rejected_not_accepting_total";
constexpr const char* kMetricMailboxSpawnRejectedOverLimitTotal =
    "logic.mailbox.spawn_rejected_over_limit_total";
constexpr const char* kMetricMailboxSpawnRejectedInvalidTaskTotal =
    "logic.mailbox.spawn_rejected_invalid_task_total";
constexpr const char* kMetricMailboxDroppedPendingTotal =
    "logic.mailbox.spawn_rejected_dropped_pending_total";
constexpr const char* kMetricTickDurationMs = "logic.tick.duration_ms";
constexpr const char* kMetricTickIntervalConfiguredMs =
    "logic.tick.interval_ms.configured";
constexpr const char* kMetricTickOverrunTotal = "logic.tick.overrun_total";
constexpr const char* kMetricHotDrainBudgetHitTotal =
    "logic.hot_event.drain_budget_hit_total";
constexpr const char* kMetricPrewarmSpawnRejectedNotAcceptingTotal =
    "logic.prewarm.spawn_rejected_not_accepting_total";
constexpr const char* kMetricPrewarmSpawnRejectedOverLimitTotal =
    "logic.prewarm.spawn_rejected_over_limit_total";
constexpr const char* kMetricPrewarmSpawnRejectedInvalidTaskTotal =
    "logic.prewarm.spawn_rejected_invalid_task_total";
constexpr const char* kMetricCoroutineTimeoutBackgroundInflight =
    "logic.coroutine.timeout_background_inflight";
constexpr const char* kMetricCoroutineHungScanTotal =
    "logic.coroutine.hung_scan_total";
constexpr const char* kMetricCoroutineDumpTotal =
    "logic.coroutine.dump_total";
constexpr const char* kMetricCoroutineHungThresholdMs =
    "logic.coroutine.hung_threshold_ms";
constexpr const char* kMetricCoroutineHungScanIntervalMs =
    "logic.coroutine.hung_scan_interval_ms";
constexpr const char* kMetricCoroutineDumpMaxEntries =
    "logic.coroutine.dump_max_entries";

constexpr std::array<uint16_t, 1> kLoginHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kLoginReq)};
constexpr std::array<uint16_t, 1> kMovementHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kMoveReq)};
constexpr std::array<uint16_t, 1> kAttackHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kAttackReq)};
constexpr std::array<uint16_t, 1> kSkillHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kSkillReq)};
constexpr std::array<uint16_t, 4> kCharacterHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kRoleListReq),
    static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
    static_cast<uint16_t>(common::MsgId::kSelectRoleReq),
    static_cast<uint16_t>(common::MsgId::kLogout)};
constexpr std::array<uint16_t, 1> kChatHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kChatReq)};
constexpr std::array<uint16_t, 3> kItemHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kPickupItemReq),
    static_cast<uint16_t>(common::MsgId::kUseItemReq),
    static_cast<uint16_t>(common::MsgId::kDropItemReq)};
constexpr std::array<uint16_t, 5> kGuildHandlerMsgIds = {
    static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR)};
constexpr std::array<uint16_t, 2> kNpcHandlerMsgIds = {
    static_cast<uint16_t>(common::MsgId::kNpcInteractReq),
    static_cast<uint16_t>(common::MsgId::kNpcMenuSelect)};

struct PlaceholderBinding {
  uint16_t msg_id;
  const char* name;
};

constexpr std::array<PlaceholderBinding, 8> kPlaceholderBindings = {{
    {static_cast<uint16_t>(common::MsgId::kEquipReq), "equip_req"},
    {static_cast<uint16_t>(common::MsgId::kUnequipReq), "unequip_req"},
    {static_cast<uint16_t>(common::MsgId::kGuildChat), "guild_chat"},
    {static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK), "guild_kick"},
    {static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY),
     "guild_make_ally"},
    {static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY),
     "guild_break_ally"},
    {static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE),
     "guild_update_notice"},
    {static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK),
     "guild_update_rank"},
}};

bool IsAuthWhitelistedMsgId(uint16_t msg_id) {
  switch (static_cast<common::MsgId>(msg_id)) {
    case common::MsgId::kLoginReq:
    case common::MsgId::kHeartbeat:
    case common::MsgId::kLogout:
      return true;
    default:
      return false;
  }
}

bool IsAuthWhitelistedEvent(const events::HotEvent& event) {
  if (event.type == events::HotEventType::kHeartbeat) {
    return true;
  }
  return IsAuthWhitelistedMsgId(event.msg_id);
}

events::HotEventPriority ClassifyMsgPriority(uint16_t msg_id) {
  switch (static_cast<common::MsgId>(msg_id)) {
    case common::MsgId::kLoginReq:
    case common::MsgId::kLogout:
    case common::MsgId::kCreateRoleReq:
    case common::MsgId::kSelectRoleReq:
    case common::MsgId::kRoleListReq:
    case common::MsgId::kMoveReq:
    case common::MsgId::kAttackReq:
    case common::MsgId::kSkillReq:
    case common::MsgId::kUseItemReq:
    case common::MsgId::kDropItemReq:
    case common::MsgId::kPickupItemReq:
    case common::MsgId::kEquipReq:
    case common::MsgId::kUnequipReq:
    case common::MsgId::kNpcInteractReq:
    case common::MsgId::kNpcMenuSelect:
      return events::HotEventPriority::kCritical;
    case common::MsgId::kHeartbeat:
    case common::MsgId::kChatReq:
      return events::HotEventPriority::kBestEffort;
    default:
      return events::HotEventPriority::kNormal;
  }
}

events::HotEventPriority ResolveEventPriority(const events::HotEvent& event) {
  const events::HotEventPriority hinted = events::GetHotEventPriority(event);
  if (hinted != events::HotEventPriority::kNormal) {
    return hinted;
  }
  return ClassifyMsgPriority(event.msg_id);
}

bool IsBestEffortEvent(const events::HotEvent& event) {
  return ResolveEventPriority(event) == events::HotEventPriority::kBestEffort;
}

bool IsBestEffortMsgId(uint16_t msg_id) {
  return ClassifyMsgPriority(msg_id) == events::HotEventPriority::kBestEffort;
}

bool ShouldFallbackOnQueueFull(uint16_t msg_id) {
  // Best-effort messages are dropped under overload. Other messages fallback
  // to legacy dispatch to avoid losing critical gameplay/account operations.
  return !IsBestEffortMsgId(msg_id);
}

RoleRecord BuildRoleRecordFromSnapshot(const common::CharacterData& data,
                                       uint32_t fallback_player_id) {
  RoleRecord record;
  record.player_id = data.id != 0 ? data.id : fallback_player_id;
  record.name = data.name.empty()
                    ? std::string("Player") + std::to_string(record.player_id)
                    : data.name;
  record.profession = static_cast<uint8_t>(data.char_class);
  record.gender = static_cast<uint8_t>(data.gender);
  record.level = data.stats.level > 0
                     ? static_cast<uint16_t>(std::min(data.stats.level, 65535))
                     : 1;
  record.map_id = data.map_id == 0 ? 1 : data.map_id;
  record.x = data.position.x;
  record.y = data.position.y;
  record.gold = data.stats.gold > 0 ? static_cast<uint64_t>(data.stats.gold) : 0;
  return record;
}

const std::string& SchemaVersionString() {
  static const std::string kSchemaVersion = std::to_string(
      static_cast<uint32_t>(mir2::proto::SchemaVersion::kSchemaVersion));
  return kSchemaVersion;
}

void SendSchemaMismatchKick(const std::shared_ptr<network::TcpSession>& session,
                            uint64_t client_id,
                            const std::string& client_version) {
  if (!session || client_id == 0) {
    return;
  }

  const std::string reason = "Schema version mismatch";
  const std::string detail =
      "client=" + (client_version.empty() ? std::string("<empty>") : client_version) +
      ", expected=" + SchemaVersionString();

  flatbuffers::FlatBufferBuilder builder;
  const auto message_offset = builder.CreateString(detail);
  const auto reason_text_offset = builder.CreateString(reason);
  const auto kick = mir2::proto::CreateKick(
      builder,
      mir2::proto::ErrorCode::ERR_KICK_ADMIN_MANUAL,
      message_offset,
      reason_text_offset);
  builder.Finish(kick);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  const auto routed =
      common::BuildRoutedMessage(client_id,
                                 static_cast<uint16_t>(common::MsgId::kKick),
                                 payload);
  session->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed);
}
}  // namespace

LogicServer::LogicServer() = default;

LogicServer::~LogicServer() {
  Shutdown();
}

bool LogicServer::Initialize(const std::string& config_path) {
  if (!config::ConfigManager::Instance().Load(config_path)) {
    return false;
  }

  const auto& log_config = config::ConfigManager::Instance().GetLogConfig();
  if (!log::Logger::Instance().Initialize(log_config.path, log_config.level,
                                          log_config.max_size_mb, log_config.max_files)) {
    return false;
  }
  CrashHandler::Initialize();

  auto server_config = config::ConfigManager::Instance().GetServerConfig();
  if (server_config.io_threads != 1) {
    SYSLOG_WARN(
        "LogicServer forcing io_threads=1 for ECS/thread-safety (configured={})",
        server_config.io_threads);
    server_config.io_threads = 1;
  }
  if (server_config.tick_interval_ms <= 0) {
    SYSLOG_WARN("LogicServer tick_interval_ms must be > 0, fallback to 50 (configured={})",
                server_config.tick_interval_ms);
    server_config.tick_interval_ms = 50;
  }

  tick_interval_ = std::chrono::milliseconds(server_config.tick_interval_ms);
  hot_event_max_drain_per_tick_ = static_cast<size_t>(std::max(
      server_config.hot_event_max_drain_per_tick, 1));
  hot_event_max_drain_duration_per_tick_ = std::chrono::milliseconds(std::max(
      server_config.hot_event_max_drain_ms_per_tick, 1));

  player_mailbox_max_high_pending_ = static_cast<size_t>(std::max(
      server_config.mailbox_player_max_high_pending, 1));
  player_mailbox_max_low_pending_ = static_cast<size_t>(std::max(
      server_config.mailbox_player_max_low_pending, 0));
  player_mailbox_max_pending_total_ =
      player_mailbox_max_high_pending_ + player_mailbox_max_low_pending_;

  mailbox_high_priority_burst_ = static_cast<uint8_t>(std::clamp(
      server_config.mailbox_high_priority_burst, 1, 255));
  mailbox_overflow_kick_threshold_ = static_cast<uint8_t>(std::clamp(
      server_config.mailbox_overflow_kick_threshold, 1, 255));

  mailbox_global_pending_hard_limit_ = static_cast<size_t>(std::max(
      server_config.mailbox_global_pending_hard_limit, 2));
  const int configured_soft_limit = server_config.mailbox_global_pending_soft_limit;
  if (configured_soft_limit < 0) {
    mailbox_global_pending_soft_limit_ = (mailbox_global_pending_hard_limit_ * 3) / 4;
  } else {
    mailbox_global_pending_soft_limit_ = static_cast<size_t>(configured_soft_limit);
  }
  if (mailbox_global_pending_soft_limit_ == 0 ||
      mailbox_global_pending_soft_limit_ >= mailbox_global_pending_hard_limit_) {
    const size_t adjusted_soft_limit = std::clamp<size_t>(
        mailbox_global_pending_soft_limit_,
        static_cast<size_t>(1),
        mailbox_global_pending_hard_limit_ - 1);
    SYSLOG_WARN(
        "LogicServer mailbox soft limit adjusted "
        "(configured_soft={}, adjusted_soft={}, hard={})",
        mailbox_global_pending_soft_limit_,
        adjusted_soft_limit,
        mailbox_global_pending_hard_limit_);
    mailbox_global_pending_soft_limit_ = adjusted_soft_limit;
  }

  backpressure_pause_ms_ = static_cast<uint32_t>(std::max(
      server_config.backpressure_pause_ms, 0));
  backpressure_signal_cooldown_ms_ = static_cast<int64_t>(std::max(
      server_config.backpressure_signal_cooldown_ms, 0));
  mailbox_soft_backpressure_pause_ms_ = static_cast<uint32_t>(std::max(
      server_config.mailbox_soft_backpressure_pause_ms, 0));
  mailbox_hard_backpressure_pause_ms_ = static_cast<uint32_t>(std::max(
      server_config.mailbox_hard_backpressure_pause_ms, 0));
  mailbox_soft_backpressure_cooldown_ms_ = static_cast<int64_t>(std::max(
      server_config.mailbox_soft_backpressure_cooldown_ms, 0));
  mailbox_hard_backpressure_cooldown_ms_ = static_cast<int64_t>(std::max(
      server_config.mailbox_hard_backpressure_cooldown_ms, 0));
  coroutine_hung_threshold_ = std::chrono::milliseconds(
      std::max(server_config.coroutine_hung_threshold_ms, 1));
  coroutine_hung_scan_interval_ = std::chrono::milliseconds(
      std::max(server_config.coroutine_hung_scan_interval_ms, 1));
  coroutine_dump_max_entries_ = static_cast<size_t>(
      std::clamp(server_config.coroutine_dump_max_entries, 1, 4096));

  if (!app_.Initialize(server_config)) {
    SYSLOG_ERROR("LogicServer application init failed");
    return false;
  }

  if (server_config.metrics_port != 0 && server_config.metrics_port != kMetricsPort) {
    SYSLOG_WARN("LogicServer metrics port overridden to {} (config={})",
                kMetricsPort, server_config.metrics_port);
  }
  monitor::Metrics::Instance().Init(kMetricsPort);
  monitor::Metrics::Instance().SetGauge(monitor::Metrics::kMailboxPendingEvents, 0);
  monitor::Metrics::Instance().SetGauge(monitor::Metrics::kMailboxActiveRunners, 0);
  monitor::Metrics::Instance().SetGauge(
      kMetricMailboxGlobalSoftLimit,
      static_cast<double>(mailbox_global_pending_soft_limit_));
  monitor::Metrics::Instance().SetGauge(
      kMetricMailboxGlobalHardLimit,
      static_cast<double>(mailbox_global_pending_hard_limit_));
  monitor::Metrics::Instance().SetGauge(kMetricMailboxGlobalUtilization, 0);
  monitor::Metrics::Instance().SetGauge(kMetricCoroutineTimeoutBackgroundInflight, 0);
  monitor::Metrics::Instance().SetGauge(
      kMetricCoroutineHungThresholdMs,
      static_cast<double>(coroutine_hung_threshold_.count()));
  monitor::Metrics::Instance().SetGauge(
      kMetricCoroutineHungScanIntervalMs,
      static_cast<double>(coroutine_hung_scan_interval_.count()));
  monitor::Metrics::Instance().SetGauge(
      kMetricCoroutineDumpMaxEntries,
      static_cast<double>(coroutine_dump_max_entries_));
  monitor::Metrics::Instance().SetGauge(
      kMetricTickIntervalConfiguredMs, static_cast<double>(tick_interval_.count()));
  monitor::Metrics::Instance().SetGauge(kMetricTickDurationMs, 0);

  registry_manager_ = &ecs::RegistryManager::Instance();
  if (!scene_manager_) {
    scene_manager_ = std::make_unique<game::map::SceneManager>();
  }

  network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());

  executor_ = std::make_unique<CoroutineExecutor>(app_.GetIoContext());
  SYSLOG_INFO(
      "Coroutine timeout semantics: timeout/cancel resumes awaiting coroutine early; "
      "underlying blocking work is not force-cancelled");
  prewarm_manager_ = std::make_unique<PrewarmManager>(*executor_);
  prewarm_manager_->SetLoader(
      [this](const PrewarmEntry& entry) {
        return RestoreSessionFromPrewarm(entry.client_id, entry.player_id, entry.account_id);
      });
  role_store_ = std::make_unique<RoleStore>();

  io_context_ = &app_.GetIoContext();
  tick_timer_ = std::make_unique<asio::steady_timer>(*io_context_);
  signal_set_ = std::make_unique<asio::signal_set>(*io_context_);
  signal_set_->add(SIGINT);
  signal_set_->add(SIGTERM);
#if defined(SIGUSR1)
  signal_set_->add(SIGUSR1);
#endif
  RegisterSignalHandlers();

  const auto& db_config = config::ConfigManager::Instance().GetDatabaseConfig();
  auto storage_pool = std::make_shared<db::PgConnectionPool>();
  auto kv_backend =
      std::make_unique<db::StorageEngineBackend>(db_config, storage_pool);
  if (!kv_backend->Initialize()) {
    SYSLOG_ERROR("StorageEngine backend init failed");
    return false;
  }
  auto backend =
      std::make_unique<db::AccountStorageBackend>(
          std::move(kv_backend), db_config, storage_pool);
  if (!backend->Initialize()) {
    SYSLOG_ERROR("AccountStorageBackend init failed");
    return false;
  }

  storage_engine::StorageEngine::Config storage_config;
  // TODO: extend ConfigManager to load storage engine config; defaults are used for now.
  if (!storage_engine::StorageEngine::Initialize(std::move(backend),
                                                 storage_config)) {
    SYSLOG_ERROR("StorageEngine init failed");
    return false;
  }

  RegisterHandlers();

  if (auto* storage_login_service =
          dynamic_cast<StorageLoginService*>(login_service_.get())) {
    storage_login_service->SetStorageEngine(&storage_engine::StorageEngine::Instance());
  }

  if (!storage_engine::StorageEngine::Instance().PerformStartupRecovery()) {
    SYSLOG_WARN("StorageEngine startup recovery had errors");
  }

  if (!network_->Start(server_config.bind_ip, server_config.port, server_config.max_connections)) {
    SYSLOG_ERROR("LogicServer network start failed");
    return false;
  }

  SYSLOG_INFO("LogicServer initialized");
  return true;
}

void LogicServer::Run() {
  if (running_.exchange(true)) {
    return;
  }

  shutdown_called_.store(false);
  stopping_.store(false);
  if (!io_context_) {
    SYSLOG_ERROR("LogicServer Run called before Initialize");
    running_.store(false);
    return;
  }

  // Initialize() may run on a bootstrap thread while io_context callbacks run
  // on Application worker threads. Rebind CharacterEntityManager on the actual
  // io thread that executes Tick/handlers.
  if (registry_manager_) {
    std::promise<void> bind_promise;
    auto bind_future = bind_promise.get_future();
    asio::post(*io_context_, [this, promise = std::move(bind_promise)]() mutable {
      logic_thread_id_ = std::this_thread::get_id();
      if (registry_manager_) {
        registry_manager_->GetCharacterManager().BindToCurrentThread();
      }
      promise.set_value();
    });
    bind_future.wait();
  }

  StartTick();
  SYSLOG_INFO("LogicServer running");

  // Application already owns io_context threads. Keep Run() blocked until
  // shutdown without busy polling.
  {
    std::unique_lock<std::mutex> lock(run_state_mutex_);
    run_state_cv_.wait(lock, [this]() {
      return stopping_.load(std::memory_order_acquire);
    });
  }

  Shutdown();
}

void LogicServer::Shutdown() {
  if (shutdown_called_.exchange(true)) {
    run_state_cv_.notify_all();
    return;
  }

  running_.store(false);
  stopping_.store(true);
  run_state_cv_.notify_all();

  if (executor_) {
    executor_->StopAccepting();
  }

  if (tick_timer_) {
    tick_timer_->cancel();
  }

  if (signal_set_) {
    signal_set_->cancel();
  }

  if (network_) {
    network_->Stop();
  }

  if (executor_) {
    const bool drained = executor_->DrainAndJoin(kExecutorDrainTimeout);
    if (!drained) {
      SYSLOG_WARN("LogicServer shutdown: coroutine executor drain timed out");
      DumpActiveCoroutines("shutdown_drain_timeout");
    }
  }

  if (!player_mailboxes_.empty()) {
    for (const auto& [client_id, mailbox] : player_mailboxes_) {
      (void)client_id;
      for (const auto& event : mailbox.high_priority_events) {
        if (hot_event_pipeline_ && event.var_ref.length > 0) {
          hot_event_pipeline_->ReleaseVarPayload(event);
        }
      }
      for (const auto& event : mailbox.low_priority_events) {
        if (hot_event_pipeline_ && event.var_ref.length > 0) {
          hot_event_pipeline_->ReleaseVarPayload(event);
        }
      }
    }
    player_mailboxes_.clear();
    mailbox_pending_events_total_ = 0;
    mailbox_active_runners_ = 0;
    PublishMailboxQueueMetrics();
  }

  // Flush dirty character data before tearing down registries.
  if (registry_manager_) {
    auto& char_mgr = registry_manager_->GetCharacterManager();
    char_mgr.SaveAllDirty();
    SYSLOG_INFO("LogicServer shutdown: flushed dirty characters");
  }

  if (registry_manager_) {
    registry_manager_->ForEachWorld([](uint32_t /*map_id*/, ecs::World& world) {
      world.ClearSystems();
    });
  }

  world_systems_.clear();
  scene_manager_.reset();
  registry_manager_ = nullptr;

  app_.ReleaseWorkGuard();
  app_.Shutdown();
  handler_registry_.reset();
  hot_event_pipeline_.reset();
  attack_handler_.reset();
  skill_handler_.reset();
  login_handler_.reset();
  login_service_.reset();
  movement_handler_.reset();
  character_handler_.reset();
  chat_handler_.reset();
  player_presence_service_.reset();
  item_handler_.reset();
  guild_handler_.reset();
  npc_command_handler_.reset();
  response_sender_.reset();
  chat_aoi_manager_.reset();
  guild_system_ = nullptr;
  prewarm_manager_.reset();
  executor_.reset();
  role_store_.reset();
  if (storage_engine::StorageEngine::IsInitialized()) {
    storage_engine::StorageEngine::Instance().Flush(10000);
    storage_engine::StorageEngine::Shutdown();
  }
  network_.reset();
  tick_timer_.reset();
  signal_set_.reset();
  io_context_ = nullptr;
  logic_thread_id_ = std::thread::id();
  CrashHandler::Shutdown();
}

void LogicServer::StartTick() {
  if (!tick_timer_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  last_tick_time_ = now;
  next_tick_time_ = now + tick_interval_;
  next_coroutine_scan_time_ = now + coroutine_hung_scan_interval_;
  ScheduleNextTick();
}

void LogicServer::ScheduleNextTick() {
  if (!tick_timer_) {
    return;
  }

  tick_timer_->expires_at(next_tick_time_);
  tick_timer_->async_wait([this](const asio::error_code& ec) { OnTick(ec); });
}

void LogicServer::OnTick(const asio::error_code& ec) {
  if (ec || stopping_.load()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const std::chrono::duration<float> delta = now - last_tick_time_;
  last_tick_time_ = now;

  const auto tick_start = std::chrono::steady_clock::now();
  Tick(delta.count());
  const auto tick_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - tick_start);
  const double tick_elapsed_ms = static_cast<double>(tick_elapsed.count()) / 1000.0;
  monitor::Metrics::Instance().SetGauge(kMetricTickDurationMs, tick_elapsed_ms);
  if (tick_elapsed > tick_interval_) {
    monitor::Metrics::Instance().IncrementCounter(kMetricTickOverrunTotal);
  }

  MaybeScanHungCoroutines(now);

  next_tick_time_ += tick_interval_;
  if (next_tick_time_ < now) {
    next_tick_time_ = now + tick_interval_;
  }
  ScheduleNextTick();
}

void LogicServer::MaybeScanHungCoroutines(std::chrono::steady_clock::time_point now) {
  if (!executor_) {
    return;
  }
  if (now < next_coroutine_scan_time_) {
    return;
  }

  next_coroutine_scan_time_ = now + coroutine_hung_scan_interval_;
  monitor::Metrics::Instance().IncrementCounter(kMetricCoroutineHungScanTotal);

  const auto hung = executor_->DetectHungCoroutines(
      coroutine_hung_threshold_, coroutine_dump_max_entries_);
  if (hung.empty()) {
    return;
  }

  SYSLOG_WARN("LogicServer detected {} hung coroutines (threshold={}ms)",
              hung.size(),
              coroutine_hung_threshold_.count());
  for (const auto& snapshot : hung) {
    SYSLOG_WARN(
        "Coroutine hung id={} trace_id={} name={} msg_id={} client_id={} "
        "status={} suspended_for_ms={} age_ms={} state_reason={}",
        snapshot.coroutine_id,
        snapshot.trace_id,
        snapshot.name,
        snapshot.msg_id,
        snapshot.client_id,
        ToString(snapshot.status),
        snapshot.suspended_for_ms,
        snapshot.age_ms,
        snapshot.last_state_reason);
  }

  DumpActiveCoroutines("hung_detected");
}

void LogicServer::DumpActiveCoroutines(const char* reason) const {
  if (!executor_) {
    return;
  }

  const auto snapshots =
      executor_->SnapshotActiveCoroutines(coroutine_dump_max_entries_);
  monitor::Metrics::Instance().IncrementCounter(kMetricCoroutineDumpTotal);

  SYSLOG_WARN("Coroutine dump reason={} active={} showing={}",
              reason == nullptr ? "unknown" : reason,
              executor_->ActiveCoroutineCount(),
              snapshots.size());
  for (const auto& snapshot : snapshots) {
    SYSLOG_WARN(
        "Coroutine dump id={} trace_id={} name={} msg_id={} client_id={} "
        "status={} age_ms={} since_last_state_ms={} suspended_for_ms={} "
        "resume_count={} suspend_count={} state_reason={}",
        snapshot.coroutine_id,
        snapshot.trace_id,
        snapshot.name,
        snapshot.msg_id,
        snapshot.client_id,
        ToString(snapshot.status),
        snapshot.age_ms,
        snapshot.since_last_state_ms,
        snapshot.suspended_for_ms,
        snapshot.resume_count,
        snapshot.suspend_count,
        snapshot.last_state_reason);
  }
}

void LogicServer::RegisterHandlers() {
  if (!hot_event_pipeline_) {
    hot_event_pipeline_ = std::make_unique<events::HotEventPipeline>();
    hot_event_pipeline_->InitializeFromEnv();
  }

  if (executor_ && !handler_registry_) {
    handler_registry_ = std::make_unique<HandlerRegistry>(*executor_);
  }
  if (network_ && !response_sender_) {
    response_sender_ = std::make_unique<ResponseSender>(
        *network_,
        [this](uint64_t client_id, uint16_t msg_id, const std::vector<uint8_t>& payload) {
          const auto routed_payload = common::BuildRoutedMessage(client_id, msg_id, payload);
          if (auto gateway = GetGatewaySession()) {
            gateway->Send(
                static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                routed_payload);
            return;
          }
          network_->Broadcast(
              static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
              routed_payload);
        });
  }
  if (registry_manager_ && !ecs_combat_service_) {
    ecs_combat_service_ = std::make_unique<EcsCombatService>(*registry_manager_);
  }
  if (executor_ && response_sender_ && ecs_combat_service_ && role_store_ && !attack_handler_) {
    attack_handler_ = std::make_unique<AttackHandler>(*executor_,
                                                      *response_sender_,
                                                      *ecs_combat_service_,
                                                      *role_store_);
  }
  if (executor_ && response_sender_ && ecs_combat_service_ && role_store_ && !skill_handler_) {
    skill_handler_ = std::make_unique<SkillHandler>(*executor_,
                                                    *response_sender_,
                                                    *ecs_combat_service_,
                                                    *role_store_);
  }
  if (registry_manager_ && !ecs_inventory_service_) {
    ecs_inventory_service_ = std::make_unique<EcsInventoryService>(*registry_manager_);
  }
  if (executor_ && !login_service_) {
    login_service_ = std::make_unique<StorageLoginService>(*executor_, nullptr);
  }
  if (executor_ && response_sender_ && login_service_ && role_store_ && !login_handler_) {
    login_handler_ = std::make_unique<LoginHandler>(*executor_,
                                                    *response_sender_,
                                                    *login_service_,
                                                    client_registry_,
                                                    *role_store_);
  }
  if (response_sender_ && registry_manager_ && role_store_ && !character_handler_) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    character_handler_ = std::make_unique<CharacterHandler>(*response_sender_,
                                                            character_manager,
                                                            *role_store_,
                                                            client_registry_);
  }
  uint32_t default_map_id = 1;
  const auto& combat_config = config::ConfigManager::Instance().GetCombatConfig();
  if (combat_config.default_respawn_map_id != 0) {
    default_map_id = combat_config.default_respawn_map_id;
  }

  ecs::World* default_world = nullptr;
  entt::registry* default_registry = nullptr;
  ecs::TeleportSystem* teleport_system = nullptr;
  if (registry_manager_) {
    default_world = registry_manager_->CreateWorld(default_map_id);
    if (default_world) {
      default_registry = &default_world->Registry();
      if (!guild_system_) {
        guild_system_ = default_world->CreateSystem<ecs::GuildSystem>(
            default_world->GetEventBus(),
            game::guild::GuildManager::Instance());
      }
      auto& bundle = EnsureWorldSystems(default_map_id, *default_world);
      teleport_system = bundle.teleport_system;
    }
  }

  mir2::game::map::AOIManager* chat_aoi = nullptr;
  if (scene_manager_) {
    if (auto* map = scene_manager_->GetMap(static_cast<int32_t>(default_map_id))) {
      chat_aoi = map->GetAOIManager();
    }
  }
  if (!chat_aoi) {
    if (!chat_aoi_manager_) {
      chat_aoi_manager_ = std::make_unique<game::map::AOIManager>(
          static_cast<int32_t>(kDefaultChatAoiWidth),
          static_cast<int32_t>(kDefaultChatAoiHeight),
          game::map::AOIManager::kDefaultGridSize);
    }
    chat_aoi = chat_aoi_manager_.get();
    SYSLOG_WARN("LogicServer using fallback AOI manager for chat");
  }

  if (executor_ && response_sender_ && ecs_inventory_service_ && !item_handler_) {
    item_handler_ = std::make_unique<ItemHandler>(*executor_,
                                                  *response_sender_,
                                                  *ecs_inventory_service_);
  }

  if (!player_presence_service_) {
    if (!default_registry) {
      SYSLOG_ERROR("LogicServer missing default ECS registry for PlayerPresenceService");
      return;
    }
    player_presence_service_ = PlayerPresenceService::CreateDefault(*default_registry);
  }

  if (executor_ && response_sender_ && default_registry && chat_aoi &&
      player_presence_service_ && !chat_handler_) {
    chat_handler_ = std::make_unique<ChatHandler>(*response_sender_,
                                                  *player_presence_service_,
                                                  *chat_aoi,
                                                  *default_registry);
  }

  if (executor_ && response_sender_ && default_registry && guild_system_ &&
      player_presence_service_ && !guild_handler_) {
    guild_handler_ = std::make_unique<GuildHandler>(*executor_,
                                                    *response_sender_,
                                                    client_registry_,
                                                    *player_presence_service_,
                                                    *guild_system_,
                                                    *default_registry);
  }

  if (executor_ && response_sender_ && default_registry && scene_manager_ &&
      !npc_command_handler_) {
    npc_command_handler_ = std::make_unique<NpcCommandHandler>(*executor_,
                                                               *response_sender_,
                                                               *default_registry,
                                                               *scene_manager_);
  }

  if (response_sender_ && registry_manager_ && scene_manager_ && role_store_ &&
      default_registry && !movement_handler_) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    const auto& server_config = config::ConfigManager::Instance().GetServerConfig();
    MovementHandler::AntiCheatConfig anti_cheat_config{
        std::max(server_config.movement_speed_violation_severity, 0),
        std::max(server_config.movement_teleport_violation_severity, 0)};
    movement_handler_ = std::make_unique<MovementHandler>(*response_sender_,
                                                          client_registry_,
                                                          character_manager,
                                                          *scene_manager_,
                                                          *default_registry,
                                                          default_map_id,
                                                          MovementValidator::Config(),
                                                          teleport_system,
                                                          &gate_manager_,
                                                          role_store_.get(),
                                                          anti_cheat_config);
  }

  if (handler_registry_) {
    auto register_direct_table =
        [this](const auto& msg_ids,
               const char* handler_name,
               HandlerRegistry::HandlerFunc handler) {
          for (const uint16_t msg_id : msg_ids) {
            handler_registry_->RegisterHandler(
                msg_id, handler, handler_name == nullptr ? "" : handler_name);
          }
        };
    auto register_msg_aware_table =
        [this](const auto& msg_ids,
               const char* handler_name,
               std::function<Task<void>(HandlerContext, uint16_t, const uint8_t*, size_t)>
                   handler) {
          for (const uint16_t msg_id : msg_ids) {
            handler_registry_->RegisterHandler(
                msg_id,
                [handler, msg_id](HandlerContext ctx,
                                  const uint8_t* payload,
                                  size_t payload_size) {
                  return handler(std::move(ctx), msg_id, payload, payload_size);
                },
                handler_name == nullptr ? "" : handler_name);
          }
        };

    if (login_handler_) {
      register_direct_table(
          kLoginHandlerMsgIds,
          "login_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return login_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (movement_handler_) {
      register_direct_table(
          kMovementHandlerMsgIds,
          "movement_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return movement_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (attack_handler_) {
      register_direct_table(
          kAttackHandlerMsgIds,
          "attack_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return attack_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (skill_handler_) {
      register_direct_table(
          kSkillHandlerMsgIds,
          "skill_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return skill_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (character_handler_) {
      register_direct_table(
          kCharacterHandlerMsgIds,
          "character_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return character_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (chat_handler_) {
      register_direct_table(
          kChatHandlerMsgIds,
          "chat_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return chat_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (item_handler_) {
      register_direct_table(
          kItemHandlerMsgIds,
          "item_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return item_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (guild_handler_) {
      register_direct_table(
          kGuildHandlerMsgIds,
          "guild_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return guild_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (npc_command_handler_) {
      register_msg_aware_table(
          kNpcHandlerMsgIds,
          "npc_command_handler",
          [this](HandlerContext ctx,
                 uint16_t msg_id,
                 const uint8_t* payload,
                 size_t payload_size) {
            return npc_command_handler_->HandleMessage(
                std::move(ctx), msg_id, payload, payload_size);
          });
    }

    for (const auto& binding : kPlaceholderBindings) {
      handler_registry_->RegisterHandler(
          binding.msg_id,
          [name = binding.name](HandlerContext ctx, const uint8_t*, size_t) -> Task<void> {
            SYSLOG_WARN("LogicServer placeholder handler {} msg_id={} client_id={}",
                        name,
                        ctx.msg_id,
                        ctx.client_id);
            co_return;
          },
          std::string("placeholder_") + binding.name);
    }
  }

  if (!network_) {
    return;
  }

  network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                            [this](const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
                              HandleServiceHello(session, payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kContextRestore),
                            [this](const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
                              HandleContextRestoreResponse(session, payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                            [this](const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
                              HandleRoutedMessage(session, payload);
                            });
}

HandlerContext LogicServer::BuildHandlerContext(uint64_t client_id) {
  HandlerContext context;
  context.client_id = client_id;

  if (!role_store_) {
    return context;
  }

  const auto role_id = role_store_->GetRoleId(client_id);
  if (!role_id.has_value() ||
      *role_id > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return context;
  }
  const uint32_t character_id = static_cast<uint32_t>(*role_id);

  if (registry_manager_) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    const entt::entity entity = character_manager.GetOrCreate(character_id);
    if (entity != entt::null) {
      context.entity = entity;
      if (auto* registry = character_manager.TryGetRegistry(character_id)) {
        context.registry = registry;
        if (const auto* version =
                registry->try_get<ecs::EntityVersionComponent>(entity)) {
          context.entity_version = version->version;
        }
      }

      if (auto map_id = character_manager.TryGetMapId(character_id)) {
        context.map_id = *map_id;
        context.world = registry_manager_->GetWorld(*map_id);
      }
    }
  }

  return context;
}

void LogicServer::DispatchHotEvent(const events::HotEvent& event) {
  const std::thread::id bound_logic_thread = logic_thread_id_;
  if (bound_logic_thread != std::thread::id() &&
      std::this_thread::get_id() != bound_logic_thread) {
    if (io_context_) {
      events::HotEvent event_copy = event;
      asio::post(*io_context_, [this, event = std::move(event_copy)]() mutable {
        DispatchHotEvent(event);
      });
    } else if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    return;
  }

  std::vector<events::HotEvent> one_event;
  one_event.reserve(1);
  one_event.push_back(event);
  DispatchHotEventsBatch(std::move(one_event));
}

void LogicServer::DispatchHotEventsBatch(std::vector<events::HotEvent> events) {
  const std::thread::id bound_logic_thread = logic_thread_id_;
  if (bound_logic_thread != std::thread::id() &&
      std::this_thread::get_id() != bound_logic_thread) {
    if (io_context_) {
      asio::post(*io_context_, [this, events = std::move(events)]() mutable {
        DispatchHotEventsBatch(std::move(events));
      });
    } else {
      for (const auto& event : events) {
        if (hot_event_pipeline_ && event.var_ref.length > 0) {
          hot_event_pipeline_->ReleaseVarPayload(event);
        }
      }
    }
    return;
  }

  if (events.empty()) {
    return;
  }

  monitor::Metrics::Instance().SetGauge(
      kMetricMailboxBatchSize, static_cast<double>(events.size()));

  if (!executor_) {
    for (const auto& event : events) {
      if (hot_event_pipeline_ && event.var_ref.length > 0) {
        hot_event_pipeline_->ReleaseVarPayload(event);
      }
    }
    return;
  }

  auto release_payload = [this](const events::HotEvent& event) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
  };

  std::unordered_set<uint64_t> batch_clients;
  batch_clients.reserve(events.size());
  std::unordered_set<uint64_t> runners_to_start;
  runners_to_start.reserve(events.size());
  std::unordered_set<uint64_t> hard_backpressure_clients;
  hard_backpressure_clients.reserve(events.size());
  std::unordered_set<uint64_t> soft_backpressure_clients;
  soft_backpressure_clients.reserve(events.size());
  std::unordered_set<uint64_t> kick_clients;
  kick_clients.reserve(events.size());

  for (const auto& event : events) {
    if (event.client_id == 0) {
      monitor::Metrics::Instance().IncrementCounter(kMetricMailboxOverflowTotal);
      release_payload(event);
      continue;
    }

    batch_clients.insert(event.client_id);

    auto [it, inserted] = player_mailboxes_.try_emplace(event.client_id);
    auto& mailbox = it->second;
    const bool best_effort = IsBestEffortEvent(event);
    bool accepted = false;
    bool dropped_due_global = false;

    auto drop_low_priority_pending = [&]() -> bool {
      if (mailbox.low_priority_events.empty()) {
        return false;
      }
      const events::HotEvent dropped = mailbox.low_priority_events.front();
      mailbox.low_priority_events.pop_front();
      release_payload(dropped);
      if (mailbox_pending_events_total_ > 0) {
        --mailbox_pending_events_total_;
      }
      monitor::Metrics::Instance().IncrementCounter(kMetricMailboxOverflowTotal);
      return true;
    };

    if (!best_effort) {
      if (mailbox_pending_events_total_ >= mailbox_global_pending_hard_limit_) {
        if (!drop_low_priority_pending()) {
          dropped_due_global = true;
        }
      }
      if (!dropped_due_global &&
          (mailbox.high_priority_events.size() >= player_mailbox_max_high_pending_ ||
           (mailbox.high_priority_events.size() +
            mailbox.low_priority_events.size()) >= player_mailbox_max_pending_total_)) {
        if (!drop_low_priority_pending()) {
          // No low-priority queue to sacrifice; handled as overflow below.
        }
      }

      if (!dropped_due_global &&
          mailbox_pending_events_total_ < mailbox_global_pending_hard_limit_ &&
          mailbox.high_priority_events.size() < player_mailbox_max_high_pending_ &&
          (mailbox.high_priority_events.size() +
           mailbox.low_priority_events.size()) < player_mailbox_max_pending_total_) {
        mailbox.high_priority_events.push_back(event);
        ++mailbox_pending_events_total_;
        mailbox.consecutive_overflow_count = 0;
        accepted = true;
      } else if (!accepted) {
        // Queue still full after low-priority drops, keep as overflow.
      }
    } else {
      if (mailbox_pending_events_total_ >= mailbox_global_pending_hard_limit_) {
        dropped_due_global = true;
      } else if (mailbox.low_priority_events.size() >= player_mailbox_max_low_pending_ ||
                 (mailbox.high_priority_events.size() +
                  mailbox.low_priority_events.size()) >=
                     player_mailbox_max_pending_total_) {
        // Low-priority event is dropped under per-player pressure.
      } else {
        mailbox.low_priority_events.push_back(event);
        ++mailbox_pending_events_total_;
        mailbox.consecutive_overflow_count = 0;
        accepted = true;
      }
    }

    if (!accepted) {
      release_payload(event);
      monitor::Metrics::Instance().IncrementCounter(kMetricMailboxOverflowTotal);

      if (dropped_due_global) {
        monitor::Metrics::Instance().IncrementCounter(kMetricMailboxGlobalOverflowTotal);
        monitor::Metrics::Instance().IncrementCounter(kMetricMailboxHardBackpressureTotal);
        hard_backpressure_clients.insert(event.client_id);
      } else if (best_effort) {
        monitor::Metrics::Instance().IncrementCounter(kMetricMailboxSoftBackpressureTotal);
        soft_backpressure_clients.insert(event.client_id);
      } else {
        monitor::Metrics::Instance().IncrementCounter(kMetricMailboxHardBackpressureTotal);
        hard_backpressure_clients.insert(event.client_id);
        if (mailbox.consecutive_overflow_count <
            std::numeric_limits<uint8_t>::max()) {
          ++mailbox.consecutive_overflow_count;
        }
        if (mailbox.consecutive_overflow_count >=
            mailbox_overflow_kick_threshold_) {
          kick_clients.insert(event.client_id);
        }
      }

      if (inserted && !mailbox.executing && mailbox.high_priority_events.empty() &&
          mailbox.low_priority_events.empty()) {
        player_mailboxes_.erase(it);
      }
      continue;
    }

    if (!mailbox.executing) {
      mailbox.executing = true;
      mailbox.high_priority_budget = mailbox_high_priority_burst_;
      ++mailbox_active_runners_;
      runners_to_start.insert(event.client_id);
    }

    if (mailbox_pending_events_total_ >= mailbox_global_pending_soft_limit_) {
      monitor::Metrics::Instance().IncrementCounter(kMetricMailboxSoftBackpressureTotal);
      soft_backpressure_clients.insert(event.client_id);
    }
  }

  monitor::Metrics::Instance().SetGauge(
      kMetricMailboxBatchPlayers, static_cast<double>(batch_clients.size()));

  PublishMailboxQueueMetrics();

  for (const uint64_t client_id : runners_to_start) {
    executor_->SpawnOrDrop(
        RunPlayerMailbox(client_id),
        [this, client_id](SpawnResult reason) {
          HandleMailboxSpawnRejected(client_id, reason);
        });
  }

  for (const uint64_t client_id : kick_clients) {
    KickMailboxOverflow(client_id);
  }

  for (const uint64_t client_id : hard_backpressure_clients) {
    (void)MaybeSendBackpressurePause(client_id,
                                     mailbox_hard_backpressure_pause_ms_,
                                     mailbox_hard_backpressure_cooldown_ms_);
  }

  for (const uint64_t client_id : soft_backpressure_clients) {
    (void)MaybeSendBackpressurePause(client_id,
                                     mailbox_soft_backpressure_pause_ms_,
                                     mailbox_soft_backpressure_cooldown_ms_);
  }
}

Task<void> LogicServer::RunPlayerMailbox(uint64_t client_id) {
  for (;;) {
    auto it = player_mailboxes_.find(client_id);
    if (it == player_mailboxes_.end()) {
      co_return;
    }
    auto& mailbox = it->second;
    if (mailbox.high_priority_events.empty() &&
        mailbox.low_priority_events.empty()) {
      if (it->second.executing && mailbox_active_runners_ > 0) {
        --mailbox_active_runners_;
      }
      it->second.executing = false;
      player_mailboxes_.erase(it);
      PublishMailboxQueueMetrics();
      co_return;
    }

    events::HotEvent next_event{};
    if (!mailbox.high_priority_events.empty() &&
        (mailbox.low_priority_events.empty() ||
         mailbox.high_priority_budget > 0)) {
      next_event = mailbox.high_priority_events.front();
      mailbox.high_priority_events.pop_front();
      if (!mailbox.low_priority_events.empty() &&
          mailbox.high_priority_budget > 0) {
        --mailbox.high_priority_budget;
      }
    } else if (!mailbox.low_priority_events.empty()) {
      next_event = mailbox.low_priority_events.front();
      mailbox.low_priority_events.pop_front();
      mailbox.high_priority_budget = mailbox_high_priority_burst_;
    } else {
      continue;
    }

    if (mailbox_pending_events_total_ > 0) {
      --mailbox_pending_events_total_;
    }
    PublishMailboxQueueMetrics();
    co_await ExecuteQueuedEvent(next_event);
  }
}

void LogicServer::HandleMailboxSpawnRejected(uint64_t client_id, SpawnResult reason) {
  auto it = player_mailboxes_.find(client_id);
  if (it == player_mailboxes_.end()) {
    return;
  }

  size_t dropped = 0;
  for (const auto& event : it->second.high_priority_events) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    ++dropped;
  }
  for (const auto& event : it->second.low_priority_events) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    ++dropped;
  }
  if (it->second.executing && mailbox_active_runners_ > 0) {
    --mailbox_active_runners_;
  }
  if (dropped >= mailbox_pending_events_total_) {
    mailbox_pending_events_total_ = 0;
  } else {
    mailbox_pending_events_total_ -= dropped;
  }
  player_mailboxes_.erase(it);

  monitor::Metrics::Instance().IncrementCounter(monitor::Metrics::kMailboxSpawnRejectedTotal);
  monitor::Metrics::Instance().IncrementCounter(kMetricMailboxDroppedPendingTotal,
                                                static_cast<uint64_t>(dropped));
  switch (reason) {
    case SpawnResult::kNotAccepting:
      monitor::Metrics::Instance().IncrementCounter(
          kMetricMailboxSpawnRejectedNotAcceptingTotal);
      break;
    case SpawnResult::kOverLimit:
      monitor::Metrics::Instance().IncrementCounter(
          kMetricMailboxSpawnRejectedOverLimitTotal);
      break;
    case SpawnResult::kInvalidTask:
      monitor::Metrics::Instance().IncrementCounter(
          kMetricMailboxSpawnRejectedInvalidTaskTotal);
      break;
    case SpawnResult::kSpawned:
      break;
  }

  PublishMailboxQueueMetrics();
  if (reason == SpawnResult::kOverLimit) {
    KickMailboxOverflow(client_id);
  }

  SYSLOG_WARN(
      "LogicServer mailbox runner rejected client_id={} reason={} dropped_pending={}",
      client_id,
      ToString(reason),
      dropped);
}

void LogicServer::PublishMailboxQueueMetrics() const {
  monitor::Metrics::Instance().SetGauge(
      monitor::Metrics::kMailboxPendingEvents,
      static_cast<double>(mailbox_pending_events_total_));
  monitor::Metrics::Instance().SetGauge(
      monitor::Metrics::kMailboxActiveRunners,
      static_cast<double>(mailbox_active_runners_));
  const double utilization =
      mailbox_global_pending_hard_limit_ == 0
          ? 0.0
          : static_cast<double>(std::min(mailbox_pending_events_total_,
                                         mailbox_global_pending_hard_limit_)) /
                static_cast<double>(mailbox_global_pending_hard_limit_);
  monitor::Metrics::Instance().SetGauge(kMetricMailboxGlobalUtilization, utilization);
}

bool LogicServer::TryReserveBackpressureSignal(uint64_t client_id,
                                               int64_t now_ms,
                                               int64_t cooldown_ms) {
  if (client_id == 0) {
    return false;
  }

  const int64_t effective_cooldown = std::max<int64_t>(0, cooldown_ms);
  bool should_signal = false;
  {
    std::lock_guard<std::mutex> lock(backpressure_mutex_);
    auto& until = backpressure_until_ms_[client_id];
    if (now_ms >= until) {
      until = now_ms + effective_cooldown;
      should_signal = true;
    }
    PruneBackpressureStateLocked(now_ms);
  }
  return should_signal;
}

void LogicServer::PruneBackpressureStateLocked(int64_t now_ms) {
  if (backpressure_until_ms_.size() <= kBackpressureStateMaxEntries) {
    return;
  }

  size_t scanned = 0;
  size_t trimmed = 0;
  for (auto it = backpressure_until_ms_.begin();
       it != backpressure_until_ms_.end() &&
       scanned < kBackpressurePruneBatchSize;) {
    ++scanned;
    if (it->second <= now_ms) {
      it = backpressure_until_ms_.erase(it);
      ++trimmed;
    } else {
      ++it;
    }
  }

  if (backpressure_until_ms_.size() > kBackpressureStateHardCapEntries) {
    size_t forced_trim = 0;
    while (backpressure_until_ms_.size() > kBackpressureStateMaxEntries &&
           forced_trim < kBackpressurePruneBatchSize &&
           !backpressure_until_ms_.empty()) {
      backpressure_until_ms_.erase(backpressure_until_ms_.begin());
      ++forced_trim;
    }
    trimmed += forced_trim;
  }

  if (trimmed > 0) {
    SYSLOG_DEBUG("LogicServer trimmed backpressure state entries={} remaining={}",
                 trimmed,
                 backpressure_until_ms_.size());
  }
}

bool LogicServer::MaybeSendBackpressurePause(uint64_t client_id,
                                             uint32_t duration_ms,
                                             int64_t cooldown_ms) {
  if (client_id == 0 || duration_ms == 0) {
    return false;
  }
  auto session = GetGatewaySession();
  if (!session) {
    return false;
  }
  const int64_t now_ms = network::TcpSession::NowMs();
  if (!TryReserveBackpressureSignal(client_id, now_ms, cooldown_ms)) {
    return false;
  }
  SendBackpressurePause(session, client_id, duration_ms);
  return true;
}

Task<void> LogicServer::ExecuteQueuedEvent(const events::HotEvent& event) {
  if (!IsAuthWhitelistedEvent(event) &&
      !client_registry_.Contains(event.client_id)) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    SYSLOG_WARN("LogicServer dropped unauthenticated event client_id={} msg_id={} type={}",
                event.client_id,
                event.msg_id,
                static_cast<int>(event.type));
    co_return;
  }

  if (event.type == events::HotEventType::kHeartbeat) {
    co_return;
  }

  auto context = BuildHandlerContext(event.client_id);

  switch (event.type) {
    case events::HotEventType::kUnknown:
      co_return;
    case events::HotEventType::kMove:
      if (movement_handler_) {
        co_await movement_handler_->HandleHot(
            std::move(context), event.data.move.target_x, event.data.move.target_y);
      }
      co_return;
    case events::HotEventType::kAttack:
      if (attack_handler_) {
        co_await attack_handler_->HandleHot(
            std::move(context), event.data.attack.target_id, event.data.attack.target_type);
      }
      co_return;
    case events::HotEventType::kSkill:
      if (skill_handler_) {
        co_await skill_handler_->HandleHot(
            std::move(context), event.data.skill.target_id, event.data.skill.skill_id);
      }
      co_return;
    case events::HotEventType::kHeartbeat:
      co_return;
    case events::HotEventType::kChat: {
      if (!chat_handler_ || !hot_event_pipeline_) {
        if (hot_event_pipeline_) {
          hot_event_pipeline_->ReleaseVarPayload(event);
        }
        co_return;
      }

      const uint8_t* raw_payload = nullptr;
      uint32_t raw_size = 0;
      if (!hot_event_pipeline_->TryReadVarPayload(event, &raw_payload, &raw_size)) {
        hot_event_pipeline_->ReleaseVarPayload(event);
        co_return;
      }

      mir2::proto::ChatChannel channel = mir2::proto::ChatChannel::WORLD;
      uint64_t target_id = 0;
      std::string content;
      flatbuffers::Verifier verifier(raw_payload, raw_size);
      if (verifier.VerifyBuffer<mir2::proto::ChatReq>(nullptr)) {
        if (const auto* req = flatbuffers::GetRoot<mir2::proto::ChatReq>(raw_payload)) {
          channel = req->channel();
          target_id = req->target_id();
          if (req->content()) {
            content = req->content()->str();  // deep-copy before any await
          }
        }
      }

      hot_event_pipeline_->ReleaseVarPayload(event);
      co_await chat_handler_->HandleHot(std::move(context), channel, target_id, std::move(content));
      co_return;
    }
    case events::HotEventType::kGeneric: {
      const uint8_t* raw_payload = nullptr;
      uint32_t raw_size = 0;
      if (event.var_ref.length > 0) {
        if (!hot_event_pipeline_ ||
            !hot_event_pipeline_->TryReadVarPayload(event, &raw_payload, &raw_size)) {
          if (hot_event_pipeline_) {
            hot_event_pipeline_->ReleaseVarPayload(event);
          }
          co_return;
        }
      }

      if (!handler_registry_) {
        if (hot_event_pipeline_ && event.var_ref.length > 0) {
          hot_event_pipeline_->ReleaseVarPayload(event);
        }
        co_return;
      }

      Task<void> task(Task<void>::Handle{});
      const bool has_handler =
          handler_registry_->CreateTask(context, event.msg_id, raw_payload, raw_size, &task);
      if (hot_event_pipeline_ && event.var_ref.length > 0) {
        hot_event_pipeline_->ReleaseVarPayload(event);
      }
      if (!has_handler) {
        SYSLOG_WARN("LogicServer no handler for msg_id={}", event.msg_id);
        co_return;
      }

      co_await std::move(task);
      co_return;
    }
  }
}

void LogicServer::KickMailboxOverflow(uint64_t client_id) {
  auto session = GetGatewaySession();
  if (!session || client_id == 0) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto message_offset = builder.CreateString("Mailbox overflow");
  const auto reason_text_offset = builder.CreateString("logic overload");
  const auto kick = mir2::proto::CreateKick(
      builder,
      mir2::proto::ErrorCode::ERR_KICK_ADMIN_MANUAL,
      message_offset,
      reason_text_offset);
  builder.Finish(kick);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  const auto routed =
      common::BuildRoutedMessage(client_id,
                                 static_cast<uint16_t>(common::MsgId::kKick),
                                 payload);
  session->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed);
}

void LogicServer::HandleServiceHello(const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
  if (!session) {
    return;
  }

  common::ServiceType remote = common::ServiceType::kGateway;
  if (!common::ParseServiceHello(payload, &remote)) {
    return;
  }

  session->SetBypassRateLimit(true);
  SYSLOG_INFO("LogicServer handshake from service={}", static_cast<int>(remote));
  const auto ack = common::BuildServiceHelloAck(common::ServiceType::kLogic, true);
  session->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHelloAck), ack);

  session->SetDisconnectedHandler(
      [this](const std::shared_ptr<network::TcpSession>& disconnected) {
        std::lock_guard<std::mutex> lock(gateway_mutex_);
        if (gateway_session_ == disconnected) {
          gateway_session_.reset();
        }
      });

  {
    std::lock_guard<std::mutex> lock(gateway_mutex_);
    gateway_session_ = session;
  }

  SendContextRestore();
}

void LogicServer::HandleRoutedMessage(const std::shared_ptr<network::TcpSession>& session,
                                      const std::vector<uint8_t>& payload) {
  if (!session) {
    return;
  }

  common::RoutedMessageData routed;
  if (!common::ParseRoutedMessage(payload, &routed)) {
    SYSLOG_ERROR("LogicServer failed to parse routed message");
    return;
  }

  if (routed.msg_id == static_cast<uint16_t>(common::MsgId::kLoginReq)) {
    common::LoginRequest login_request;
    const auto status =
        common::DecodeLoginRequest(routed.msg_id, routed.payload, &login_request);
    if (status != common::MessageCodecStatus::kOk) {
      SYSLOG_WARN("LogicServer login request decode failed (client_id={}, status={})",
                  routed.client_id,
                  static_cast<int>(status));
      SendSchemaMismatchKick(session, routed.client_id, "");
      return;
    }

    if (login_request.version != SchemaVersionString()) {
      SYSLOG_WARN("LogicServer schema version mismatch (client_id={}, version={}, expected={})",
                  routed.client_id,
                  login_request.version.empty() ? "<empty>" : login_request.version,
                  SchemaVersionString());
      SendSchemaMismatchKick(session, routed.client_id, login_request.version);
      return;
    }
  }

  if (!hot_event_pipeline_) {
    monitor::Metrics::Instance().IncrementCounter(kMetricLegacyFallbackTotal);
    SYSLOG_WARN("LogicServer hot_event_pipeline unavailable, fallback to legacy dispatch "
                "msg_id={} client_id={}",
                routed.msg_id,
                routed.client_id);
    DispatchRoutedMessageLegacy(routed.client_id, routed.msg_id, routed.payload);
    return;
  }

  HandlerContext context;
  context.client_id = routed.client_id;
  context.msg_id = routed.msg_id;
  const auto enqueue_result = hot_event_pipeline_->TryEnqueue(
      context, routed.msg_id, routed.payload.data(), routed.payload.size());

  if (enqueue_result == events::HotEventPipeline::EnqueueResult::kEnqueued) {
    return;
  }

  if (enqueue_result == events::HotEventPipeline::EnqueueResult::kBypass ||
      enqueue_result == events::HotEventPipeline::EnqueueResult::kArenaExhausted) {
    monitor::Metrics::Instance().IncrementCounter(kMetricLegacyFallbackTotal);
    if (enqueue_result == events::HotEventPipeline::EnqueueResult::kArenaExhausted) {
      monitor::Metrics::Instance().IncrementCounter(kMetricArenaFallbackTotal);
      SYSLOG_WARN("LogicServer hot arena exhausted, fallback to legacy dispatch "
                  "msg_id={} client_id={}",
                  routed.msg_id,
                  routed.client_id);
    }
    if (!DispatchRoutedMessageLegacy(routed.client_id, routed.msg_id, routed.payload)) {
      SYSLOG_WARN("LogicServer legacy fallback failed msg_id={} client_id={}",
                  routed.msg_id,
                  routed.client_id);
    }
    return;
  }

  if (enqueue_result == events::HotEventPipeline::EnqueueResult::kPayloadTooLarge) {
    SYSLOG_WARN("LogicServer dropped oversized payload msg_id={} client_id={} size={}",
                routed.msg_id,
                routed.client_id,
                routed.payload.size());
    return;
  }

  if (enqueue_result == events::HotEventPipeline::EnqueueResult::kQueueFull) {
    if (ShouldFallbackOnQueueFull(routed.msg_id)) {
      monitor::Metrics::Instance().IncrementCounter(kMetricQueueFullFallbackTotal);
      SYSLOG_WARN("LogicServer hot queue full, fallback to legacy dispatch "
                  "msg_id={} client_id={}",
                  routed.msg_id,
                  routed.client_id);
      if (!DispatchRoutedMessageLegacy(routed.client_id, routed.msg_id, routed.payload)) {
        SYSLOG_WARN("LogicServer legacy fallback failed msg_id={} client_id={}",
                    routed.msg_id,
                    routed.client_id);
      }
      return;
    }

    SYSLOG_WARN("LogicServer hot queue full, dropping non-critical msg_id={} client_id={}",
                routed.msg_id,
                routed.client_id);
    if (MaybeSendBackpressurePause(routed.client_id,
                                   backpressure_pause_ms_,
                                   backpressure_signal_cooldown_ms_)) {
      monitor::Metrics::Instance().IncrementCounter(kMetricBackpressureQueueFullSignalTotal);
    }
    return;
  }

  SYSLOG_WARN("LogicServer dropped msg_id={} client_id={} enqueue_result={}",
              routed.msg_id,
              routed.client_id,
              static_cast<int>(enqueue_result));
}

bool LogicServer::DispatchRoutedMessageLegacy(uint64_t client_id,
                                              uint16_t msg_id,
                                              const std::vector<uint8_t>& payload) {
  if (msg_id == static_cast<uint16_t>(common::MsgId::kHeartbeat)) {
    return true;
  }

  if (!IsAuthWhitelistedMsgId(msg_id) && !client_registry_.Contains(client_id)) {
    SYSLOG_WARN("LogicServer dropped unauthenticated routed message client_id={} msg_id={}",
                client_id,
                msg_id);
    return true;
  }

  if (!handler_registry_) {
    SYSLOG_WARN("LogicServer legacy dispatch unavailable msg_id={} client_id={} (registry nil)",
                msg_id,
                client_id);
    return false;
  }

  auto context = BuildHandlerContext(client_id);
  const uint8_t* raw = payload.empty() ? nullptr : payload.data();
  const SpawnResult spawn_result =
      handler_registry_->DispatchMessage(context, msg_id, raw, payload.size());
  if (spawn_result != SpawnResult::kSpawned) {
    SYSLOG_WARN("LogicServer legacy dispatch rejected client_id={} msg_id={} reason={}",
                client_id,
                msg_id,
                ToString(spawn_result));
    return false;
  }
  return true;
}

void LogicServer::SendBackpressurePause(const std::shared_ptr<network::TcpSession>& session,
                                        uint64_t client_id,
                                        uint32_t duration_ms) {
  if (!session || client_id == 0 || duration_ms == 0) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto control = mir2::proto::CreateBackpressureControl(
      builder,
      client_id,
      mir2::proto::BackpressureAction::PAUSE_READ,
      duration_ms);
  builder.Finish(control);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  session->Send(static_cast<uint16_t>(common::InternalMsgId::kBackpressureControl),
                payload);
  monitor::Metrics::Instance().IncrementCounter("logic.backpressure.pause_signal_total");
}

void LogicServer::HandleContextRestoreResponse(
    const std::shared_ptr<network::TcpSession>& session,
    const std::vector<uint8_t>& payload) {
  if (!session || payload.empty()) {
    return;
  }

  flatbuffers::Verifier verifier(payload.data(), payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::ContextRestoreResponse>(nullptr)) {
    SYSLOG_WARN("LogicServer failed to verify ContextRestoreResponse");
    return;
  }

  const auto* response =
      flatbuffers::GetRoot<mir2::proto::ContextRestoreResponse>(payload.data());
  if (!response) {
    return;
  }

  const uint32_t request_id = response->request_id();
  const uint32_t expected = last_context_request_id_.load();
  if (expected != 0 && request_id != expected) {
    SYSLOG_WARN("LogicServer ContextRestoreResponse request_id mismatch (got={}, expected={})",
                request_id, expected);
  }

  std::vector<PrewarmEntry> entries;
  if (const auto* connections = response->connections()) {
    entries.reserve(connections->size());
    for (const auto* ctx : *connections) {
      if (!ctx || ctx->player_id() == 0) {
        continue;
      }
      PrewarmEntry entry;
      entry.client_id = ctx->client_id();
      entry.player_id = ctx->player_id();
      entry.account_id = ctx->account_id();
      if (ctx->ip_address()) {
        entry.ip_address = ctx->ip_address()->str();
      }
      entry.connected_at_ms = ctx->connected_at_ms();
      entry.last_active_ms = ctx->last_active_ms();
      entries.push_back(std::move(entry));
    }
  }

  if (entries.empty()) {
    SendLogicReady(0, 0);
    return;
  }

  if (prewarm_running_.exchange(true)) {
    SYSLOG_WARN("LogicServer prewarm already in progress, skip response {}", request_id);
    return;
  }

  if (!executor_ || !prewarm_manager_) {
    prewarm_running_.store(false);
    SendLogicReady(static_cast<uint32_t>(entries.size()), 0);
    return;
  }

  const uint32_t prewarm_total = static_cast<uint32_t>(entries.size());
  executor_->SpawnOrDrop(
      RunPrewarm(std::move(entries)),
      [this, prewarm_total](SpawnResult reason) {
        prewarm_running_.store(false);
        monitor::Metrics::Instance().IncrementCounter(
            monitor::Metrics::kPrewarmSpawnRejectedTotal);
        switch (reason) {
          case SpawnResult::kNotAccepting:
            monitor::Metrics::Instance().IncrementCounter(
                kMetricPrewarmSpawnRejectedNotAcceptingTotal);
            break;
          case SpawnResult::kOverLimit:
            monitor::Metrics::Instance().IncrementCounter(
                kMetricPrewarmSpawnRejectedOverLimitTotal);
            break;
          case SpawnResult::kInvalidTask:
            monitor::Metrics::Instance().IncrementCounter(
                kMetricPrewarmSpawnRejectedInvalidTaskTotal);
            break;
          case SpawnResult::kSpawned:
            break;
        }

        SYSLOG_WARN("LogicServer prewarm skipped: spawn rejected reason={}",
                    ToString(reason));
        SendLogicReady(0, 0);
        SYSLOG_INFO("LogicServer prewarm reject fallback: report ready total={} restored=0",
                    prewarm_total);
      });
}

Task<void> LogicServer::RunPrewarm(std::vector<PrewarmEntry> entries) {
  PrewarmResult result;
  try {
    if (prewarm_manager_) {
      result = co_await prewarm_manager_->Prewarm(entries);
    }
  } catch (const std::exception& ex) {
    SYSLOG_WARN("LogicServer prewarm failed: {}", ex.what());
  } catch (...) {
    SYSLOG_WARN("LogicServer prewarm failed: unknown error");
  }

  prewarm_running_.store(false);
  SendLogicReady(result.success, result.duration_ms);
  co_return;
}

bool LogicServer::PostToMainThread(std::function<void()> fn) {
  if (!io_context_ || !fn) {
    return false;
  }
  asio::post(*io_context_, std::move(fn));
  return true;
}

bool LogicServer::RestoreSessionFromPrewarm(uint64_t client_id,
                                            uint32_t player_id,
                                            uint32_t account_id) {
  if (client_id == 0 || player_id == 0) {
    return false;
  }

  std::optional<common::CharacterData> snapshot;
  if (storage_engine::StorageEngine::IsInitialized()) {
    try {
      auto& engine = storage_engine::StorageEngine::Instance();
      const std::string key = "char:" + std::to_string(player_id);
      auto stored = engine.Get(key);
      if (!stored) {
        stored = engine.LoadFromDB(key);
      }
      if (stored) {
        snapshot = ecs::DeserializeCharacterSnapshot(
            stored->data.data(),
            stored->data.size());
      }
    } catch (const std::exception& ex) {
      SYSLOG_WARN("LogicServer prewarm load failed (player_id={}): {}",
                  player_id, ex.what());
    } catch (...) {
      SYSLOG_WARN("LogicServer prewarm load failed (player_id={}): unknown error",
                  player_id);
    }
  }

  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  const bool posted = PostToMainThread(
      [this, client_id, player_id, account_id,
       snapshot = std::move(snapshot), promise]() mutable {
        bool ok = false;
        try {
          if (!registry_manager_ || !role_store_) {
            promise->set_value(false);
            return;
          }

          auto& character_manager = registry_manager_->GetCharacterManager();
          if (snapshot) {
            character_manager.Preload(*snapshot);
            ok = true;
          } else {
            ok = character_manager.GetOrCreate(player_id) != entt::null;
          }

          if (!ok) {
            promise->set_value(false);
            return;
          }

          character_manager.OnLogin(player_id);
          role_store_->BindClientRole(client_id, player_id);
          client_registry_.Track(client_id);

          uint64_t resolved_account_id = account_id;
          if (resolved_account_id == 0 && snapshot &&
              !snapshot->account_id.empty()) {
            try {
              resolved_account_id = static_cast<uint64_t>(
                  std::stoull(snapshot->account_id));
            } catch (...) {
              resolved_account_id = 0;
            }
          }

          if (resolved_account_id != 0) {
            role_store_->BindClientAccount(client_id, resolved_account_id);

            RoleRecord role;
            if (snapshot) {
              role = BuildRoleRecordFromSnapshot(*snapshot, player_id);
            } else {
              role.player_id = player_id;
              role.name = std::string("Player") + std::to_string(player_id);
            }
            role_store_->AddRole(resolved_account_id, role);
          }

          promise->set_value(true);
        } catch (const std::exception& ex) {
          SYSLOG_WARN("LogicServer prewarm apply failed (client_id={}, player_id={}): {}",
                      client_id, player_id, ex.what());
          promise->set_value(false);
        } catch (...) {
          SYSLOG_WARN("LogicServer prewarm apply failed (client_id={}, player_id={}): unknown error",
                      client_id, player_id);
          promise->set_value(false);
        }
      });
  if (!posted) {
    return false;
  }

  return future.get();
}

void LogicServer::SendContextRestore() {
  auto session = GetGatewaySession();
  if (!session) {
    SYSLOG_WARN("LogicServer SendContextRestore skipped: gateway session missing");
    return;
  }

  const uint32_t request_id = context_request_id_.fetch_add(1) + 1;
  last_context_request_id_.store(request_id);
  const uint64_t now_ms = static_cast<uint64_t>(network::TcpSession::NowMs());

  flatbuffers::FlatBufferBuilder builder;
  const auto request = mir2::proto::CreateContextRestore(builder, request_id, now_ms);
  builder.Finish(request);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  session->Send(static_cast<uint16_t>(common::InternalMsgId::kContextRestore), payload);
  SYSLOG_INFO("LogicServer sent ContextRestore request_id={} (ts={})", request_id, now_ms);
}

void LogicServer::SendLogicReady(uint32_t prewarm_count, uint64_t duration_ms) {
  auto session = GetGatewaySession();
  if (!session) {
    SYSLOG_WARN("LogicServer SendLogicReady skipped: gateway session missing");
    return;
  }

  const uint64_t now_ms = static_cast<uint64_t>(network::TcpSession::NowMs());
  const uint32_t duration_ms32 =
      duration_ms > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
          ? std::numeric_limits<uint32_t>::max()
          : static_cast<uint32_t>(duration_ms);

  flatbuffers::FlatBufferBuilder builder;
  const auto ready = mir2::proto::CreateLogicReady(
      builder, now_ms, prewarm_count, duration_ms32);
  builder.Finish(ready);
  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  session->Send(static_cast<uint16_t>(common::InternalMsgId::kLogicReady), payload);
  SYSLOG_INFO("LogicServer sent LogicReady prewarm_count={} duration_ms={}",
              prewarm_count, duration_ms32);
}

std::shared_ptr<network::TcpSession> LogicServer::GetGatewaySession() const {
  std::lock_guard<std::mutex> lock(gateway_mutex_);
  return gateway_session_;
}

void LogicServer::RegisterSignalHandlers() {
  if (!signal_set_) {
    return;
  }
  signal_set_->async_wait(
      [this](const asio::error_code& ec, int signal) { OnSignal(ec, signal); });
}

void LogicServer::OnSignal(const asio::error_code& ec, int signal) {
  if (ec) {
    return;
  }

#if defined(SIGUSR1)
  if (signal == SIGUSR1) {
    SYSLOG_INFO("LogicServer received SIGUSR1, dumping active coroutines");
    DumpActiveCoroutines("SIGUSR1");
    RegisterSignalHandlers();
    return;
  }
#endif

  SYSLOG_INFO("LogicServer received signal {}, shutting down...", signal);
  RequestStop();
}

void LogicServer::RequestStop() {
  if (stopping_.exchange(true)) {
    run_state_cv_.notify_all();
    return;
  }
  run_state_cv_.notify_all();

  if (executor_) {
    executor_->StopAccepting();
  }

  if (tick_timer_) {
    tick_timer_->cancel();
  }

  if (signal_set_) {
    signal_set_->cancel();
  }

  if (network_) {
    network_->Stop();
  }
}

WorldSystemBundle& LogicServer::EnsureWorldSystems(uint32_t map_id,
                                                   ecs::World& world) {
  auto& bundle = world_systems_[map_id];
  if (!bundle) {
    bundle = std::make_unique<WorldSystemBundle>();
  }

  if (!bundle->ecs_systems_registered) {
    world.CreateSystem<ecs::CombatSystem>();
    world.CreateSystem<ecs::InventorySystem>();
    world.CreateSystem<ecs::TradeSystem>();
    world.CreateSystem<ecs::LevelUpSystem>(world.Registry(), world.GetEventBus());
    world.CreateSystem<ecs::AttributeRecalcSystem>(world.Registry(), world.GetEventBus());
    world.CreateSystem<ecs::RecoverySystem>();
    world.CreateSystem<ecs::LuckSystem>();
    if (!scene_manager_) {
      scene_manager_ = std::make_unique<game::map::SceneManager>();
    }
    bundle->teleport_system =
        world.CreateSystem<ecs::TeleportSystem>(*scene_manager_, world.GetEventBus());
    bundle->ecs_systems_registered = true;
  }

  if (!bundle->effect_system) {
    bundle->effect_system = std::make_unique<ecs::EffectSystem>(world.Registry());
  }

  if (!bundle->skill_system) {
    auto skill_system = std::make_unique<ecs::SkillSystem>(world.Registry());
    skill_system->set_event_bus(&world.GetEventBus());
    bundle->skill_system = std::move(skill_system);
  }

  if (!bundle->monster_ai_system) {
    bundle->monster_ai_system =
        std::make_unique<ecs::MonsterAISystem>(world.Registry(), world.GetEventBus());
  }

  if (!bundle->monster_drop_system) {
    bundle->monster_drop_system =
        std::make_unique<ecs::MonsterDropSystem>(world.Registry(), world.GetEventBus());
    bundle->monster_drop_system->LoadDropTables(kMonsterDropTablePath);
    bundle->monster_drop_system->SubscribeToDeathEvents();
  }

  return *bundle;
}

void LogicServer::TickWorldSystems(ecs::World& world,
                                   WorldSystemBundle& bundle,
                                   float delta_time,
                                   int64_t now_ms) {
  if (bundle.effect_system) {
    bundle.effect_system->update(now_ms);
  }
  if (bundle.skill_system) {
    bundle.skill_system->update(now_ms);
  }
  if (bundle.monster_ai_system) {
    bundle.monster_ai_system->Update(world.Registry(), delta_time);
  }
}

void LogicServer::Tick(float delta_time) {
  if (hot_event_pipeline_) {
    std::size_t drained = 0;
    std::vector<events::HotEvent> drained_events;
    drained_events.reserve(std::min<size_t>(hot_event_max_drain_per_tick_, 512));
    const auto drain_start = std::chrono::steady_clock::now();
    bool budget_hit = false;
    events::HotEvent event{};
    while (drained < hot_event_max_drain_per_tick_ &&
           hot_event_pipeline_->TryDequeue(&event)) {
      drained_events.push_back(event);
      ++drained;
      if (std::chrono::steady_clock::now() - drain_start >=
          hot_event_max_drain_duration_per_tick_) {
        budget_hit = hot_event_pipeline_->ApproxSize() > 0;
        break;
      }
    }
    if (!budget_hit && drained >= hot_event_max_drain_per_tick_ &&
        hot_event_pipeline_->ApproxSize() > 0) {
      budget_hit = true;
    }
    if (budget_hit) {
      monitor::Metrics::Instance().IncrementCounter(kMetricHotDrainBudgetHitTotal);
    }
    DispatchHotEventsBatch(std::move(drained_events));
    hot_event_pipeline_->ObserveDrain(drained);
  }

  if (registry_manager_) {
    const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
    registry_manager_->ForEachWorld([this, delta_time, now_ms](uint32_t map_id,
                                                              ecs::World& world) {
      auto& bundle = EnsureWorldSystems(map_id, world);
      TickWorldSystems(world, bundle, delta_time, now_ms);
    });
    registry_manager_->UpdateAll(delta_time);
    registry_manager_->GetCharacterManager().Update(delta_time);
  }

  if (network_) {
    network_->Tick();
  }
}

}  // namespace mir2::logic
