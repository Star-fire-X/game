#include "logic/services/ecs_combat_service.h"

#include <optional>

#include <entt/entt.hpp>

#include "combat_generated.h"
#include "config/config_manager.h"
#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/combat_system.h"
#include "ecs/world.h"
#include "log/logger.h"
#include "logic/services/error_code_adapter.h"
#include "server/ecs/systems/combat_core.h"

namespace mir2::logic {

namespace {

mir2::ecs::EventBus* ResolveEventBus(mir2::ecs::RegistryManager& registry_manager,
                                    std::optional<uint32_t> map_id) {
  if (!map_id.has_value()) {
    return nullptr;
  }
  auto* world = registry_manager.GetWorld(*map_id);
  return world ? &world->GetEventBus() : nullptr;
}

}  // namespace

EcsCombatService::EcsCombatService(mir2::ecs::RegistryManager& registry_manager)
    : registry_manager_(registry_manager) {}

CombatResult EcsCombatService::Attack(uint64_t attacker_id,
                                      uint64_t target_id,
                                      mir2::proto::EntityType target_type) {
  CombatResult result;

  if (target_type == mir2::proto::EntityType::NONE) {
    SYSLOG_WARN("EcsCombatService::Attack invalid target_type=NONE (attacker={}, target={})",
                attacker_id, target_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }
  if (attacker_id == 0) {
    SYSLOG_WARN("EcsCombatService::Attack invalid attacker_id=0");
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }
  if (target_id == 0) {
    SYSLOG_WARN("EcsCombatService::Attack invalid target_id=0 (attacker={})", attacker_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto attacker_opt = character_manager.TryGet(static_cast<uint32_t>(attacker_id));
  if (!attacker_opt.has_value()) {
    SYSLOG_WARN("EcsCombatService::Attack failed to resolve attacker entity (id={})",
                attacker_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }
  entt::entity attacker = *attacker_opt;

  auto target_opt = character_manager.TryGet(static_cast<uint32_t>(target_id));
  if (!target_opt.has_value()) {
    SYSLOG_WARN("EcsCombatService::Attack target not found (attacker={}, target={})",
                attacker_id, target_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }
  entt::entity target = *target_opt;

  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(attacker_id));
  entt::registry* target_registry = character_manager.TryGetRegistry(static_cast<uint32_t>(target_id));
  if (!registry || !target_registry || registry != target_registry) {
    SYSLOG_WARN("EcsCombatService::Attack registry mismatch (attacker={}, target={})",
                attacker_id, target_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }

  if (!registry->valid(attacker) || !registry->valid(target)) {
    SYSLOG_WARN("EcsCombatService::Attack invalid entities (attacker={}, target={})",
                attacker_id, target_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  const auto map_id = character_manager.TryGetMapId(static_cast<uint32_t>(attacker_id));
  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);

  const auto& config = config::ConfigManager::Instance().GetCombatConfig();
  const auto attack_result = mir2::ecs::CombatSystem::ExecuteAttack(
      *registry, attacker, target, config, event_bus);

  result.code = ToLegacyError(attack_result.error_code);
  result.damage = attack_result.damage.final_damage;
  if (auto* target_attrs = registry->try_get<mir2::ecs::CharacterAttributesComponent>(target)) {
    result.target_hp = target_attrs->hp;
  }
  result.target_dead = attack_result.target_died;

  SYSLOG_DEBUG(
      "EcsCombatService::Attack attacker={} target={} target_type={} code={} damage={} hp={} dead={}",
               attacker_id,
               target_id,
               static_cast<int>(target_type),
               static_cast<int>(result.code),
               result.damage,
               result.target_hp,
               result.target_dead);

  return result;
}

CombatResult EcsCombatService::UseSkill(uint64_t caster_id,
                                        uint64_t target_id,
                                        uint32_t skill_id) {
  CombatResult result;

  if (caster_id == 0) {
    SYSLOG_WARN("EcsCombatService::UseSkill invalid caster_id=0");
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }
  if (target_id == 0) {
    SYSLOG_WARN("EcsCombatService::UseSkill invalid target_id=0 (caster={})", caster_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }
  if (skill_id == 0) {
    SYSLOG_WARN("EcsCombatService::UseSkill invalid skill_id=0 (caster={})", caster_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto caster_opt = character_manager.TryGet(static_cast<uint32_t>(caster_id));
  if (!caster_opt.has_value()) {
    SYSLOG_WARN("EcsCombatService::UseSkill failed to resolve caster entity (id={})",
                caster_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }
  entt::entity caster = *caster_opt;

  auto target_opt = character_manager.TryGet(static_cast<uint32_t>(target_id));
  if (!target_opt.has_value()) {
    SYSLOG_WARN("EcsCombatService::UseSkill target not found (caster={}, target={})",
                caster_id, target_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }
  entt::entity target = *target_opt;

  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(caster_id));
  entt::registry* target_registry = character_manager.TryGetRegistry(static_cast<uint32_t>(target_id));
  if (!registry || !target_registry || registry != target_registry) {
    SYSLOG_WARN("EcsCombatService::UseSkill registry mismatch (caster={}, target={})",
                caster_id, target_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }

  if (!registry->valid(caster) || !registry->valid(target)) {
    SYSLOG_WARN("EcsCombatService::UseSkill invalid entities (caster={}, target={})",
                caster_id, target_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  const auto map_id = character_manager.TryGetMapId(static_cast<uint32_t>(caster_id));
  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);

  const auto& config = config::ConfigManager::Instance().GetCombatConfig();
  const mir2::common::AttackType attack_type =
      legend2::combat::get_attack_type_for_skill(skill_id);
  const auto attack_result = mir2::ecs::CombatSystem::ProcessAttackWithType(
      *registry, caster, target, config, attack_type, event_bus);

  result.code = ToLegacyError(attack_result.error_code);
  result.damage = attack_result.damage.final_damage;
  if (auto* target_attrs = registry->try_get<mir2::ecs::CharacterAttributesComponent>(target)) {
    result.target_hp = target_attrs->hp;
  }
  result.target_dead = attack_result.target_died;

  SYSLOG_DEBUG("EcsCombatService::UseSkill caster={} target={} skill={} code={} damage={} hp={} dead={}",
               caster_id,
               target_id,
               skill_id,
               static_cast<int>(result.code),
               result.damage,
               result.target_hp,
               result.target_dead);

  return result;
}

}  // namespace mir2::logic
