#include "logic/logic_server.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <future>
#include <limits>
#include <optional>
#include <string>
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
constexpr auto kTickInterval = std::chrono::milliseconds(50);
constexpr uint16_t kMetricsPort = 9091;
constexpr uint32_t kDefaultChatAoiWidth = 1024;
constexpr uint32_t kDefaultChatAoiHeight = 1024;
constexpr const char* kMonsterDropTablePath = "config/tables/monsters.yaml";
constexpr uint32_t kBackpressurePauseMs = 100;
constexpr int64_t kBackpressureSignalCooldownMs = 100;
constexpr size_t kBackpressureStateMaxEntries = 8192;
constexpr size_t kBackpressurePruneBatchSize = 256;
constexpr size_t kBackpressureStateHardCapEntries = 16384;
constexpr size_t kPlayerMailboxMaxPending = 100;
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

bool ShouldFallbackOnQueueFull(uint16_t msg_id) {
  // Heartbeat/chat are best-effort under overload. Other messages should take
  // legacy fallback to avoid dropping critical gameplay/account operations.
  switch (static_cast<common::MsgId>(msg_id)) {
    case common::MsgId::kHeartbeat:
    case common::MsgId::kChatReq:
      return false;
    default:
      return true;
  }
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
  if (!app_.Initialize(server_config)) {
    SYSLOG_ERROR("LogicServer application init failed");
    return false;
  }

  if (server_config.metrics_port != 0 && server_config.metrics_port != kMetricsPort) {
    SYSLOG_WARN("LogicServer metrics port overridden to {} (config={})",
                kMetricsPort, server_config.metrics_port);
  }
  monitor::Metrics::Instance().Init(kMetricsPort);

  registry_manager_ = &ecs::RegistryManager::Instance();
  if (!scene_manager_) {
    scene_manager_ = std::make_unique<game::map::SceneManager>();
  }

  network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());

  executor_ = std::make_unique<CoroutineExecutor>(app_.GetIoContext());
  prewarm_manager_ = std::make_unique<PrewarmManager>(*executor_);
  prewarm_manager_->SetLoader(
      [this](const PrewarmEntry& entry) {
        return RestoreSessionFromPrewarm(entry.client_id, entry.player_id, entry.account_id);
      });
  role_store_ = std::make_unique<RoleStore>();

  io_context_ = &app_.GetIoContext();
  tick_timer_ = std::make_unique<asio::steady_timer>(*io_context_);
  signal_set_ = std::make_unique<asio::signal_set>(*io_context_, SIGINT, SIGTERM);
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
    }
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
  next_tick_time_ = now + kTickInterval;
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

  Tick(delta.count());

  next_tick_time_ += kTickInterval;
  if (next_tick_time_ < now) {
    next_tick_time_ = now + kTickInterval;
  }
  ScheduleNextTick();
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
  if (executor_ && response_sender_ && registry_manager_ && role_store_ && !character_handler_) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    character_handler_ = std::make_unique<CharacterHandler>(*executor_,
                                                            *response_sender_,
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
    chat_handler_ = std::make_unique<ChatHandler>(*executor_,
                                                  *response_sender_,
                                                  client_registry_,
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

  if (executor_ && response_sender_ && registry_manager_ && scene_manager_ &&
      !movement_handler_) {
    auto& character_manager = registry_manager_->GetCharacterManager();
    movement_handler_ = std::make_unique<MovementHandler>(*executor_,
                                                          *response_sender_,
                                                          client_registry_,
                                                          character_manager,
                                                          *scene_manager_,
                                                          default_map_id,
                                                          MovementValidator::Config(),
                                                          teleport_system,
                                                          &gate_manager_);
  }

  if (handler_registry_) {
    auto register_direct_table =
        [this](const auto& msg_ids, HandlerRegistry::HandlerFunc handler) {
          for (const uint16_t msg_id : msg_ids) {
            handler_registry_->RegisterHandler(msg_id, handler);
          }
        };
    auto register_msg_aware_table =
        [this](const auto& msg_ids,
               std::function<Task<void>(HandlerContext, uint16_t, const uint8_t*, size_t)>
                   handler) {
          for (const uint16_t msg_id : msg_ids) {
            handler_registry_->RegisterHandler(
                msg_id,
                [handler, msg_id](HandlerContext ctx,
                                  const uint8_t* payload,
                                  size_t payload_size) {
                  return handler(std::move(ctx), msg_id, payload, payload_size);
                });
          }
        };

    if (login_handler_) {
      register_direct_table(
          kLoginHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return login_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (movement_handler_) {
      register_direct_table(
          kMovementHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return movement_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (attack_handler_) {
      register_direct_table(
          kAttackHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return attack_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (skill_handler_) {
      register_direct_table(
          kSkillHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return skill_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (character_handler_) {
      register_direct_table(
          kCharacterHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return character_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (chat_handler_) {
      register_direct_table(
          kChatHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return chat_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (item_handler_) {
      register_direct_table(
          kItemHandlerMsgIds,
          [this](HandlerContext ctx, const uint8_t* payload, size_t payload_size) {
            return item_handler_->HandleMessage(std::move(ctx), payload, payload_size);
          });
    }
    if (guild_handler_) {
      register_msg_aware_table(
          kGuildHandlerMsgIds,
          [this](HandlerContext ctx,
                 uint16_t msg_id,
                 const uint8_t* payload,
                 size_t payload_size) {
            return guild_handler_->HandleMessage(std::move(ctx), msg_id, payload, payload_size);
          });
    }
    if (npc_command_handler_) {
      register_msg_aware_table(
          kNpcHandlerMsgIds,
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
          });
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

  if (!executor_ || event.client_id == 0) {
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    return;
  }

  bool start_runner = false;
  bool overflow = false;
  auto& mailbox = player_mailboxes_[event.client_id];
  if (mailbox.pending_events.size() >= kPlayerMailboxMaxPending) {
    overflow = true;
  } else {
    mailbox.pending_events.push_back(event);
    if (!mailbox.executing) {
      mailbox.executing = true;
      start_runner = true;
    }
  }

  if (overflow) {
    monitor::Metrics::Instance().IncrementCounter(kMetricMailboxOverflowTotal);
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    KickMailboxOverflow(event.client_id);
    return;
  }

  if (start_runner) {
    executor_->Spawn(RunPlayerMailbox(event.client_id));
  }
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

  std::unordered_map<uint64_t, std::vector<events::HotEvent>> grouped_events;
  grouped_events.reserve(events.size());

  std::vector<events::HotEvent> overflow_events;
  overflow_events.reserve(events.size() / 4 + 1);

  for (const auto& event : events) {
    if (event.client_id == 0) {
      overflow_events.push_back(event);
      continue;
    }
    grouped_events[event.client_id].push_back(event);
  }

  monitor::Metrics::Instance().SetGauge(
      kMetricMailboxBatchPlayers, static_cast<double>(grouped_events.size()));

  std::vector<uint64_t> runners_to_start;
  runners_to_start.reserve(grouped_events.size());

  for (auto& [client_id, batch] : grouped_events) {
    bool start_runner = false;
    size_t accepted = 0;
    auto& mailbox = player_mailboxes_[client_id];
    const size_t pending = mailbox.pending_events.size();
    const size_t available =
        pending >= kPlayerMailboxMaxPending ? 0 : (kPlayerMailboxMaxPending - pending);
    accepted = std::min(available, batch.size());
    for (size_t i = 0; i < accepted; ++i) {
      mailbox.pending_events.push_back(batch[i]);
    }
    if (accepted > 0 && !mailbox.executing) {
      mailbox.executing = true;
      start_runner = true;
    }

    if (start_runner) {
      runners_to_start.push_back(client_id);
    }
    for (size_t i = accepted; i < batch.size(); ++i) {
      overflow_events.push_back(batch[i]);
    }
  }

  for (const auto& event : overflow_events) {
    monitor::Metrics::Instance().IncrementCounter(kMetricMailboxOverflowTotal);
    if (hot_event_pipeline_ && event.var_ref.length > 0) {
      hot_event_pipeline_->ReleaseVarPayload(event);
    }
    if (event.client_id != 0) {
      KickMailboxOverflow(event.client_id);
    }
  }

  for (uint64_t client_id : runners_to_start) {
    executor_->Spawn(RunPlayerMailbox(client_id));
  }
}

Task<void> LogicServer::RunPlayerMailbox(uint64_t client_id) {
  for (;;) {
    auto it = player_mailboxes_.find(client_id);
    if (it == player_mailboxes_.end()) {
      co_return;
    }
    if (it->second.pending_events.empty()) {
      it->second.executing = false;
      player_mailboxes_.erase(it);
      co_return;
    }
    events::HotEvent next_event = it->second.pending_events.front();
    it->second.pending_events.pop_front();
    co_await ExecuteQueuedEvent(next_event);
  }
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
    const int64_t now_ms = network::TcpSession::NowMs();
    bool should_signal = false;
    {
      std::lock_guard<std::mutex> lock(backpressure_mutex_);
      auto& until = backpressure_until_ms_[routed.client_id];
      if (now_ms >= until) {
        until = now_ms + kBackpressureSignalCooldownMs;
        should_signal = true;
      }

      if (backpressure_until_ms_.size() > kBackpressureStateMaxEntries) {
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
    }
    if (should_signal) {
      SendBackpressurePause(session, routed.client_id, kBackpressurePauseMs);
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
  return handler_registry_->DispatchMessage(context, msg_id, raw, payload.size());
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

  if (!executor_->Spawn(RunPrewarm(std::move(entries)))) {
    prewarm_running_.store(false);
    SYSLOG_WARN("LogicServer prewarm skipped: coroutine executor is not accepting tasks");
  }
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
    drained_events.reserve(256);
    events::HotEvent event{};
    while (hot_event_pipeline_->TryDequeue(&event)) {
      drained_events.push_back(event);
      ++drained;
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
