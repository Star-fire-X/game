#include "logic/logic_server.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <exception>
#include <filesystem>
#include <future>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <asio/dispatch.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/message_codec.h"
#include "config/config_manager.h"
#include "config/map_config_loader.h"
#include "core/utils.h"
#include "storage_engine/backends/account_storage_backend.h"
#include "storage_engine/backends/storage_engine_backend.h"
#include "ecs/components/entity_version_component.h"
#include "ecs/character_snapshot_codec.h"
#include "ecs/id_types.h"
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
#include "logic/entity_lane_scheduler.h"
#include "logic/events/hot_event_pipeline.h"
#include "logic/handler_msg_id_matrix.h"
#include "logic/handler_registry.h"
#include "logic/handlers/attack_handler.h"
#include "logic/handlers/achievement/achievement_handler.h"
#include "logic/handlers/auction/auction_handler.h"
#include "logic/handlers/character/character_handler.h"
#include "logic/handlers/chat/chat_handler.h"
#include "logic/handlers/guild/guild_handler.h"
#include "logic/handlers/item/item_handler.h"
#include "logic/handlers/login/login_handler.h"
#include "logic/handlers/mail/mail_handler.h"
#include "logic/handlers/movement/movement_handler.h"
#include "logic/handlers/npc/npc_command_handler.h"
#include "logic/handlers/party/party_handler.h"
#include "logic/handlers/ranking/ranking_handler.h"
#include "logic/handlers/skill_handler.h"
#include "logic/handlers/trade/trade_handler.h"
#include "logic/prewarm_manager.h"
#include "logic/response_sender.h"
#include "logic/thread_affinity.h"
#include "logic/services/ecs_combat_service.h"
#include "logic/services/ecs_inventory_service.h"
#include "logic/services/player_presence_service.h"
#include "logic/services/ranking_service.h"
#include "logic/services/session_role_store.h"
#include "logic/services/storage_login_service.h"
#include "logic/services/world_sync_broadcast_service.h"
#include "monitor/metrics.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "storage_engine/storage_engine.h"
#include "ecs/systems/guild_system.h"
#include "game/guild/guild_manager.h"
#include "game/map/aoi_manager.h"
#include "game/map/map_context_service.h"
#include "game/map/map_instance.h"
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
constexpr const char* kMetricMailboxRunnerExceptionTotal =
    "logic.mailbox.runner_exception_total";
constexpr const char* kMetricMailboxCancelTotal = "logic.mailbox.cancel_total";
constexpr const char* kMetricMailboxCancelDroppedPendingTotal =
    "logic.mailbox.cancel_dropped_pending_total";
constexpr const char* kMetricMailboxCancelledIncomingDropTotal =
    "logic.mailbox.cancelled_incoming_drop_total";
constexpr const char* kMetricTickDurationMs = "logic.tick.duration_ms";
constexpr const char* kMetricTickIntervalConfiguredMs =
    "logic.tick.interval_ms.configured";
constexpr const char* kMetricTickOverrunTotal = "logic.tick.overrun_total";
constexpr const char* kMetricTickPhaseHotEventMs = "logic.tick.phase.hot_event_ms";
constexpr const char* kMetricTickPhaseZombieScanMs =
    "logic.tick.phase.zombie_scan_ms";
constexpr const char* kMetricTickPhaseEcsWorldSystemsMs =
    "logic.tick.phase.ecs_world_systems_ms";
constexpr const char* kMetricTickPhaseEcsRegistryUpdateMs =
    "logic.tick.phase.ecs_registry_update_ms";
constexpr const char* kMetricTickPhaseCharacterUpdateMs =
    "logic.tick.phase.character_update_ms";
constexpr const char* kMetricTickPhaseNetworkTickMs =
    "logic.tick.phase.network_tick_ms";
constexpr const char* kMetricTickPhaseHungScanMs = "logic.tick.phase.hung_scan_ms";
constexpr const char* kMetricHotDrainBudgetHitTotal =
    "logic.hot_event.drain_budget_hit_total";
constexpr const char* kMetricPrewarmSpawnRejectedNotAcceptingTotal =
    "logic.prewarm.spawn_rejected_not_accepting_total";
constexpr const char* kMetricPrewarmSpawnRejectedOverLimitTotal =
    "logic.prewarm.spawn_rejected_over_limit_total";
constexpr const char* kMetricPrewarmSpawnRejectedInvalidTaskTotal =
    "logic.prewarm.spawn_rejected_invalid_task_total";
constexpr const char* kMetricPrewarmLegacySnapshotMigratedTotal =
    "logic.prewarm.snapshot_legacy_migrated_total";
constexpr const char* kMetricPrewarmSnapshotAccountBackfillTotal =
    "logic.prewarm.snapshot_account_backfill_total";
constexpr const char* kMetricPrewarmSnapshotRewriteFailedTotal =
    "logic.prewarm.snapshot_rewrite_failed_total";
constexpr const char* kMetricPrewarmSnapshotDecodeFailedTotal =
    "logic.prewarm.snapshot_decode_failed_total";
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
constexpr const char* kMetricLegacyDispatchBlockedTotal =
    "logic.legacy_dispatch_blocked_total";
constexpr const char* kMetricEntityLaneActive = "logic.entity_lane.active";
constexpr const char* kMetricEntityLanePending = "logic.entity_lane.pending";
constexpr const char* kMetricEntityVersionMismatchTotal =
    "logic.entity_version_mismatch_total";
constexpr const char* kMetricSessionCleanupAllTotal =
    "logic.session.cleanup_all_total";
constexpr const char* kMetricSessionCleanupReconcileTotal =
    "logic.session.cleanup_reconcile_total";
constexpr const char* kMetricSessionCleanupZombieTotal =
    "logic.session.cleanup_zombie_total";
constexpr const char* kMetricSessionReconcileLocalOnlyTotal =
    "logic.session.reconcile.local_only_total";
constexpr const char* kMetricSessionReconcileSnapshotSize =
    "logic.session.reconcile.snapshot_size";
constexpr const char* kMetricSessionZombieScanTotal =
    "logic.session.zombie_scan_total";

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

double DurationMs(std::chrono::steady_clock::duration duration) {
  return static_cast<double>(
             std::chrono::duration_cast<std::chrono::microseconds>(duration).count()) /
         1000.0;
}

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
    case common::MsgId::kGuildCreateReq:
    case common::MsgId::kGuildJoinReq:
    case common::MsgId::kGuildLeaveReq:
    case common::MsgId::kGuildKickReq:
    case common::MsgId::kGuildDeclareWarReq:
    case common::MsgId::kGuildCancelWarReq:
    case common::MsgId::kGuildMakeAllyReq:
    case common::MsgId::kGuildBreakAllyReq:
    case common::MsgId::kGuildUpdateNoticeReq:
    case common::MsgId::kGuildUpdateRankReq:
    case common::MsgId::kTradeReq:
    case common::MsgId::kTradeAddItemReq:
    case common::MsgId::kTradeSetGoldReq:
    case common::MsgId::kTradeConfirmReq:
    case common::MsgId::kTradeCancelReq:
    case common::MsgId::kPartyInviteReq:
    case common::MsgId::kPartyJoinReq:
    case common::MsgId::kPartyLeaveReq:
    case common::MsgId::kPartyKickReq:
    case common::MsgId::kAuctionListReq:
    case common::MsgId::kAuctionSellReq:
    case common::MsgId::kAuctionBuyReq:
    case common::MsgId::kAuctionCancelReq:
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

uint64_t BuildLaneKey(const HandlerContext& context, uint64_t client_id) {
  constexpr uint64_t kEntityTag = 1ULL << 63;
  if (context.entity != entt::null) {
    return kEntityTag | static_cast<uint64_t>(entt::to_integral(context.entity));
  }
  return client_id;
}

const char* ToString(events::HotEventType type) noexcept {
  switch (type) {
    case events::HotEventType::kUnknown:
      return "unknown";
    case events::HotEventType::kMove:
      return "move";
    case events::HotEventType::kAttack:
      return "attack";
    case events::HotEventType::kSkill:
      return "skill";
    case events::HotEventType::kHeartbeat:
      return "heartbeat";
    case events::HotEventType::kChat:
      return "chat";
    case events::HotEventType::kGeneric:
      return "generic";
  }
  return "invalid";
}

std::vector<uint8_t> BuildKickPayload(mir2::proto::ErrorCode error_code,
                                      const std::string& message,
                                      const std::string& reason_text) {
  flatbuffers::FlatBufferBuilder builder;
  const auto message_offset = builder.CreateString(message);
  const auto reason_text_offset = builder.CreateString(reason_text);
  const auto kick = mir2::proto::CreateKick(
      builder, error_code, message_offset, reason_text_offset);
  builder.Finish(kick);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

void SendKickPayload(const std::shared_ptr<network::TcpSession>& session,
                     uint64_t client_id,
                     const std::vector<uint8_t>& payload) {
  if (!session || client_id == 0 || payload.empty()) {
    return;
  }

  const auto routed =
      common::BuildRoutedMessage(client_id,
                                 static_cast<uint16_t>(common::MsgId::kKick),
                                 payload);
  session->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed);
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

std::vector<uint8_t> BuildDuplicateLoginKickPayload() {
  return BuildKickPayload(mir2::proto::ErrorCode::ERR_KICK_DUPLICATE_LOGIN,
                          "Account logged in elsewhere",
                          "duplicate login");
}

std::vector<uint8_t> BuildMailboxOverflowKickPayload() {
  return BuildKickPayload(mir2::proto::ErrorCode::ERR_KICK_ADMIN_MANUAL,
                          "Mailbox overflow",
                          "logic overload");
}

std::vector<uint8_t> BuildMailboxExceptionKickPayload() {
  return BuildKickPayload(mir2::proto::ErrorCode::ERR_KICK_ADMIN_MANUAL,
                          "Mailbox handler failure",
                          "mailbox exception");
}
}  // namespace

LogicServer::LogicServer() = default;

LogicServer::~LogicServer() {
  Shutdown();
}

uint32_t LogicServer::ResolveDefaultMapId() const {
  uint32_t default_map_id = 1;
  const auto& combat_config = config::ConfigManager::Instance().GetCombatConfig();
  if (combat_config.default_respawn_map_id != 0) {
    default_map_id = combat_config.default_respawn_map_id;
  }
  return default_map_id;
}

bool LogicServer::BindWorldMapContext(uint32_t map_id,
                                      ecs::World& world,
                                      bool require_map) {
  if (!scene_manager_) {
    SYSLOG_ERROR("LogicServer BindWorldMapContext failed: scene manager missing");
    return false;
  }

  auto* map = scene_manager_->GetMap(static_cast<int32_t>(map_id));
  if (!map) {
    if (require_map) {
      SYSLOG_ERROR("LogicServer BindWorldMapContext failed: map {} not loaded", map_id);
      return false;
    }
    return false;
  }

  map->SetEventBus(&world.GetEventBus());
  world.Registry().ctx().insert_or_assign<game::map::MapInstance*>(std::move(map));
  if (map_context_service_) {
    map_context_service_->BindRegistry(world.Registry(), map_id);
  }
  return true;
}

bool LogicServer::BootstrapMapRuntime(uint32_t default_map_id) {
  if (!registry_manager_ || !scene_manager_) {
    SYSLOG_ERROR("LogicServer map runtime bootstrap failed: managers not ready");
    return false;
  }
  if (!map_context_service_) {
    map_context_service_ =
        std::make_unique<game::map::MapContextService>(*scene_manager_);
  }

  std::filesystem::path map_table_dir = std::filesystem::path("config") / "tables";
  const std::filesystem::path config_dir =
      std::filesystem::path(config_path_).parent_path();
  if (!config_dir.empty()) {
    map_table_dir = config_dir / "tables";
  }

  const auto map_configs =
      config::MapConfigLoader::LoadAllMapConfigs(map_table_dir.string());
  std::unordered_set<uint32_t> prepared_maps;
  prepared_maps.reserve(map_configs.size() + 1);

  for (const auto& map_config : map_configs) {
    if (map_config.map_id < 0) {
      continue;
    }

    const uint32_t map_id = static_cast<uint32_t>(map_config.map_id);
    if (!prepared_maps.insert(map_id).second) {
      continue;
    }

    ecs::World* world = registry_manager_->CreateWorld(map_id);
    if (!world) {
      SYSLOG_WARN("LogicServer map runtime bootstrap skipped map {}: world init failed",
                  map_id);
      continue;
    }

    game::map::SceneManager::MapConfig scene_config;
    scene_config.map_id = map_config.map_id;
    scene_config.fixes = map_config.fixes;
    if (!scene_manager_->GetOrCreateMap(scene_config)) {
      SYSLOG_WARN("LogicServer map runtime bootstrap skipped map {}: scene init failed",
                  map_id);
      continue;
    }

    (void)BindWorldMapContext(map_id, *world, false);
  }

  ecs::World* default_world = registry_manager_->CreateWorld(default_map_id);
  if (!default_world) {
    SYSLOG_ERROR("LogicServer map runtime bootstrap failed: default world {} init failed",
                 default_map_id);
    return false;
  }

  if (prepared_maps.find(default_map_id) == prepared_maps.end()) {
    game::map::SceneManager::MapConfig default_scene_config;
    default_scene_config.map_id = static_cast<int32_t>(default_map_id);
    if (!scene_manager_->GetOrCreateMap(default_scene_config)) {
      SYSLOG_ERROR(
          "LogicServer map runtime bootstrap failed: default map {} init failed",
          default_map_id);
      return false;
    }
  }

  if (!BindWorldMapContext(default_map_id, *default_world, true)) {
    return false;
  }

  gate_manager_.LoadFromConfig("config/gates.yaml");

  size_t world_count = 0;
  registry_manager_->ForEachWorld([&world_count](uint32_t /*map_id*/, ecs::World& /*world*/) {
    ++world_count;
  });

  SYSLOG_INFO("LogicServer map runtime bootstrapped default_map_id={} maps={} worlds={}",
              default_map_id,
              scene_manager_->MapCount(),
              world_count);
  return true;
}

bool LogicServer::Initialize(const std::string& config_path) {
  if (shutdown_called_.load(std::memory_order_acquire)) {
    SYSLOG_ERROR("LogicServer Initialize rejected: server instance already shutdown");
    return false;
  }
  if (io_context_ != nullptr || network_ || executor_ || tick_timer_ || signal_set_) {
    SYSLOG_ERROR("LogicServer Initialize rejected: server instance already initialized");
    return false;
  }
  if (running_.load(std::memory_order_acquire) ||
      stopping_.load(std::memory_order_acquire)) {
    SYSLOG_ERROR("LogicServer Initialize rejected: server is running or stopping");
    return false;
  }

  if (!config::ConfigManager::Instance().Load(config_path)) {
    return false;
  }
  config_path_ = config_path;

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
  session_cleanup_on_gateway_disconnect_ =
      server_config.session_cleanup_on_gateway_disconnect;
  reconcile_cleanup_enabled_ = server_config.reconcile_cleanup_enabled;
  zombie_detection_enabled_ = server_config.zombie_detection_enabled;
  zombie_scan_interval_ms_ = static_cast<int64_t>(std::max(
      server_config.zombie_detection_scan_interval_ms, 1));
  zombie_idle_timeout_ms_ = static_cast<int64_t>(std::max(
      server_config.zombie_detection_idle_timeout_ms, 1));
  legacy_fallback_enabled_ = server_config.legacy_fallback_enabled;
  legacy_fallback_allow_auth_whitelist_ =
      server_config.legacy_fallback_allow_auth_whitelist;
  legacy_fallback_allow_critical_msgs_ =
      server_config.legacy_fallback_allow_critical_msgs;
  legacy_fallback_allow_normal_msgs_ =
      server_config.legacy_fallback_allow_normal_msgs;
  queue_full_fallback_non_best_effort_enabled_ =
      server_config.queue_full_fallback_non_best_effort_enabled;
  chat_batch_send_enabled_ = server_config.chat_batch_send_enabled;
  SYSLOG_INFO(
      "LogicServer legacy fallback policy enabled={} allow_auth_whitelist={} "
      "allow_critical={} allow_normal={} queue_full_non_best_effort_enabled={}",
      legacy_fallback_enabled_,
      legacy_fallback_allow_auth_whitelist_,
      legacy_fallback_allow_critical_msgs_,
      legacy_fallback_allow_normal_msgs_,
      queue_full_fallback_non_best_effort_enabled_);
  SYSLOG_INFO("LogicServer chat batch send enabled={}", chat_batch_send_enabled_);

  if (!app_.Initialize(server_config)) {
    SYSLOG_ERROR("LogicServer application init failed");
    return false;
  }

  if (server_config.metrics_port == 0) {
    SYSLOG_INFO("LogicServer metrics disabled by config (metrics_port=0)");
  } else {
    if (server_config.metrics_port != kMetricsPort) {
      SYSLOG_WARN("LogicServer metrics port overridden to {} (config={})",
                  kMetricsPort,
                  server_config.metrics_port);
    }
    try {
      monitor::Metrics::Instance().Init(kMetricsPort);
    } catch (const std::exception& ex) {
      SYSLOG_WARN("LogicServer metrics init failed on port {}: {}",
                  kMetricsPort,
                  ex.what());
    } catch (...) {
      SYSLOG_WARN("LogicServer metrics init failed on port {}: unknown exception",
                  kMetricsPort);
    }
  }
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
  monitor::Metrics::Instance().SetGauge(kMetricSessionReconcileSnapshotSize, 0);

  registry_manager_ = &ecs::RegistryManager::Instance();
  if (!scene_manager_) {
    scene_manager_ = std::make_unique<game::map::SceneManager>();
  }
  if (!map_context_service_) {
    map_context_service_ =
        std::make_unique<game::map::MapContextService>(*scene_manager_);
  }

  const uint32_t default_map_id = ResolveDefaultMapId();
  if (!BootstrapMapRuntime(default_map_id)) {
    SYSLOG_ERROR("LogicServer map runtime bootstrap failed");
    return false;
  }

  network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());

  executor_ = std::make_unique<CoroutineExecutor>(app_.GetIoContext());
  entity_lane_scheduler_ = std::make_unique<EntityLaneScheduler>(app_.GetIoContext());
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
  db_pool_ = std::make_shared<db::PgConnectionPool>();
  auto storage_pool = db_pool_;
  auto kv_backend =
      std::make_unique<db::StorageEngineBackend>(db_config, storage_pool);
  if (!kv_backend->Initialize()) {
    SYSLOG_ERROR("StorageEngine backend init failed");
    return false;
  }
  const auto& loaded_storage_config =
      config::ConfigManager::Instance().GetStorageEngineConfig();
  db::AccountStorageBackend::AccountCacheOptions account_cache_options;
  account_cache_options.ttl_seconds =
      loaded_storage_config.account_cache_ttl_seconds;
  account_cache_options.max_entries =
      loaded_storage_config.account_cache_max_entries;
  auto backend =
      std::make_unique<db::AccountStorageBackend>(
          std::move(kv_backend),
          db_config,
          storage_pool,
          account_cache_options);
  if (!backend->Initialize()) {
    SYSLOG_ERROR("AccountStorageBackend init failed");
    return false;
  }

  storage_engine::StorageEngine::Config storage_config;
  storage_config.l1_max_entries = loaded_storage_config.l1_max_entries;
  storage_config.l1_ttl_seconds = loaded_storage_config.l1_ttl_seconds;
  storage_config.l2_max_size_mb = loaded_storage_config.l2_max_size_mb;
  storage_config.l2_path = loaded_storage_config.l2_path;
  storage_config.l2_ttl_seconds = loaded_storage_config.l2_ttl_seconds;
  storage_config.auto_sync_interval_ms = loaded_storage_config.auto_sync_interval_ms;
  storage_config.batch_size = loaded_storage_config.batch_size;
  storage_config.sync_timeout_ms = loaded_storage_config.sync_timeout_ms;
  storage_config.queue_capacity = loaded_storage_config.queue_capacity;
  storage_config.queue_worker_threads = loaded_storage_config.queue_worker_threads;
  storage_config.queue_retry_count = loaded_storage_config.queue_retry_count;
  storage_config.queue_retry_delay_ms = loaded_storage_config.queue_retry_delay_ms;
  storage_config.dead_letter_max_items = loaded_storage_config.dead_letter_max_items;
  storage_config.enable_strict_write_guarantee =
      loaded_storage_config.enable_strict_write_guarantee;
  storage_config.critical_key_prefixes = loaded_storage_config.critical_key_prefixes;
  storage_config.sync_write_key_prefixes =
      loaded_storage_config.sync_write_key_prefixes;
  storage_config.critical_data_no_ttl = loaded_storage_config.critical_data_no_ttl;
  storage_config.enable_outbox = loaded_storage_config.enable_outbox;
  storage_config.outbox_replay_limit = loaded_storage_config.outbox_replay_limit;
  storage_config.outbox_max_items = loaded_storage_config.outbox_max_items;
  storage_config.circuit_breaker_threshold =
      loaded_storage_config.circuit_breaker_threshold;
  storage_config.circuit_breaker_timeout_ms =
      loaded_storage_config.circuit_breaker_timeout_ms;
  storage_config.enable_metrics = loaded_storage_config.enable_metrics;
  storage_config.enable_audit_log = loaded_storage_config.enable_audit_log;
  storage_config.audit_log_max_entries = loaded_storage_config.audit_log_max_entries;
  storage_config.enable_access_control =
      loaded_storage_config.enable_access_control;
  storage_config.require_auth_for_reads =
      loaded_storage_config.require_auth_for_reads;
  storage_config.access_control_token =
      loaded_storage_config.access_control_token;
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

  if (!server_config.enable_network_listener) {
    SYSLOG_INFO("LogicServer network listener disabled by config");
    SYSLOG_INFO("LogicServer initialized");
    return true;
  }

  const auto& logic_service = config::ConfigManager::Instance().GetServiceConfig().logic;
  const std::string transport = ToLowerAscii(logic_service.transport);
  bool started = false;
  std::string selected_transport = "tcp";

  if (transport == "uds" || (transport == "auto" && !logic_service.uds_path.empty())) {
    if (logic_service.uds_path.empty()) {
      if (transport == "uds") {
        SYSLOG_ERROR("Logic transport 'uds' requires non-empty services.logic.uds_path");
        return false;
      }
    } else {
#if defined(ASIO_HAS_LOCAL_SOCKETS)
      started = network_->StartUnix(logic_service.uds_path, server_config.max_connections);
      if (started) {
        selected_transport = "uds";
      } else if (transport == "auto") {
        SYSLOG_WARN(
            "LogicServer failed to start uds listener(path={}), fallback to tcp {}:{}",
            logic_service.uds_path,
            server_config.bind_ip,
            server_config.port);
      } else {
        SYSLOG_ERROR("LogicServer failed to start uds listener(path={})",
                     logic_service.uds_path);
        return false;
      }
#else
      if (transport == "uds") {
        SYSLOG_ERROR("Logic transport 'uds' is not supported on this platform");
        return false;
      }
      SYSLOG_WARN("Logic uds transport unavailable on this platform, fallback to tcp");
#endif
    }
  }

  if (!started) {
    started = network_->Start(server_config.bind_ip, server_config.port, server_config.max_connections);
    selected_transport = "tcp";
  }

  if (!started) {
    SYSLOG_ERROR("LogicServer network start failed");
    return false;
  }
  SYSLOG_INFO("LogicServer listening via {} endpoint={}{}", selected_transport,
              selected_transport == "uds" ? logic_service.uds_path : server_config.bind_ip,
              selected_transport == "uds" ? "" : ":" + std::to_string(server_config.port));

  SYSLOG_INFO("LogicServer initialized");
  return true;
}

void LogicServer::Run() {
  if (running_.exchange(true)) {
    return;
  }

  if (shutdown_called_.load(std::memory_order_acquire) ||
      stopping_.load(std::memory_order_acquire)) {
    running_.store(false, std::memory_order_release);
    return;
  }
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
      BindLogicThread(logic_thread_id_);
      if (registry_manager_) {
        registry_manager_->GetCharacterManager().BindToCurrentThread();
      }
      promise.set_value();
    });
    constexpr auto kBindPollInterval = std::chrono::milliseconds(5);
    constexpr auto kBindMaxWait = std::chrono::seconds(5);
    const auto bind_deadline = std::chrono::steady_clock::now() + kBindMaxWait;
    while (bind_future.wait_for(kBindPollInterval) != std::future_status::ready) {
      if (stopping_.load(std::memory_order_acquire)) {
        // Shutdown may race with startup in zero-tick scenarios; exit early
        // instead of waiting indefinitely on a bind callback that may never run.
        running_.store(false, std::memory_order_release);
        return;
      }
      if (std::chrono::steady_clock::now() >= bind_deadline) {
        SYSLOG_ERROR("LogicServer Run aborted: logic thread bind timed out");
        RequestStop();
        running_.store(false, std::memory_order_release);
        return;
      }
    }
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

  // Cancel and destroy io-bound control objects on the io_context thread before
  // Application::Shutdown tears down io_context/reactor internals.
  if (io_context_) {
    std::promise<void> cancel_promise;
    auto cancel_future = cancel_promise.get_future();
    asio::dispatch(*io_context_, [this, promise = std::move(cancel_promise)]() mutable {
      if (tick_timer_) {
        asio::error_code ignored_ec;
        tick_timer_->cancel(ignored_ec);
      }
      if (signal_set_) {
        asio::error_code ignored_ec;
        signal_set_->cancel(ignored_ec);
      }
      tick_timer_.reset();
      signal_set_.reset();
      promise.set_value();
    });
    cancel_future.wait();
  } else {
    if (tick_timer_) {
      asio::error_code ignored_ec;
      tick_timer_->cancel(ignored_ec);
    }
    if (signal_set_) {
      asio::error_code ignored_ec;
      signal_set_->cancel(ignored_ec);
    }
    tick_timer_.reset();
    signal_set_.reset();
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
  cancelled_mailbox_clients_.clear();

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
  map_context_service_.reset();
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
  trade_handler_.reset();
  party_handler_.reset();
  mail_handler_.reset();
  ranking_handler_.reset();
  achievement_handler_.reset();
  auction_handler_.reset();
  npc_command_handler_.reset();
  response_sender_.reset();
  chat_aoi_manager_.reset();
  guild_system_ = nullptr;
  prewarm_manager_.reset();
  executor_.reset();
  entity_lane_scheduler_.reset();
  role_store_.reset();
  db_pool_.reset();
  if (storage_engine::StorageEngine::IsInitialized()) {
    storage_engine::StorageEngine::Instance().Flush(10000);
    storage_engine::StorageEngine::Shutdown();
  }
  network_.reset();
  io_context_ = nullptr;
  ClearLogicThread();
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

  const auto hung_scan_start = std::chrono::steady_clock::now();
  MaybeScanHungCoroutines(now);
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseHungScanMs,
      DurationMs(std::chrono::steady_clock::now() - hung_scan_start));

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
  if (response_sender_ && ecs_combat_service_ && role_store_ && !attack_handler_) {
    attack_handler_ = std::make_unique<AttackHandler>(*response_sender_,
                                                      *ecs_combat_service_,
                                                      *role_store_);
  }
  if (response_sender_ && ecs_combat_service_ && role_store_ && !skill_handler_) {
    skill_handler_ = std::make_unique<SkillHandler>(*response_sender_,
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
    const auto& server_config = config::ConfigManager::Instance().GetServerConfig();
    const int configured_capacity = server_config.login_username_rate_limit_capacity;
    const int configured_refill_rate = server_config.login_username_rate_limit_refill_rate;
    const int configured_refill_interval =
        server_config.login_username_rate_limit_refill_interval_seconds;
    const mir2::security::RateLimiter::Config login_rate_limit_config{
        .capacity = std::max(1, configured_capacity),
        .refill_rate = std::max(1, configured_refill_rate),
        .refill_interval_seconds = std::max(1, configured_refill_interval)};
    if (configured_capacity <= 0 ||
        configured_refill_rate <= 0 ||
        configured_refill_interval <= 0) {
      SYSLOG_WARN(
          "LogicServer invalid login username rate limit config "
          "(capacity={}, refill_rate={}, refill_interval_seconds={}), "
          "clamped to (capacity={}, refill_rate={}, refill_interval_seconds={})",
          configured_capacity,
          configured_refill_rate,
          configured_refill_interval,
          login_rate_limit_config.capacity,
          login_rate_limit_config.refill_rate,
          login_rate_limit_config.refill_interval_seconds);
    }
    login_handler_ = std::make_unique<LoginHandler>(*executor_,
                                                    *response_sender_,
                                                    *login_service_,
                                                    client_registry_,
                                                    *role_store_,
                                                    login_rate_limit_config);
  }
  if (response_sender_ && registry_manager_ && role_store_ && !character_handler_) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    character_handler_ = std::make_unique<CharacterHandler>(*response_sender_,
                                                            character_manager,
                                                            *role_store_,
                                                            client_registry_,
                                                            [this](uint64_t player_id) {
                                                              TriggerImmediateStateSync(player_id);
                                                            });
  }
  const uint32_t default_map_id = ResolveDefaultMapId();

  ecs::World* default_world = nullptr;
  entt::registry* default_registry = nullptr;
  ecs::TeleportSystem* teleport_system = nullptr;
  if (registry_manager_) {
    default_world = registry_manager_->GetWorld(default_map_id);
    if (default_world) {
      if (!BindWorldMapContext(default_map_id, *default_world, true)) {
        SYSLOG_ERROR("LogicServer failed to bind world runtime for default_map_id={}",
                     default_map_id);
        return;
      }
      default_registry = &default_world->Registry();
      if (!guild_system_) {
        guild_system_ = default_world->CreateSystem<ecs::GuildSystem>(
            default_world->GetEventBus(),
            game::guild::GuildManager::Instance());
      }
      auto& bundle = EnsureWorldSystems(default_map_id, *default_world);
      teleport_system = bundle.teleport_system;
    } else {
      SYSLOG_ERROR("LogicServer missing default world map_id={}", default_map_id);
      return;
    }
  }

  mir2::game::map::AOIManager* chat_aoi = nullptr;
  if (scene_manager_) {
    if (auto* map = scene_manager_->GetMap(static_cast<int32_t>(default_map_id))) {
      chat_aoi = map->GetAOIManager();
    }
  }
  if (!chat_aoi) {
    SYSLOG_ERROR("LogicServer missing default map AOI manager map_id={}", default_map_id);
    return;
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
                                                  *default_registry,
                                                  chat_batch_send_enabled_);
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

  if (default_registry && !ranking_service_) {
    ranking_service_ = std::make_unique<RankingService>(*default_registry, db_pool_);
  }

  if (response_sender_ && default_registry && !trade_handler_) {
    trade_handler_ = std::make_unique<TradeHandler>(*response_sender_,
                                                    client_registry_,
                                                    *default_registry,
                                                    role_store_.get(),
                                                    ecs_inventory_service_.get());
  }

  if (response_sender_ && default_registry && !party_handler_) {
    party_handler_ = std::make_unique<PartyHandler>(*response_sender_,
                                                    client_registry_,
                                                    *default_registry,
                                                    role_store_.get());
  }

  if (response_sender_ && ranking_service_ && default_registry && !ranking_handler_) {
    ranking_handler_ = std::make_unique<RankingHandler>(*response_sender_,
                                                        *ranking_service_,
                                                        *default_registry);
  }

  if (response_sender_ && default_registry && !mail_handler_) {
    mail_handler_ = std::make_unique<MailHandler>(*response_sender_,
                                                  client_registry_,
                                                  *default_registry,
                                                  role_store_.get(),
                                                  db_pool_);
  }

  if (response_sender_ && default_registry && !achievement_handler_) {
    achievement_handler_ = std::make_unique<AchievementHandler>(*response_sender_,
                                                                *default_registry);
  }

  if (response_sender_ && default_registry && !auction_handler_) {
    auction_handler_ = std::make_unique<AuctionHandler>(*response_sender_,
                                                        client_registry_,
                                                        *default_registry,
                                                        role_store_.get(),
                                                        db_pool_);
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
          matrix::kLoginHandlerMsgIds,
          "login_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return login_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (movement_handler_) {
      register_direct_table(
          matrix::kMovementHandlerMsgIds,
          "movement_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return movement_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (attack_handler_) {
      register_direct_table(
          matrix::kAttackHandlerMsgIds,
          "attack_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return attack_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (skill_handler_) {
      register_direct_table(
          matrix::kSkillHandlerMsgIds,
          "skill_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return skill_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (character_handler_) {
      register_direct_table(
          matrix::kCharacterHandlerMsgIds,
          "character_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return character_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (chat_handler_) {
      register_direct_table(
          matrix::kChatHandlerMsgIds,
          "chat_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return chat_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (item_handler_) {
      register_direct_table(
          matrix::kItemHandlerMsgIds,
          "item_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return item_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (guild_handler_) {
      register_direct_table(
          matrix::kGuildHandlerMsgIds,
          "guild_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return guild_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (trade_handler_) {
      register_direct_table(
          matrix::kTradeHandlerMsgIds,
          "trade_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return trade_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (party_handler_) {
      register_direct_table(
          matrix::kPartyHandlerMsgIds,
          "party_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return party_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (ranking_handler_) {
      register_direct_table(
          matrix::kRankingHandlerMsgIds,
          "ranking_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return ranking_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (mail_handler_) {
      register_direct_table(
          matrix::kMailHandlerMsgIds,
          "mail_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return mail_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (achievement_handler_) {
      register_direct_table(
          matrix::kAchievementHandlerMsgIds,
          "achievement_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return achievement_handler_->HandleMessage(
                std::move(ctx), payload, payload_size);
          });
    }
    if (auction_handler_) {
      register_direct_table(
          matrix::kAuctionHandlerMsgIds,
          "auction_handler",
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return auction_handler_->HandleMessage(
                std::move(ctx), payload, payload_size);
          });
    }
    if (npc_command_handler_) {
      register_msg_aware_table(
          matrix::kNpcHandlerMsgIds,
          "npc_command_handler",
          [this](HandlerContext ctx,
                 uint16_t msg_id,
                 const uint8_t* payload,
                 size_t payload_size) {
            return npc_command_handler_->HandleMessage(
                std::move(ctx), msg_id, payload, payload_size);
          });
    }

    for (const auto& binding : matrix::kPlaceholderBindings) {
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

bool LogicServer::IsLegacyBypassAllowed(uint16_t msg_id) const {
  if (!legacy_fallback_enabled_) {
    return false;
  }
  if (IsAuthWhitelistedMsgId(msg_id)) {
    return legacy_fallback_allow_auth_whitelist_;
  }
  switch (ClassifyMsgPriority(msg_id)) {
    case events::HotEventPriority::kCritical:
      return legacy_fallback_allow_critical_msgs_;
    case events::HotEventPriority::kNormal:
      return legacy_fallback_allow_normal_msgs_;
    case events::HotEventPriority::kBestEffort:
      return false;
    default:
      return false;
  }
}

HandlerContext LogicServer::BuildHandlerContext(uint64_t client_id) {
  if (!AssertOnLogicThread("LogicServer::BuildHandlerContext")) {
    return {};
  }

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

  if (!AssertOnLogicThread("LogicServer::DispatchHotEvent")) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
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

  if (!AssertOnLogicThread("LogicServer::DispatchHotEventsBatch")) {
    for (const auto& event : events) {
      if (hot_event_pipeline_ && event.var_ref.length > 0) {
        hot_event_pipeline_->ReleaseVarPayload(event);
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
    if (cancelled_mailbox_clients_.find(event.client_id) !=
        cancelled_mailbox_clients_.end()) {
      release_payload(event);
      monitor::Metrics::Instance().IncrementCounter(kMetricMailboxCancelledIncomingDropTotal);
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
  events::HotEvent in_flight_event{};
  bool has_in_flight_event = false;
  try {
    for (;;) {
      auto it = player_mailboxes_.find(client_id);
      if (cancelled_mailbox_clients_.find(client_id) !=
          cancelled_mailbox_clients_.end()) {
        if (it != player_mailboxes_.end()) {
          if (it->second.executing && mailbox_active_runners_ > 0) {
            --mailbox_active_runners_;
          }
          it->second.executing = false;
          player_mailboxes_.erase(it);
          PublishMailboxQueueMetrics();
        }
        cancelled_mailbox_clients_.erase(client_id);
        SYSLOG_INFO("LogicServer mailbox runner cancelled client_id={}", client_id);
        co_return;
      }
      if (it == player_mailboxes_.end()) {
        cancelled_mailbox_clients_.erase(client_id);
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

      in_flight_event = next_event;
      has_in_flight_event = true;
      if (cancelled_mailbox_clients_.find(client_id) !=
          cancelled_mailbox_clients_.end()) {
        if (hot_event_pipeline_ && in_flight_event.var_ref.length > 0) {
          hot_event_pipeline_->ReleaseVarPayload(in_flight_event);
        }
        has_in_flight_event = false;
        continue;
      }
      co_await ExecuteQueuedEvent(next_event);
      has_in_flight_event = false;
    }
  } catch (const std::exception& ex) {
    monitor::Metrics::Instance().IncrementCounter(kMetricMailboxRunnerExceptionTotal);
    SYSLOG_ERROR(
        "LogicServer mailbox runner exception client_id={} msg_id={} type={} "
        "entity_id={} entity_version={} enqueue_ts_us={} error={}",
        client_id,
        has_in_flight_event ? in_flight_event.msg_id : 0,
        has_in_flight_event ? ToString(in_flight_event.type) : "none",
        has_in_flight_event ? in_flight_event.entity_id : 0,
        has_in_flight_event ? in_flight_event.entity_version : 0,
        has_in_flight_event ? in_flight_event.enqueue_ts_us : 0,
        ex.what());
  } catch (...) {
    monitor::Metrics::Instance().IncrementCounter(kMetricMailboxRunnerExceptionTotal);
    SYSLOG_ERROR(
        "LogicServer mailbox runner exception client_id={} msg_id={} type={} "
        "entity_id={} entity_version={} enqueue_ts_us={} error=unknown",
        client_id,
        has_in_flight_event ? in_flight_event.msg_id : 0,
        has_in_flight_event ? ToString(in_flight_event.type) : "none",
        has_in_flight_event ? in_flight_event.entity_id : 0,
        has_in_flight_event ? in_flight_event.entity_version : 0,
        has_in_flight_event ? in_flight_event.enqueue_ts_us : 0);
  }

  if (has_in_flight_event && hot_event_pipeline_ &&
      in_flight_event.var_ref.length > 0) {
    hot_event_pipeline_->ReleaseVarPayload(in_flight_event);
  }

  KickMailboxException(client_id);
  (void)CleanupClientSession(client_id, "mailbox_exception");
  cancelled_mailbox_clients_.erase(client_id);
  co_return;
}

bool LogicServer::CancelMailbox(uint64_t client_id, const char* reason) {
  if (!AssertOnLogicThread("LogicServer::CancelMailbox")) {
    return false;
  }
  if (client_id == 0) {
    return false;
  }

  auto it = player_mailboxes_.find(client_id);
  if (it == player_mailboxes_.end()) {
    cancelled_mailbox_clients_.erase(client_id);
    return false;
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
  it->second.high_priority_events.clear();
  it->second.low_priority_events.clear();

  if (dropped >= mailbox_pending_events_total_) {
    mailbox_pending_events_total_ = 0;
  } else {
    mailbox_pending_events_total_ -= dropped;
  }

  const bool had_executing_runner = it->second.executing;
  if (had_executing_runner && mailbox_active_runners_ > 0) {
    --mailbox_active_runners_;
  }

  if (had_executing_runner) {
    cancelled_mailbox_clients_.insert(client_id);
  } else {
    cancelled_mailbox_clients_.erase(client_id);
  }
  it->second.executing = false;
  player_mailboxes_.erase(it);

  monitor::Metrics::Instance().IncrementCounter(kMetricMailboxCancelTotal);
  if (dropped > 0) {
    monitor::Metrics::Instance().IncrementCounter(kMetricMailboxCancelDroppedPendingTotal,
                                                  static_cast<uint64_t>(dropped));
  }
  PublishMailboxQueueMetrics();

  SYSLOG_INFO(
      "LogicServer cancelled mailbox client_id={} reason={} dropped_pending={} had_runner={}",
      client_id,
      reason == nullptr ? "unknown" : reason,
      dropped,
      had_executing_runner);
  return true;
}

void LogicServer::HandleMailboxSpawnRejected(uint64_t client_id, SpawnResult reason) {
  auto it = player_mailboxes_.find(client_id);
  if (it == player_mailboxes_.end()) {
    cancelled_mailbox_clients_.erase(client_id);
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
  cancelled_mailbox_clients_.erase(client_id);

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
  if (!AssertOnLogicThread("LogicServer::ExecuteQueuedEvent")) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    co_return;
  }

  if (cancelled_mailbox_clients_.find(event.client_id) !=
      cancelled_mailbox_clients_.end()) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    co_return;
  }

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
  if (context.IsValid() && !context.ValidateCacheVersion()) {
    monitor::Metrics::Instance().IncrementCounter(kMetricEntityVersionMismatchTotal);
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    SYSLOG_WARN("LogicServer stale context before dispatch client_id={} msg_id={} entity={}",
                event.client_id,
                event.msg_id,
                static_cast<uint64_t>(entt::to_integral(context.entity)));
    co_return;
  }

  std::optional<EntityLaneScheduler::ScopedLane> lane_guard;
  if (entity_lane_scheduler_) {
    lane_guard.emplace(
        co_await entity_lane_scheduler_->Enter(BuildLaneKey(context, event.client_id)));
  }
  if (cancelled_mailbox_clients_.find(event.client_id) !=
      cancelled_mailbox_clients_.end()) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    co_return;
  }

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
  SendKickPayload(GetGatewaySession(), client_id, BuildMailboxOverflowKickPayload());
}

void LogicServer::KickMailboxException(uint64_t client_id) {
  SendKickPayload(GetGatewaySession(), client_id, BuildMailboxExceptionKickPayload());
}

void LogicServer::OnGatewayDisconnected() {
  if (!session_cleanup_on_gateway_disconnect_) {
    return;
  }

  const std::thread::id bound_logic_thread = logic_thread_id_;
  if (bound_logic_thread == std::thread::id() ||
      std::this_thread::get_id() == bound_logic_thread) {
    CleanupAllClientSessions("gateway_disconnected");
    return;
  }

  if (!PostToMainThread([this]() { CleanupAllClientSessions("gateway_disconnected"); })) {
    SYSLOG_WARN("LogicServer failed to post gateway disconnect cleanup to main thread");
  }
}

void LogicServer::CleanupAllClientSessions(const char* reason) {
  if (!AssertOnLogicThread("LogicServer::CleanupAllClientSessions")) {
    return;
  }

  const auto clients = client_registry_.GetAll();
  std::unordered_set<uint64_t> cleanup_client_ids(clients.begin(), clients.end());
  for (const auto& [client_id, _] : client_last_activity_ms_) {
    cleanup_client_ids.insert(client_id);
  }
  for (const uint64_t client_id : last_reconciled_client_ids_) {
    cleanup_client_ids.insert(client_id);
  }
  for (const uint64_t client_id : cancelled_mailbox_clients_) {
    cleanup_client_ids.insert(client_id);
  }
  {
    std::lock_guard<std::mutex> lock(backpressure_mutex_);
    for (const auto& [client_id, _] : backpressure_until_ms_) {
      cleanup_client_ids.insert(client_id);
    }
  }

  uint64_t cleaned_total = 0;
  for (const uint64_t client_id : cleanup_client_ids) {
    if (CleanupClientSession(client_id, reason)) {
      ++cleaned_total;
    }
  }
  last_reconciled_client_ids_.clear();
  monitor::Metrics::Instance().IncrementCounter(kMetricSessionCleanupAllTotal, cleaned_total);

  if (cleaned_total > 0) {
    SYSLOG_INFO("LogicServer session cleanup_all reason={} cleaned={}",
                reason == nullptr ? "unknown" : reason,
                cleaned_total);
  }
}

bool LogicServer::CleanupClientSession(uint64_t client_id, const char* reason) {
  if (!AssertOnLogicThread("LogicServer::CleanupClientSession")) {
    return false;
  }
  if (client_id == 0) {
    return false;
  }

  bool cleaned = false;
  if (CancelMailbox(client_id, reason)) {
    cleaned = true;
  }
  std::optional<uint64_t> role_id_opt;
  std::optional<uint64_t> account_id_opt;
  if (role_store_) {
    role_id_opt = role_store_->GetRoleId(client_id);
    account_id_opt = role_store_->GetAccountId(client_id);
  }

  if (role_id_opt.has_value() && registry_manager_ &&
      *role_id_opt <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    character_manager.OnDisconnect(static_cast<uint32_t>(*role_id_opt));
    cleaned = true;
  }

  if (role_id_opt.has_value() || account_id_opt.has_value()) {
    cleaned = true;
  }
  if (role_store_) {
    role_store_->UnbindClient(client_id);
  }

  if (client_registry_.Contains(client_id)) {
    cleaned = true;
  }
  client_registry_.Remove(client_id);

  if (client_last_activity_ms_.erase(client_id) > 0) {
    cleaned = true;
  }
  if (last_reconciled_client_ids_.erase(client_id) > 0) {
    cleaned = true;
  }

  {
    std::lock_guard<std::mutex> lock(backpressure_mutex_);
    if (backpressure_until_ms_.erase(client_id) > 0) {
      cleaned = true;
    }
  }

  if (cleaned) {
    SYSLOG_DEBUG("LogicServer cleaned client session reason={} client_id={}",
                 reason == nullptr ? "unknown" : reason,
                 client_id);
  }
  return cleaned;
}

void LogicServer::ReconcileWithGatewaySnapshot(
    const std::unordered_set<uint64_t>& restored_client_ids,
    uint32_t request_id) {
  if (!AssertOnLogicThread("LogicServer::ReconcileWithGatewaySnapshot")) {
    return;
  }

  last_reconciled_client_ids_ = restored_client_ids;
  monitor::Metrics::Instance().SetGauge(
      kMetricSessionReconcileSnapshotSize,
      static_cast<double>(restored_client_ids.size()));

  if (!reconcile_cleanup_enabled_) {
    return;
  }

  const auto local_client_ids = client_registry_.GetAll();
  uint64_t local_only_total = 0;
  for (const uint64_t client_id : local_client_ids) {
    if (restored_client_ids.find(client_id) != restored_client_ids.end()) {
      continue;
    }
    if (CleanupClientSession(client_id, "reconcile_missing_in_gateway_snapshot")) {
      ++local_only_total;
    }
  }

  monitor::Metrics::Instance().IncrementCounter(
      kMetricSessionCleanupReconcileTotal, local_only_total);
  monitor::Metrics::Instance().IncrementCounter(
      kMetricSessionReconcileLocalOnlyTotal, local_only_total);

  if (local_only_total > 0) {
    SYSLOG_INFO(
        "LogicServer reconcile request_id={} snapshot_size={} local_only_cleaned={}",
        request_id,
        restored_client_ids.size(),
        local_only_total);
  }
}

void LogicServer::MarkClientActivity(uint64_t client_id, int64_t now_ms) {
  if (client_id == 0 || now_ms <= 0) {
    return;
  }

  const std::thread::id bound_logic_thread = logic_thread_id_;
  if (bound_logic_thread != std::thread::id() &&
      std::this_thread::get_id() != bound_logic_thread) {
    if (io_context_) {
      asio::post(*io_context_, [this, client_id, now_ms]() {
        MarkClientActivity(client_id, now_ms);
      });
    }
    return;
  }

  client_last_activity_ms_[client_id] = now_ms;
}

void LogicServer::ScanZombieSessions(int64_t now_ms) {
  if (!zombie_detection_enabled_ || now_ms <= 0) {
    return;
  }
  if (!AssertOnLogicThread("LogicServer::ScanZombieSessions")) {
    return;
  }
  if (last_zombie_scan_ms_ > 0 &&
      now_ms - last_zombie_scan_ms_ < zombie_scan_interval_ms_) {
    return;
  }
  last_zombie_scan_ms_ = now_ms;
  monitor::Metrics::Instance().IncrementCounter(kMetricSessionZombieScanTotal);

  const auto local_client_ids = client_registry_.GetAll();
  uint64_t cleaned_total = 0;
  for (const uint64_t client_id : local_client_ids) {
    if (last_reconciled_client_ids_.find(client_id) != last_reconciled_client_ids_.end()) {
      continue;
    }
    const auto it = client_last_activity_ms_.find(client_id);
    if (it == client_last_activity_ms_.end()) {
      continue;
    }
    if (now_ms < it->second) {
      SYSLOG_WARN(
          "LogicServer zombie scan skipped client due to non-monotonic activity time "
          "(client_id={}, now_ms={}, last_activity_ms={})",
          client_id,
          now_ms,
          it->second);
      continue;
    }
    if (now_ms - it->second < zombie_idle_timeout_ms_) {
      continue;
    }
    if (CleanupClientSession(client_id, "zombie_idle_and_not_reconciled")) {
      ++cleaned_total;
    }
  }

  monitor::Metrics::Instance().IncrementCounter(
      kMetricSessionCleanupZombieTotal, cleaned_total);
  if (cleaned_total > 0) {
    SYSLOG_INFO("LogicServer zombie scan cleaned={} idle_timeout_ms={} scan_interval_ms={}",
                cleaned_total,
                zombie_idle_timeout_ms_,
                zombie_scan_interval_ms_);
  }
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
        bool should_cleanup = false;
        {
          std::lock_guard<std::mutex> lock(gateway_mutex_);
          if (gateway_session_ == disconnected) {
            gateway_session_.reset();
            should_cleanup = true;
          }
        }
        if (should_cleanup) {
          OnGatewayDisconnected();
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
  if (!AssertOnLogicThread("LogicServer::HandleRoutedMessage")) {
    return;
  }

  if (!session) {
    return;
  }

  common::RoutedMessageData routed;
  if (!common::ParseRoutedMessage(payload, &routed)) {
    SYSLOG_ERROR("LogicServer failed to parse routed message");
    return;
  }
  // Keep activity timestamps in the same clock domain as zombie detection.
  MarkClientActivity(routed.client_id, mir2::core::GetCurrentTimestampMs());

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
    const bool should_fallback =
        queue_full_fallback_non_best_effort_enabled_ &&
        ShouldFallbackOnQueueFull(routed.msg_id);
    if (should_fallback) {
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
  if (!AssertOnLogicThread("LogicServer::DispatchRoutedMessageLegacy")) {
    return false;
  }

  if (!IsLegacyBypassAllowed(msg_id)) {
    monitor::Metrics::Instance().IncrementCounter(kMetricLegacyDispatchBlockedTotal);
    SYSLOG_WARN("LogicServer blocked legacy dispatch by policy msg_id={} client_id={}",
                msg_id,
                client_id);
    return false;
  }

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
  std::unordered_set<uint64_t> restored_client_ids;
  if (const auto* connections = response->connections()) {
    entries.reserve(connections->size());
    restored_client_ids.reserve(connections->size());
    for (const auto* ctx : *connections) {
      if (ctx && ctx->client_id() != 0) {
        restored_client_ids.insert(ctx->client_id());
      }
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
  ReconcileWithGatewaySnapshot(restored_client_ids, request_id);

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
                                            ecs::CharacterId player_id,
                                            ecs::AccountId account_id) {
  if (client_id == 0 || player_id == ecs::kInvalidCharacterId) {
    return false;
  }

  std::optional<common::CharacterData> snapshot;
  const std::string key = "char:" + std::to_string(player_id);
  if (storage_engine::StorageEngine::IsInitialized()) {
    try {
      auto& engine = storage_engine::StorageEngine::Instance();
      auto stored = engine.Get(key);
      if (!stored) {
        stored = engine.LoadFromDB(key);
      }
      if (stored) {
        auto decoded = ecs::DeserializeCharacterSnapshotWithMetadata(
            stored->data.data(), stored->data.size());
        if (!decoded.has_value()) {
          monitor::Metrics::Instance().IncrementCounter(
              kMetricPrewarmSnapshotDecodeFailedTotal);
          SYSLOG_WARN("LogicServer prewarm snapshot decode failed (player_id={})",
                      player_id);
        } else {
          snapshot = std::move(decoded->data);
          bool rewrite_needed = decoded->migrated_from_legacy;
          if (decoded->migrated_from_legacy) {
            monitor::Metrics::Instance().IncrementCounter(
                kMetricPrewarmLegacySnapshotMigratedTotal);
          }

          if (snapshot->account_id == ecs::kInvalidAccountId &&
              account_id != ecs::kInvalidAccountId) {
            snapshot->account_id = account_id;
            rewrite_needed = true;
            monitor::Metrics::Instance().IncrementCounter(
                kMetricPrewarmSnapshotAccountBackfillTotal);
          }

          if (rewrite_needed) {
            const auto migrated_bytes = ecs::SerializeCharacterSnapshot(*snapshot);
            if (!engine.Set(key, migrated_bytes, storage_engine::Priority::HIGH)) {
              monitor::Metrics::Instance().IncrementCounter(
                  kMetricPrewarmSnapshotRewriteFailedTotal);
              SYSLOG_WARN("LogicServer prewarm snapshot rewrite failed (player_id={})",
                          player_id);
            }
          }
        }
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
          const auto evicted_client_id = role_store_->BindClientRole(client_id, player_id);
          if (evicted_client_id.has_value() && *evicted_client_id != client_id) {
            if (auto gateway = GetGatewaySession()) {
              const auto routed =
                  common::BuildRoutedMessage(*evicted_client_id,
                                             static_cast<uint16_t>(common::MsgId::kKick),
                                             BuildDuplicateLoginKickPayload());
              gateway->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed);
            }
            CleanupClientSession(*evicted_client_id, "prewarm_duplicate_login");
            SYSLOG_WARN(
                "LogicServer prewarm evicted old client on duplicate login "
                "(player_id={}, old_client_id={}, new_client_id={})",
                player_id,
                *evicted_client_id,
                client_id);
          }
          client_registry_.Track(client_id);

          ecs::AccountId resolved_account_id = account_id;
          if (resolved_account_id == ecs::kInvalidAccountId && snapshot) {
            resolved_account_id = static_cast<ecs::AccountId>(snapshot->account_id);
          }

          if (resolved_account_id != ecs::kInvalidAccountId) {
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

          TriggerImmediateStateSync(player_id);

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

void LogicServer::TriggerImmediateStateSync(uint64_t player_id) {
  if (player_id == 0 || !registry_manager_) {
    return;
  }

  bool sent = false;
  registry_manager_->ForEachWorld([this, player_id, &sent](uint32_t map_id, ecs::World& world) {
    if (sent) {
      return;
    }
    auto& bundle = EnsureWorldSystems(map_id, world);
    if (!bundle.world_sync_broadcast_service) {
      return;
    }
    sent = bundle.world_sync_broadcast_service->RequestImmediateStateSyncForRole(player_id);
  });

  if (!sent) {
    SYSLOG_DEBUG("LogicServer immediate StateSync skipped (player_id={} not found)", player_id);
  }
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

bool LogicServer::ReloadStorageRuntimeConfig() {
  if (config_path_.empty()) {
    SYSLOG_WARN("LogicServer runtime config reload skipped: empty config path");
    return false;
  }
  if (!storage_engine::StorageEngine::IsInitialized()) {
    SYSLOG_WARN("LogicServer runtime config reload skipped: storage not initialized");
    return false;
  }
  if (!config::ConfigManager::Instance().Reload()) {
    SYSLOG_ERROR("LogicServer runtime config reload failed: unable to reload {}",
                 config_path_);
    return false;
  }

  const auto& loaded = config::ConfigManager::Instance().GetStorageEngineConfig();
  storage_engine::StorageEngine::RuntimeTunableConfig runtime;
  runtime.l1_ttl_seconds = loaded.l1_ttl_seconds;
  runtime.auto_sync_interval_ms = loaded.auto_sync_interval_ms;
  runtime.batch_size = loaded.batch_size;
  runtime.queue_retry_count = loaded.queue_retry_count;
  runtime.queue_retry_delay_ms = loaded.queue_retry_delay_ms;
  runtime.circuit_breaker_threshold = loaded.circuit_breaker_threshold;
  runtime.circuit_breaker_timeout_ms = loaded.circuit_breaker_timeout_ms;
  runtime.enable_metrics = loaded.enable_metrics;
  runtime.enable_strict_write_guarantee = loaded.enable_strict_write_guarantee;
  runtime.enable_access_control = loaded.enable_access_control;
  runtime.require_auth_for_reads = loaded.require_auth_for_reads;
  runtime.access_control_token = loaded.access_control_token;

  const bool applied =
      storage_engine::StorageEngine::Instance().ApplyRuntimeConfig(runtime);
  if (applied) {
    SYSLOG_INFO(
        "LogicServer reloaded storage runtime config: l1_ttl={} sync_ms={} batch={} retry={} retry_delay_ms={} metrics={} strict_write={} access_control={} auth_reads={}",
        loaded.l1_ttl_seconds,
        loaded.auto_sync_interval_ms,
        loaded.batch_size,
        loaded.queue_retry_count,
        loaded.queue_retry_delay_ms,
        loaded.enable_metrics,
        loaded.enable_strict_write_guarantee,
        loaded.enable_access_control,
        loaded.require_auth_for_reads);
  } else {
    SYSLOG_ERROR("LogicServer failed to apply storage runtime config");
  }
  return applied;
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
    SYSLOG_INFO(
        "LogicServer received SIGUSR1, dumping active coroutines and reloading storage config");
    DumpActiveCoroutines("SIGUSR1");
    ReloadStorageRuntimeConfig();
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

  if (io_context_) {
    asio::dispatch(*io_context_, [this]() {
      if (tick_timer_) {
        asio::error_code ignored_ec;
        tick_timer_->cancel(ignored_ec);
      }
      if (signal_set_) {
        asio::error_code ignored_ec;
        signal_set_->cancel(ignored_ec);
      }
    });
  }

  if (network_) {
    network_->Stop();
  }
}

WorldSystemBundle& LogicServer::EnsureWorldSystems(uint32_t map_id,
                                                   ecs::World& world) {
  (void)BindWorldMapContext(map_id, world, false);

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
    bundle->effect_system =
        std::make_unique<ecs::EffectSystem>(world.Registry(), &world.GetEventBus());
  }

  if (!bundle->skill_system) {
    auto skill_system = std::make_unique<ecs::SkillSystem>(
        world.Registry(),
        map_context_service_.get(),
        false);
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

  if (!bundle->world_sync_broadcast_service && response_sender_ && role_store_) {
    bundle->world_sync_broadcast_service =
        std::make_unique<WorldSyncBroadcastService>(
            *response_sender_, world.GetEventBus(), *role_store_, scene_manager_.get());
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
  if (bundle.world_sync_broadcast_service) {
    bundle.world_sync_broadcast_service->Tick(now_ms);
  }
}

void LogicServer::Tick(float delta_time) {
  if (!AssertOnLogicThread("LogicServer::Tick")) {
    return;
  }

  if (entity_lane_scheduler_) {
    monitor::Metrics::Instance().SetGauge(
        kMetricEntityLaneActive,
        static_cast<double>(entity_lane_scheduler_->ActiveLanes()));
    monitor::Metrics::Instance().SetGauge(
        kMetricEntityLanePending,
        static_cast<double>(entity_lane_scheduler_->PendingWaiters()));
  }

  const auto hot_event_start = std::chrono::steady_clock::now();
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
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseHotEventMs,
      DurationMs(std::chrono::steady_clock::now() - hot_event_start));

  const auto zombie_scan_start = std::chrono::steady_clock::now();
  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  ScanZombieSessions(now_ms);
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseZombieScanMs,
      DurationMs(std::chrono::steady_clock::now() - zombie_scan_start));

  std::chrono::steady_clock::duration ecs_world_systems_duration =
      std::chrono::steady_clock::duration::zero();
  std::chrono::steady_clock::duration ecs_registry_update_duration =
      std::chrono::steady_clock::duration::zero();
  std::chrono::steady_clock::duration character_update_duration =
      std::chrono::steady_clock::duration::zero();
  if (registry_manager_) {
    const auto ecs_world_systems_start = std::chrono::steady_clock::now();
    registry_manager_->ForEachWorld([this, delta_time, now_ms](uint32_t map_id,
                                                              ecs::World& world) {
      auto& bundle = EnsureWorldSystems(map_id, world);
      TickWorldSystems(world, bundle, delta_time, now_ms);
    });
    ecs_world_systems_duration = std::chrono::steady_clock::now() -
                                 ecs_world_systems_start;

    const auto ecs_registry_update_start = std::chrono::steady_clock::now();
    registry_manager_->UpdateAll(delta_time);
    ecs_registry_update_duration = std::chrono::steady_clock::now() -
                                   ecs_registry_update_start;

    const auto character_update_start = std::chrono::steady_clock::now();
    registry_manager_->GetCharacterManager().Update(delta_time);
    character_update_duration = std::chrono::steady_clock::now() -
                                character_update_start;
  }
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseEcsWorldSystemsMs, DurationMs(ecs_world_systems_duration));
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseEcsRegistryUpdateMs, DurationMs(ecs_registry_update_duration));
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseCharacterUpdateMs, DurationMs(character_update_duration));

  const auto network_tick_start = std::chrono::steady_clock::now();
  if (network_) {
    network_->Tick();
  }
  monitor::Metrics::Instance().SetGauge(
      kMetricTickPhaseNetworkTickMs,
      DurationMs(std::chrono::steady_clock::now() - network_tick_start));
}

}  // namespace mir2::logic
