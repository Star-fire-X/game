#include "game/game_server.h"

#include <filesystem>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "config/config_manager.h"
#include "config/map_config_loader.h"
#include "handlers/chat/chat_handler.h"
#include "handlers/combat/combat_handler.h"
#include "handlers/handler_utils.h"
#include "handlers/item/item_handler.h"
#include "handlers/movement/movement_handler.h"
#include "handlers/movement/entity_broadcast_service.h"
#include "ecs/components/character_components.h"
#include "ecs/systems/combat_system.h"
#include "legacy/character.h"
#include "legacy/inventory_system.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::game {

namespace {

class EcsCombatService final : public legend2::handlers::CombatService {
public:
    EcsCombatService(mir2::ecs::CharacterEntityManager& character_manager,
                     const legend2::CombatConfig& combat_config)
        : character_manager_(character_manager),
          combat_config_(combat_config) {}

    legend2::handlers::CombatResult Attack(uint64_t attacker_id, uint64_t target_id) override {
        return ExecuteAttack(attacker_id, target_id);
    }

    legend2::handlers::CombatResult UseSkill(uint64_t caster_id,
                                             uint64_t target_id,
                                             uint32_t /*skill_id*/) override {
        return ExecuteAttack(caster_id, target_id);
    }

private:
    legend2::handlers::CombatResult ExecuteAttack(uint64_t attacker_id, uint64_t target_id) {
        const uint32_t attacker = static_cast<uint32_t>(attacker_id);
        const uint32_t target = static_cast<uint32_t>(target_id);
        auto attacker_entity = character_manager_.GetOrCreate(attacker);
        auto target_entity = character_manager_.GetOrCreate(target);

        entt::registry* registry = character_manager_.TryGetRegistry(attacker);
        entt::registry* target_registry = character_manager_.TryGetRegistry(target);
        if (attacker_entity == entt::null || target_entity == entt::null ||
            !registry || !target_registry || registry != target_registry) {
            legend2::handlers::CombatResult out;
            out.code = mir2::common::ErrorCode::kTargetNotFound;
            return out;
        }

        auto result = mir2::ecs::CombatSystem::ExecuteAttack(
            *registry, attacker_entity, target_entity, combat_config_);

        legend2::handlers::CombatResult out;
        out.code = result.success ? mir2::common::ErrorCode::kOk
                                  : legend2::handlers::ToCommonError(result.error_code);
        out.damage = result.success ? result.damage.final_damage : 0;
        out.target_dead = result.target_died;
        if (auto* attributes = registry->try_get<mir2::ecs::CharacterAttributesComponent>(target_entity)) {
            out.target_hp = attributes->hp;
        }
        return out;
    }

    mir2::ecs::CharacterEntityManager& character_manager_;
    legend2::CombatConfig combat_config_;
};

class EcsInventoryService final : public legend2::handlers::InventoryService {
public:
    explicit EcsInventoryService(mir2::ecs::CharacterEntityManager& character_manager)
        : character_manager_(character_manager) {}

    legend2::handlers::ItemPickupResult PickupItem(uint64_t character_id,
                                                   uint32_t item_id) override {
        legend2::handlers::ItemPickupResult out;
        out.item_id = item_id;

        const uint32_t id = static_cast<uint32_t>(character_id);
        if (!LoadInventoryState(id)) {
            out.code = mir2::common::ErrorCode::kUnknown;
            return out;
        }

        auto entity = character_manager_.GetOrCreate(id);
        entt::registry* registry = character_manager_.TryGetRegistry(id);
        if (entity == entt::null || !registry) {
            out.code = mir2::common::ErrorCode::kUnknown;
            return out;
        }
        auto* state = registry->try_get<mir2::ecs::CharacterStateComponent>(entity);
        legend2::Character snapshot;
        if (state) {
            snapshot.set_map_id(state->map_id);
            snapshot.set_position(state->position);
        }

        auto result = inventory_system_.pickup_item(id, item_id, snapshot);
        out.code = legend2::handlers::ToCommonError(result.error_code);
        if (result.success) {
            SaveInventoryState(id);
        }
        return out;
    }

    legend2::handlers::ItemUseResult UseItem(uint64_t character_id,
                                             uint16_t slot,
                                             uint32_t item_id) override {
        legend2::handlers::ItemUseResult out;
        out.slot = slot;
        out.item_id = item_id;

        const uint32_t id = static_cast<uint32_t>(character_id);
        if (!LoadInventoryState(id)) {
            out.code = mir2::common::ErrorCode::kUnknown;
            return out;
        }

        const auto item_opt = inventory_system_.get_item_at(id, slot);
        if (!item_opt || item_opt->template_id != item_id) {
            out.code = mir2::common::ErrorCode::kInvalidAction;
            return out;
        }

        auto result = inventory_system_.remove_item(id, slot, 1);
        out.code = legend2::handlers::ToCommonError(result.error_code);
        out.remaining = item_opt->quantity > 0
            ? static_cast<uint32_t>(item_opt->quantity - 1)
            : 0;
        if (result.success) {
            SaveInventoryState(id);
        }
        return out;
    }

    legend2::handlers::ItemDropResult DropItem(uint64_t character_id,
                                               uint16_t slot,
                                               uint32_t item_id,
                                               uint32_t count) override {
        legend2::handlers::ItemDropResult out;
        out.item_id = item_id;
        out.count = count;

        const uint32_t id = static_cast<uint32_t>(character_id);
        if (!LoadInventoryState(id)) {
            out.code = mir2::common::ErrorCode::kUnknown;
            return out;
        }

        const auto item_opt = inventory_system_.get_item_at(id, slot);
        if (!item_opt || item_opt->template_id != item_id) {
            out.code = mir2::common::ErrorCode::kInvalidAction;
            return out;
        }
        if (count == 0 || count > static_cast<uint32_t>(item_opt->quantity)) {
            out.code = mir2::common::ErrorCode::kInvalidAction;
            return out;
        }

        auto entity = character_manager_.GetOrCreate(id);
        entt::registry* registry = character_manager_.TryGetRegistry(id);
        if (entity == entt::null || !registry) {
            out.code = mir2::common::ErrorCode::kUnknown;
            return out;
        }
        auto* state = registry->try_get<mir2::ecs::CharacterStateComponent>(entity);
        mir2::common::Position drop_pos = state ? state->position : mir2::common::Position{100, 100};
        uint32_t map_id = state ? state->map_id : 1;

        auto drop_item = *item_opt;
        drop_item.quantity = static_cast<int>(count);
        inventory_system_.drop_item(drop_item, drop_pos, map_id, id);

        auto result = inventory_system_.remove_item(id, slot, static_cast<int>(count));
        out.code = legend2::handlers::ToCommonError(result.error_code);
        if (result.success) {
            SaveInventoryState(id);
        }
        return out;
    }

private:
    bool LoadInventoryState(uint32_t character_id) {
        entt::entity entity = character_manager_.GetOrCreate(character_id);
        entt::registry* registry = character_manager_.TryGetRegistry(character_id);
        if (entity == entt::null || !registry) {
            return false;
        }

        auto& inventory = registry->get_or_emplace<mir2::ecs::InventoryComponent>(entity);
        inventory_system_.deserialize_inventory(character_id, inventory.inventory_json);
        inventory_system_.deserialize_equipment(character_id, inventory.equipment_json);
        return true;
    }

    void SaveInventoryState(uint32_t character_id) {
        auto entity = character_manager_.TryGet(character_id);
        if (!entity) {
            return;
        }

        entt::registry* registry = character_manager_.TryGetRegistry(character_id);
        if (!registry) {
            return;
        }

        auto& inventory = registry->get_or_emplace<mir2::ecs::InventoryComponent>(*entity);
        inventory.inventory_json = inventory_system_.serialize_inventory(character_id);
        inventory.equipment_json = inventory_system_.serialize_equipment(character_id);
    }

    mir2::ecs::CharacterEntityManager& character_manager_;
    legend2::InventorySystem inventory_system_;
};

class LogoutHandler final : public legend2::handlers::BaseHandler {
public:
    LogoutHandler(mir2::ecs::CharacterEntityManager& character_manager,
                  legend2::handlers::ClientRegistry& client_registry)
        : BaseHandler(mir2::log::LogCategory::kGame),
          character_manager_(character_manager),
          client_registry_(client_registry) {}

protected:
    void DoHandle(const legend2::handlers::HandlerContext& context,
                  uint16_t /*msg_id*/,
                  const std::vector<uint8_t>& /*payload*/,
                  legend2::handlers::ResponseCallback callback) override {
        character_manager_.OnDisconnect(static_cast<uint32_t>(context.client_id));
        client_registry_.Remove(context.client_id);
        if (callback) {
            callback(legend2::handlers::ResponseList{});
        }
    }

private:
    mir2::ecs::CharacterEntityManager& character_manager_;
    legend2::handlers::ClientRegistry& client_registry_;
};

}  // namespace

GameServer::GameServer()
    : registry_manager_(ecs::RegistryManager::Instance()),
      character_entity_manager_(registry_manager_.GetCharacterManager()) {}

GameServer::~GameServer() = default;

bool GameServer::Initialize(const std::string& config_path) {
    if (!config::ConfigManager::Instance().Load(config_path)) {
        return false;
    }

    const auto& log_config = config::ConfigManager::Instance().GetLogConfig();
    if (!log::Logger::Instance().Initialize(log_config.path, log_config.level,
                                            log_config.max_size_mb, log_config.max_files)) {
        return false;
    }

    const auto& server_config = config::ConfigManager::Instance().GetServerConfig();
    if (!app_.Initialize(server_config)) {
        SYSLOG_ERROR("GameServer application init failed");
        return false;
    }
    monitor::Metrics::Instance().Init(server_config.metrics_port);

    network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());
    if (!network_->Start(server_config.bind_ip, server_config.port, server_config.max_connections)) {
        SYSLOG_ERROR("GameServer network start failed");
        return false;
    }

    const auto& combat_config = config::ConfigManager::Instance().GetCombatConfig();
    combat_service_ = std::make_unique<EcsCombatService>(
        character_entity_manager_, combat_config);
    inventory_service_ = std::make_unique<EcsInventoryService>(
        character_entity_manager_);

    std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
    if (config_dir.empty()) {
        config_dir = "config";
    }
    const auto map_configs = config::MapConfigLoader::LoadAllMapConfigs(
        (config_dir / "tables").string());
    auto apply_map_config = [&](int32_t map_id) {
        const config::MapConfigLoader::MapConfig* config = nullptr;
        for (const auto& entry : map_configs) {
            if (entry.map_id == map_id) {
                config = &entry;
                break;
            }
        }
        if (!config) {
            return;
        }
        if (auto* map = scene_manager_.GetMap(map_id)) {
            map->SetAttributes(config->attributes);
            for (const auto& [x, y] : config->fixes) {
                map->SetWalkable(x, y, false);
            }
        }
    };
    auto setup_effect_broadcast = [this](ecs::World* world, int32_t map_id) {
        if (!world) {
            return;
        }
        auto* map = scene_manager_.GetMap(map_id);
        if (!map) {
            return;
        }
        auto* aoi_manager = map->GetAOIManager();
        if (!aoi_manager) {
            return;
        }

        auto broadcaster = std::make_unique<ecs::EffectBroadcaster>(world->Registry());
        auto service = std::make_unique<handlers::EffectBroadcastService>(
            *network_, *aoi_manager, world->Registry());
        broadcaster->set_broadcast_callback(
            [service_ptr = service.get()](uint64_t caster_id, uint64_t target_id,
                                          uint32_t skill_id, uint8_t effect_type,
                                          const std::string& effect_id,
                                          const std::string& sound_id,
                                          int x, int y, uint32_t duration_ms) {
                service_ptr->BroadcastSkillEffect(caster_id, target_id, skill_id, effect_type,
                                                  effect_id, sound_id, x, y, duration_ms);
            });

        effect_broadcasters_.push_back(std::move(broadcaster));
        effect_broadcast_services_.push_back(std::move(service));
    };
    auto setup_entity_broadcast = [this](ecs::World* world, int32_t map_id) {
        if (!world || !network_) {
            return;
        }
        auto* map = scene_manager_.GetMap(map_id);
        if (!map) {
            return;
        }

        auto service = std::make_unique<handlers::EntityBroadcastService>(
            *network_, world->Registry());
        map->SetAOICallback(
            [service_ptr = service.get()](mir2::game::map::AOIEventType event_type,
                                          entt::entity watcher,
                                          entt::entity target,
                                          int32_t x,
                                          int32_t y) {
                service_ptr->HandleAOIEvent(event_type, watcher, target, x, y);
            });
        entity_broadcast_services_.push_back(std::move(service));
    };

    // 初始化多地图（Phase 4: 多地图支持）
    game::map::SceneManager::MapConfig map1_config{
        .map_id = 1,
        .width = 500,
        .height = 500,
        .grid_size = 20
    };
    scene_manager_.GetOrCreateMap(map1_config);
    auto* world1 = registry_manager_.CreateWorld(map1_config.map_id);
    if (world1) {
        if (auto* map = scene_manager_.GetMap(map1_config.map_id)) {
            map->SetEventBus(&world1->GetEventBus());
        }
    }
    apply_map_config(map1_config.map_id);
    SYSLOG_INFO("GameServer: Map 1 (比奇城, 500x500) initialized");
    setup_effect_broadcast(world1, map1_config.map_id);
    setup_entity_broadcast(world1, map1_config.map_id);

    game::map::SceneManager::MapConfig map2_config{
        .map_id = 2,
        .width = 300,
        .height = 300,
        .grid_size = 20
    };
    scene_manager_.GetOrCreateMap(map2_config);
    auto* world2 = registry_manager_.CreateWorld(map2_config.map_id);
    if (world2) {
        if (auto* map = scene_manager_.GetMap(map2_config.map_id)) {
            map->SetEventBus(&world2->GetEventBus());
        }
    }
    apply_map_config(map2_config.map_id);
    SYSLOG_INFO("GameServer: Map 2 (盟重土城, 300x300) initialized");
    setup_effect_broadcast(world2, map2_config.map_id);
    setup_entity_broadcast(world2, map2_config.map_id);

    game::map::SceneManager::MapConfig map3_config{
        .map_id = 3,
        .width = 200,
        .height = 200,
        .grid_size = 20
    };
    scene_manager_.GetOrCreateMap(map3_config);
    auto* world3 = registry_manager_.CreateWorld(map3_config.map_id);
    if (world3) {
        if (auto* map = scene_manager_.GetMap(map3_config.map_id)) {
            map->SetEventBus(&world3->GetEventBus());
        }
    }
    apply_map_config(map3_config.map_id);
    SYSLOG_INFO("GameServer: Map 3 (沙巴克, 200x200) initialized");
    setup_effect_broadcast(world3, map3_config.map_id);
    setup_entity_broadcast(world3, map3_config.map_id);

    // 创建并注册 TeleportSystem（Phase 4: 传送系统）
    if (world1) {
        teleport_system_ = world1->CreateSystem<ecs::TeleportSystem>(scene_manager_, world1->GetEventBus());
        SYSLOG_INFO("GameServer: TeleportSystem registered");
    }
    gate_manager_.LoadFromConfig((config_dir / "gates.yaml").string());
    for (const auto& map_config : map_configs) {
        for (const auto& gate : map_config.gates) {
            gate_manager_.AddGate(gate);
        }
    }

    RegisterMessageHandlers();
    RegisterHandlers();
    SYSLOG_INFO("GameServer initialized");
    return true;
}

void GameServer::Run() {
    if (logic_thread_.joinable()) {
        return;
    }
    logic_thread_ = std::thread([this]() { app_.Run([this](float delta_time) { Tick(delta_time); }); });
}

void GameServer::Shutdown() {
    if (network_) {
        network_->Stop();
    }
    app_.Stop();
    if (logic_thread_.joinable()) {
        logic_thread_.join();
    }
    app_.Shutdown();
    log::Logger::Instance().Shutdown();
}

void GameServer::Tick(float delta_time) {
    registry_manager_.UpdateAll(delta_time);
    registry_manager_.ForEachWorld([this, delta_time](uint32_t map_id, ecs::World& world) {
        auto* map = scene_manager_.GetMap(static_cast<int32_t>(map_id));
        if (!map) {
            return;
        }
        map->UpdateAreaEvents(delta_time, world.Registry());
    });
    character_entity_manager_.Update(delta_time);
    if (network_) {
        network_->Tick();
    }
}

void GameServer::RegisterHandlers() {
    if (!network_) {
        return;
    }

    network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                              [this](const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
                                  if (!session) {
                                      return;
                                  }
                                  common::ServiceType remote = common::ServiceType::kGateway;
                                  if (!common::ParseServiceHello(payload, &remote)) {
                                      return;
                                  }
                                  SYSLOG_INFO("GameServer handshake from service={}",
                                              static_cast<int>(remote));
                                  const auto ack = common::BuildServiceHelloAck(common::ServiceType::kGame, true);
                                  session->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHelloAck),
                                                ack);
                              });

    network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                              [this](const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
                                  HandleRoutedMessage(session, payload);
                              });
}

void GameServer::RegisterMessageHandlers() {
    auto movement_handler = std::make_shared<legend2::handlers::MovementHandler>(
        client_registry_, character_entity_manager_, scene_manager_, 1,
        legend2::handlers::MovementValidator::Config(), teleport_system_, &gate_manager_);
    auto combat_handler = std::make_shared<legend2::handlers::CombatHandler>(*combat_service_);
    auto item_handler = std::make_shared<legend2::handlers::ItemHandler>(*inventory_service_);
    auto chat_handler = std::make_shared<legend2::handlers::ChatHandler>(client_registry_);
    auto logout_handler = std::make_shared<LogoutHandler>(character_entity_manager_, client_registry_);

    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
                               movement_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kAttackReq),
                               combat_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kSkillReq),
                               combat_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq),
                               item_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq),
                               item_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq),
                               item_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                               chat_handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kLogout),
                               logout_handler);
}

void GameServer::HandleRoutedMessage(const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
    if (!session) {
        return;
    }
    common::RoutedMessageData routed;
    if (!common::ParseRoutedMessage(payload, &routed)) {
        SYSLOG_ERROR("GameServer failed to parse routed message");
        return;
    }

    client_registry_.Track(routed.client_id);

    legend2::handlers::HandlerContext context;
    context.client_id = routed.client_id;
    context.session = session;
    context.post = [this](std::function<void()> task) {
        if (task) {
            app_.GetIoContext().post(std::move(task));
        }
    };

    bool handled = handler_registry_.Dispatch(
        context, routed.msg_id, routed.payload,
        [session](const legend2::handlers::ResponseList& responses) {
            if (!session) {
                return;
            }
            for (const auto& response : responses) {
                const auto routed_rsp = common::BuildRoutedMessage(
                    response.client_id, response.msg_id, response.payload);
                session->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed_rsp);
            }
        });

    if (!handled) {
        SYSLOG_WARN("GameServer no handler for msg_id={}", routed.msg_id);
    }
}

}  // namespace mir2::game
