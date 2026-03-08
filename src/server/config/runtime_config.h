/**
 * @file runtime_config.h
 * @brief Runtime gameplay config snapshot primitives.
 */

#ifndef MIR2_CONFIG_RUNTIME_CONFIG_H_
#define MIR2_CONFIG_RUNTIME_CONFIG_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/item_template.h"
#include "ecs/components/skill_template_component.h"
#include "game/entity/monster_drop_config.h"
#include "game/entity/monster_spawn_config.h"
#include "game/map/gate_manager.h"
#include "game/map/map_attributes.h"
#include "game/npc/npc_types.h"
#include "logic/services/merchant_service.h"

namespace mir2::config {

struct ConfigArtifact {
  std::string name;
  std::string file;
  std::string hash;
  std::size_t row_count = 0;
};

struct ConfigManifest {
  std::string bundle_type;
  int schema_version = 0;
  std::string generated_at;
  std::vector<ConfigArtifact> artifacts;

  [[nodiscard]] bool HasArtifact(std::string_view name) const;
  [[nodiscard]] const ConfigArtifact* FindArtifact(std::string_view name) const;
};

struct ConfigData {
  ConfigManifest manifest;
  struct RuntimeMapConfig {
    int32_t map_id = 0;
    mir2::game::map::MapAttributes attributes;
    std::vector<std::pair<int32_t, int32_t>> fixes;
  };

  std::unordered_map<uint32_t, mir2::data::ItemTemplate> items;
  std::unordered_map<uint32_t, mir2::ecs::SkillTemplate> skills;
  std::unordered_map<int32_t, RuntimeMapConfig> maps;
  std::vector<mir2::game::map::GateInfo> gates;
  std::unordered_map<uint32_t, mir2::game::entity::MonsterDropTable> drop_tables;
  std::unordered_map<uint32_t, mir2::logic::ShopConfig> shops;
  std::unordered_map<uint32_t, mir2::game::entity::MonsterSpawnPoint> monster_spawn_points;
  std::unordered_map<uint64_t, mir2::game::npc::NpcConfig> npcs;

  [[nodiscard]] const mir2::data::ItemTemplate* FindItem(uint32_t item_id) const;
  [[nodiscard]] const mir2::ecs::SkillTemplate* FindSkill(uint32_t skill_id) const;
};

class ConfigBundleLoader {
 public:
  static std::optional<ConfigData> LoadFromRuntimeDir(
      const std::filesystem::path& runtime_dir,
      std::string* error_out = nullptr);
};

class ConfigStore {
 public:
  [[nodiscard]] std::shared_ptr<const ConfigData> GetSnapshot() const;

  bool ReloadFromRuntimeDir(const std::filesystem::path& runtime_dir,
                            std::string* error_out = nullptr);

 private:
  mutable std::shared_mutex mutex_;
  std::shared_ptr<const ConfigData> snapshot_;
};

}  // namespace mir2::config

#endif  // MIR2_CONFIG_RUNTIME_CONFIG_H_
