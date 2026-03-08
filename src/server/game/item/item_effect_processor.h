/**
 * @file item_effect_processor.h
 * @brief 物品效果处理器
 */

#ifndef MIR2_GAME_ITEM_ITEM_EFFECT_PROCESSOR_H_
#define MIR2_GAME_ITEM_ITEM_EFFECT_PROCESSOR_H_

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs {
class EventBus;
struct ItemComponent;
}  // namespace mir2::ecs

namespace mir2::game::map {
class MapContextService;
class MapInstance;
}  // namespace mir2::game::map

namespace mir2::game::item {

class ItemEffectProcessor {
 public:
  explicit ItemEffectProcessor(
      entt::registry& registry,
      ecs::EventBus& event_bus,
      map::MapContextService* map_context_service = nullptr,
      bool allow_registry_ctx_fallback = true);

  // 处理物品使用效果，返回是否消耗物品
 bool ProcessItemUse(entt::entity character, entt::entity item);

 private:
  bool ProcessDrug(entt::entity character, const ecs::ItemComponent& item);
  bool ProcessScroll(entt::entity character, const ecs::ItemComponent& item);
  bool ProcessSkillBook(entt::entity character, const ecs::ItemComponent& item);
  bool ProcessBundledDrug(entt::entity character, const ecs::ItemComponent& item);
  bool ProcessAbilityDrug(entt::entity character);
  bool ProcessExpDrug(entt::entity character);
  bool ProcessRandomScroll(entt::entity character);
  bool ProcessHomeScroll(entt::entity character);
  bool ProcessCastleScroll(entt::entity character);
  bool ProcessLottery(entt::entity character, const ecs::ItemComponent& item);
  bool ProcessBlessOil(entt::entity character);
  bool ProcessRepairOil(entt::entity character, bool perfect);
  const map::MapInstance* ResolveMapInstance(entt::entity character) const;

  entt::registry& registry_;
  ecs::EventBus& event_bus_;
  map::MapContextService* map_context_service_ = nullptr;
  bool allow_registry_ctx_fallback_ = true;
  uint32_t active_scroll_item_id_ = 0;
};

}  // namespace mir2::game::item

#endif  // MIR2_GAME_ITEM_ITEM_EFFECT_PROCESSOR_H_
