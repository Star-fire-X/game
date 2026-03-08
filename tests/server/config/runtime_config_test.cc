#include <gtest/gtest.h>

#include <openssl/sha.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "config/runtime_config.h"
#include "data/item_template.h"
#include "ecs/components/skill_template_component.h"

namespace mir2::config::test {
namespace {

using mir2::data::ItemTemplateManager;
using nlohmann::json;

std::string Sha256Hex(const std::string& content) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(content.data()),
         content.size(),
         digest);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(SHA256_DIGEST_LENGTH * 2);
  for (unsigned char byte : digest) {
    result.push_back(kHex[(byte >> 4) & 0x0F]);
    result.push_back(kHex[byte & 0x0F]);
  }
  return result;
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

class RuntimeConfigTest : public ::testing::Test {
 protected:
  struct ArtifactSpec {
    std::string file_name;
    std::string content;
    bool write_file = true;
    bool corrupt_hash = false;
  };

  void SetUp() override {
    ItemTemplateManager::Instance().Clear();

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    runtime_dir_ = std::filesystem::temp_directory_path() /
                   ("mir2_runtime_config_test_" + std::to_string(now));
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(runtime_dir_, ec))
        << ec.message();
  }

  void TearDown() override {
    ItemTemplateManager::Instance().Clear();
    std::error_code ec;
    std::filesystem::remove_all(runtime_dir_, ec);
  }

  static std::size_t ArtifactRowCount(const std::string& artifact_name,
                                      const std::string& content) {
    json root;
    try {
      root = json::parse(content);
    } catch (const std::exception&) {
      return 0;
    }
    if (artifact_name == "items") {
      return root.at("items").size();
    }
    if (artifact_name == "skills") {
      return root.at("skills").size();
    }
    if (artifact_name == "maps") {
      return root.at("maps").size();
    }
    if (artifact_name == "gates") {
      return root.at("gates").size();
    }
    if (artifact_name == "drops") {
      return root.at("drop_tables").size();
    }
    if (artifact_name == "shops") {
      return root.at("shops").size();
    }
    if (artifact_name == "monster_spawns") {
      return root.at("spawn_points").size();
    }
    if (artifact_name == "npcs") {
      return root.at("npcs").size();
    }
    return 0;
  }

  static std::unordered_map<std::string, ArtifactSpec> MakeCompleteArtifacts() {
    return {
        {"items", ArtifactSpec{"items.json", BuildItemsContent()}},
        {"skills", ArtifactSpec{"skills.json", BuildSkillsContent()}},
        {"maps", ArtifactSpec{"maps.json", BuildMapsContent()}},
        {"gates", ArtifactSpec{"gates.json", BuildGatesContent()}},
        {"drops", ArtifactSpec{"drops.json", BuildDropsContent()}},
        {"shops", ArtifactSpec{"shops.json", BuildShopsContent()}},
        {"monster_spawns",
         ArtifactSpec{"monster_spawns.json", BuildMonsterSpawnsContent()}},
        {"npcs", ArtifactSpec{"npcs.json", BuildNpcsContent()}},
    };
  }

  void WriteRuntimeBundle(
      const std::unordered_map<std::string, ArtifactSpec>& artifacts,
      int schema_version = 1,
      const std::string& bundle_type = "gameplay",
      const std::string& generated_at = "2026-03-06T00:00:00Z") {
    json manifest = {
        {"bundle_type", bundle_type},
        {"schema_version", schema_version},
        {"generated_at", generated_at},
        {"artifacts", json::array()},
    };

    for (const auto& [name, spec] : artifacts) {
      if (spec.write_file) {
        std::ofstream artifact_out(runtime_dir_ / spec.file_name,
                                   std::ios::out | std::ios::trunc);
        ASSERT_TRUE(artifact_out.is_open());
        artifact_out << spec.content;
        artifact_out.close();
      }

      manifest["artifacts"].push_back({
          {"name", name},
          {"file", spec.file_name},
          {"hash", spec.corrupt_hash ? std::string(64, '0') : Sha256Hex(spec.content)},
          {"row_count", ArtifactRowCount(name, spec.content)},
      });
    }
    std::sort(manifest["artifacts"].begin(),
              manifest["artifacts"].end(),
              [](const json& lhs, const json& rhs) {
                return lhs.at("name").get<std::string>() <
                       rhs.at("name").get<std::string>();
              });

    std::ofstream manifest_out(runtime_dir_ / "manifest.json",
                               std::ios::out | std::ios::trunc);
    ASSERT_TRUE(manifest_out.is_open());
    manifest_out << manifest.dump(2) << "\n";
  }

  void WriteCompleteRuntimeBundle(
      const std::unordered_map<std::string, ArtifactSpec>& overrides = {},
      int schema_version = 1,
      const std::string& bundle_type = "gameplay",
      const std::string& generated_at = "2026-03-06T00:00:00Z") {
    auto artifacts = MakeCompleteArtifacts();
    for (const auto& [name, spec] : overrides) {
      artifacts[name] = spec;
    }
    WriteRuntimeBundle(artifacts, schema_version, bundle_type, generated_at);
  }

  static std::string BuildItemsContent() {
    return json{
               {"items",
                json::array({
                    {{"id", 1001},
                     {"name", "Small Heal"},
                     {"std_mode", 0},
                     {"shape", 1},
                     {"weight", 1},
                     {"ani_count", 1},
                     {"special_pwr", 0},
                     {"item_desc", 0},
                     {"looks", 10},
                     {"dura_max", 1},
                     {"ac", 50},
                     {"mac", 0},
                     {"dc", 0},
                     {"mc", 0},
                     {"sc", 0},
                     {"need_type", 0},
                     {"need_level", 1},
                     {"need_class", 99},
                     {"price", 100},
                     {"stackable", true},
                     {"stack_limit", 20}},
                })}}
        .dump(2) +
        "\n";
  }

  static std::string BuildSkillsContent(
      std::optional<std::vector<std::string>> fields_override = std::nullopt,
      bool duplicate_ids = false,
      bool empty_array = false) {
    std::vector<std::string> fields = fields_override.value_or(std::vector<std::string>{
        "id",           "name",             "description",     "required_class",
        "required_level", "max_level",        "train_level_req", "train_points_req",
        "skill_type",   "target_type",      "is_universal",    "is_passive",
        "mp_cost",      "consumes_talisman", "talisman_cost",   "required_amulet",
        "amulet_cost",  "cooldown_ms",      "cast_time_ms",    "can_be_interrupted",
        "range",        "aoe_radius",       "min_power",       "max_power",
        "def_power",    "def_max_power",    "train_lv",        "duration_ms",
        "stat_modifier","dot_damage",       "dot_interval_ms", "effect_type",
        "effect_id",    "animation_id",     "sound_id"});

    auto make_skill = [&](uint32_t id, const std::string& name) {
      json skill = json::object();
      for (const auto& field : fields) {
        if (field == "id") {
          skill[field] = id;
        } else if (field == "name") {
          skill[field] = name;
        } else if (field == "description") {
          skill[field] = "desc";
        } else if (field == "required_class") {
          skill[field] = id == 3 ? "WARRIOR" : "MAGE";
        } else if (field == "required_level") {
          skill[field] = id == 3 ? 7 : 19;
        } else if (field == "max_level") {
          skill[field] = 3;
        } else if (field == "train_level_req") {
          skill[field] = json::array({1, 7, 11, 15});
        } else if (field == "train_points_req") {
          skill[field] = json::array({0, 500, 2000, 5000});
        } else if (field == "skill_type") {
          skill[field] = id == 3 ? "PHYSICAL" : "MAGICAL";
        } else if (field == "target_type") {
          skill[field] = "SINGLE_ENEMY";
        } else if (field == "is_universal") {
          skill[field] = false;
        } else if (field == "is_passive") {
          skill[field] = false;
        } else if (field == "mp_cost") {
          skill[field] = id == 3 ? 3 : 14;
        } else if (field == "consumes_talisman") {
          skill[field] = false;
        } else if (field == "talisman_cost") {
          skill[field] = 0;
        } else if (field == "required_amulet") {
          skill[field] = "NONE";
        } else if (field == "amulet_cost") {
          skill[field] = 0;
        } else if (field == "cooldown_ms") {
          skill[field] = id == 3 ? 800 : 1000;
        } else if (field == "cast_time_ms") {
          skill[field] = 0;
        } else if (field == "can_be_interrupted") {
          skill[field] = true;
        } else if (field == "range") {
          skill[field] = id == 3 ? 1.0 : 7.0;
        } else if (field == "aoe_radius") {
          skill[field] = 0.0;
        } else if (field == "min_power") {
          skill[field] = id == 3 ? 3 : 8;
        } else if (field == "max_power") {
          skill[field] = id == 3 ? 8 : 16;
        } else if (field == "def_power") {
          skill[field] = 0;
        } else if (field == "def_max_power") {
          skill[field] = 0;
        } else if (field == "train_lv") {
          skill[field] = 0;
        } else if (field == "duration_ms") {
          skill[field] = 0;
        } else if (field == "stat_modifier") {
          skill[field] = 0;
        } else if (field == "dot_damage") {
          skill[field] = 0;
        } else if (field == "dot_interval_ms") {
          skill[field] = 1000;
        } else if (field == "effect_type") {
          skill[field] = 0;
        } else if (field == "effect_id") {
          skill[field] = 0;
        } else if (field == "animation_id") {
          skill[field] = "";
        } else if (field == "sound_id") {
          skill[field] = "";
        }
      }
      return skill;
    };

    json skills = json::array();
    if (!empty_array) {
      skills.push_back(make_skill(3, "Attack Training"));
      skills.push_back(make_skill(duplicate_ids ? 3 : 11, "Lightning"));
    }
    return json{{"skills", skills}}.dump(2) + "\n";
  }

  static std::string BuildMapsContent(bool duplicate_map_ids = false) {
    const int second_map_id = duplicate_map_ids ? 1 : 2;
    return json{
               {"maps",
                json::array({
                    {{"map_id", 1},
                     {"is_safe_zone", false},
                     {"min_level", 1},
                     {"max_level", 255},
                     {"fixes", json::array()},
                     {"safe_zones", json::array()},
                     {"quest_requirements", json::array()}},
                    {{"map_id", second_map_id},
                     {"is_safe_zone", true},
                     {"min_level", 10},
                     {"max_level", 60},
                     {"home_map", "100"},
                     {"fixes", json::array({{{"x", 10}, {"y", 20}}})},
                     {"safe_zones",
                      json::array({{{"x", 1}, {"y", 2}, {"radius", 3}}})},
                     {"quest_requirements",
                      json::array({{{"quest_id", 7}, {"quest_value", 11}}})}},
                })}}
        .dump(2) +
        "\n";
  }

  static std::string BuildGatesContent(bool duplicate_source = false) {
    return json{
               {"gates",
                json::array({
                    {{"gate_id", 10},
                     {"source_map", "1"},
                     {"source_x", 10},
                     {"source_y", 20},
                     {"target_map", "2"},
                     {"target_x", 30},
                     {"target_y", 40},
                     {"require_item", false},
                     {"required_item_id", 0}},
                    {{"gate_id", 20},
                     {"source_map", "1"},
                     {"source_x", duplicate_source ? 10 : 11},
                     {"source_y", duplicate_source ? 20 : 21},
                     {"target_map", "3"},
                     {"target_x", 50},
                     {"target_y", 60},
                     {"require_item", true},
                     {"required_item_id", 1001}},
                })}}
        .dump(2) +
        "\n";
  }

  static std::string BuildDropsContent(bool duplicate_drop = false,
                                       bool invalid_rate = false,
                                       bool empty_tables = false) {
    json tables = json::array();
    if (!empty_tables) {
      tables.push_back({
          {"monster_template_id", 50},
          {"items",
           json::array({
               {{"item_id", 1001},
                {"drop_rate", 0.1},
                {"min_count", 0},
                {"max_count", 1},
                {"rarity", 2},
                {"boss_bonus", 0.0}},
           })},
      });
      tables.push_back({
          {"monster_template_id", 100},
          {"items",
           json::array({
               {{"item_id", 2001},
                {"drop_rate", invalid_rate ? 1.5 : 1.0},
                {"min_count", 1},
                {"max_count", 1},
                {"rarity", 1},
                {"boss_bonus", 0.0}},
               {{"item_id", duplicate_drop ? 2001 : 2002},
                {"drop_rate", 0.25},
                {"min_count", 1},
                {"max_count", 2},
                {"rarity", 3},
                {"boss_bonus", 0.5}},
           })},
      });
    }
    return json{{"drop_tables", tables}}.dump(2) + "\n";
  }

  static std::string BuildShopsContent(bool empty_shops = false) {
    json shops = json::array();
    if (!empty_shops) {
      shops.push_back({
          {"store_id", 100},
          {"name", "basic"},
          {"buy_rate", 1.0},
          {"sell_rate", 0.5},
          {"items",
           json::array({
               {{"item_id", 1001}, {"price", 100}, {"stock", -1}},
               {{"item_id", 2001}, {"price", 250}, {"stock", 5}},
           })},
      });
      shops.push_back({
          {"store_id", 200},
          {"name", "premium"},
          {"buy_rate", 1.5},
          {"sell_rate", 0.25},
          {"items",
           json::array({
               {{"item_id", 3001}, {"price", 999}, {"stock", 1}},
           })},
      });
    }
    return json{{"shops", shops}}.dump(2) + "\n";
  }

  static std::string BuildMonsterSpawnsContent(bool empty_spawn_points = false) {
    json spawn_points = json::array();
    if (!empty_spawn_points) {
      spawn_points.push_back({
          {"spawn_id", 10},
          {"map_id", 1},
          {"center_x", 10},
          {"center_y", 20},
          {"spawn_radius", 3},
          {"monster_template_id", 9001},
          {"patrol_radius", 5},
          {"respawn_interval", 12.5},
          {"max_count", 2},
          {"aggro_range", 11},
          {"attack_range", 4},
      });
      spawn_points.push_back({
          {"spawn_id", 20},
          {"map_id", 2},
          {"center_x", 30},
          {"center_y", 40},
          {"spawn_radius", 6},
          {"monster_template_id", 9002},
          {"patrol_radius", 8},
          {"respawn_interval", 30.0},
          {"max_count", 3},
          {"aggro_range", 14},
          {"attack_range", 5},
      });
    }
    return json{{"spawn_points", spawn_points}}.dump(2) + "\n";
  }

  static std::string BuildNpcsContent(bool empty_npcs = false) {
    json npcs = json::array();
    if (!empty_npcs) {
      npcs.push_back({
          {"npc_id", 1},
          {"template_id", 2001},
          {"name", "Potion Trader"},
          {"type", "MERCHANT"},
          {"map_id", 1},
          {"x", 10},
          {"y", 15},
          {"direction", 0},
          {"enabled", true},
          {"store_id", 77},
      });
      npcs.push_back({
          {"npc_id", 2},
          {"template_id", 2002},
          {"name", "Disabled Trader"},
          {"type", "MERCHANT"},
          {"map_id", 2},
          {"x", 20},
          {"y", 25},
          {"direction", 3},
          {"enabled", false},
          {"store_id", 78},
      });
    }
    return json{{"npcs", npcs}}.dump(2) + "\n";
  }

  std::filesystem::path runtime_dir_;
};

TEST_F(RuntimeConfigTest, LoadRuntimeDirParsesManifestAndItems) {
  WriteCompleteRuntimeBundle();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  EXPECT_EQ(data->manifest.bundle_type, "gameplay");
  EXPECT_EQ(data->manifest.generated_at, "2026-03-06T00:00:00Z");
  ASSERT_EQ(data->items.size(), 1u);
  const auto* item = data->FindItem(1001);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->name, "Small Heal");
  EXPECT_TRUE(item->stackable);
}

TEST_F(RuntimeConfigTest, ReloadKeepsPreviousSnapshotWhenNewRuntimeDirIsInvalid) {
  WriteCompleteRuntimeBundle();

  ConfigStore store;
  std::string error;
  ASSERT_TRUE(store.ReloadFromRuntimeDir(runtime_dir_, &error)) << error;

  WriteCompleteRuntimeBundle(
      {{"items", ArtifactSpec{"items.json", BuildItemsContent(), true, true}}},
      1,
      "gameplay",
      "2026-03-07T00:00:00Z");

  error.clear();
  EXPECT_FALSE(store.ReloadFromRuntimeDir(runtime_dir_, &error));
  ASSERT_FALSE(error.empty());

  auto snapshot = store.GetSnapshot();
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(snapshot->manifest.generated_at, "2026-03-06T00:00:00Z");
  ASSERT_NE(snapshot->FindItem(1001), nullptr);
  EXPECT_EQ(snapshot->FindItem(2001), nullptr);
}

TEST_F(RuntimeConfigTest, ItemTemplateManagerCanLoadFromConfigData) {
  WriteCompleteRuntimeBundle();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  auto& manager = ItemTemplateManager::Instance();
  ASSERT_TRUE(manager.LoadFromConfigData(*data));
  ASSERT_EQ(manager.Count(), 1u);
  const auto* item = manager.GetTemplate(1001);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->price, 100);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirParsesSkillsWhenArtifactPresent) {
  WriteCompleteRuntimeBundle();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  ASSERT_EQ(data->skills.size(), 2u);
  const auto* skill = data->FindSkill(3);
  ASSERT_NE(skill, nullptr);
  EXPECT_EQ(skill->name, "Attack Training");
  EXPECT_EQ(skill->required_class, mir2::common::CharacterClass::WARRIOR);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenBundleTypeIsInvalid) {
  WriteCompleteRuntimeBundle({}, 1, "runtime");

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("bundle_type"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSchemaVersionIsInvalid) {
  WriteCompleteRuntimeBundle({}, 0);

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("schema_version"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSkillsArtifactIsMissing) {
  auto artifacts = MakeCompleteArtifacts();
  artifacts.erase("skills");
  WriteRuntimeBundle(artifacts);

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("skills"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenRowCountDoesNotMatchArtifact) {
  auto artifacts = MakeCompleteArtifacts();
  WriteRuntimeBundle(artifacts);

  const auto manifest_path = runtime_dir_ / "manifest.json";
  auto manifest = json::parse(ReadTextFile(manifest_path));
  for (auto& artifact : manifest["artifacts"]) {
    if (artifact.at("name") == "shops") {
      artifact["row_count"] = 999;
    }
  }
  std::ofstream manifest_out(manifest_path, std::ios::out | std::ios::trunc);
  ASSERT_TRUE(manifest_out.is_open());
  manifest_out << manifest.dump(2) << "\n";
  manifest_out.flush();
  ASSERT_TRUE(manifest_out.good());
  manifest_out.close();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("row_count"), std::string::npos) << error;
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirParsesMapsGatesAndDropsWhenArtifactsPresent) {
  WriteCompleteRuntimeBundle();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  ASSERT_EQ(data->maps.size(), 2u);
  const auto map_it = data->maps.find(2);
  ASSERT_NE(map_it, data->maps.end());
  EXPECT_TRUE(map_it->second.attributes.is_safe_zone);
  ASSERT_EQ(map_it->second.fixes.size(), 1u);
  EXPECT_EQ(map_it->second.fixes[0].first, 10);
  EXPECT_EQ(map_it->second.fixes[0].second, 20);

  ASSERT_EQ(data->gates.size(), 2u);
  EXPECT_EQ(data->gates[0].gate_id, 10u);
  EXPECT_EQ(data->gates[1].required_item_id, 1001);

  ASSERT_EQ(data->drop_tables.size(), 2u);
  const auto drop_it = data->drop_tables.find(100);
  ASSERT_NE(drop_it, data->drop_tables.end());
  ASSERT_EQ(drop_it->second.items.size(), 2u);
  EXPECT_EQ(drop_it->second.items[0].item_id, 2001u);
  EXPECT_FLOAT_EQ(drop_it->second.items[1].boss_bonus, 0.5f);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirAllowsEmptyDropsArtifact) {
  WriteCompleteRuntimeBundle(
      {{"drops", ArtifactSpec{"drops.json", BuildDropsContent(false, false, true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  EXPECT_TRUE(data->drop_tables.empty());
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirParsesShopsAndMonsterSpawnsWhenArtifactsPresent) {
  WriteCompleteRuntimeBundle();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  ASSERT_EQ(data->shops.size(), 2u);
  const auto shop_it = data->shops.find(100);
  ASSERT_NE(shop_it, data->shops.end());
  EXPECT_EQ(shop_it->second.name, "basic");
  EXPECT_FLOAT_EQ(shop_it->second.buy_rate, 1.0f);
  ASSERT_EQ(shop_it->second.items.size(), 2u);
  EXPECT_EQ(shop_it->second.items[0].item_id, 1001u);
  EXPECT_EQ(shop_it->second.items[1].stock, 5);

  ASSERT_EQ(data->monster_spawn_points.size(), 2u);
  const auto spawn_it = data->monster_spawn_points.find(20);
  ASSERT_NE(spawn_it, data->monster_spawn_points.end());
  EXPECT_EQ(spawn_it->second.map_id, 2u);
  EXPECT_EQ(spawn_it->second.center_x, 30);
  EXPECT_EQ(spawn_it->second.monster_template_id, 9002u);
  EXPECT_FLOAT_EQ(spawn_it->second.respawn_interval, 30.0f);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirAllowsEmptyShopsAndMonsterSpawnsArtifacts) {
  WriteCompleteRuntimeBundle(
      {{"shops", ArtifactSpec{"shops.json", BuildShopsContent(true)}},
       {"monster_spawns",
        ArtifactSpec{"monster_spawns.json", BuildMonsterSpawnsContent(true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  EXPECT_TRUE(data->shops.empty());
  EXPECT_TRUE(data->monster_spawn_points.empty());
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirParsesNpcsWhenArtifactPresent) {
  WriteCompleteRuntimeBundle();

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  ASSERT_EQ(data->npcs.size(), 2u);
  const auto npc_it = data->npcs.find(1u);
  ASSERT_NE(npc_it, data->npcs.end());
  EXPECT_EQ(npc_it->second.template_id, 2001u);
  EXPECT_EQ(npc_it->second.name, "Potion Trader");
  EXPECT_EQ(npc_it->second.store_id, 77u);

  const auto disabled_it = data->npcs.find(2u);
  ASSERT_NE(disabled_it, data->npcs.end());
  EXPECT_FALSE(disabled_it->second.enabled);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirAllowsEmptyNpcsArtifact) {
  WriteCompleteRuntimeBundle(
      {{"npcs", ArtifactSpec{"npcs.json", BuildNpcsContent(true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  EXPECT_TRUE(data->npcs.empty());
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirAllowsEmptySkillsArray) {
  WriteCompleteRuntimeBundle(
      {{"skills",
        ArtifactSpec{"skills.json", BuildSkillsContent(std::nullopt, false, true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  ASSERT_TRUE(data.has_value()) << error;
  EXPECT_TRUE(data->skills.empty());
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSkillsMissingRequiredCanonicalField) {
  WriteCompleteRuntimeBundle(
      {{"skills",
        ArtifactSpec{"skills.json",
                     BuildSkillsContent(std::vector<std::string>{
                         "id", "name", "description", "required_level", "max_level",
                         "train_level_req", "train_points_req", "skill_type",
                         "target_type", "is_universal", "is_passive", "mp_cost",
                         "consumes_talisman", "talisman_cost", "required_amulet",
                         "amulet_cost", "cooldown_ms", "cast_time_ms",
                         "can_be_interrupted", "range", "aoe_radius", "min_power",
                         "max_power", "def_power", "def_max_power", "train_lv",
                         "duration_ms", "stat_modifier", "dot_damage",
                         "dot_interval_ms", "effect_type", "effect_id",
                         "animation_id", "sound_id"})}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("required_class"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSkillsContainDuplicateIds) {
  WriteCompleteRuntimeBundle(
      {{"skills", ArtifactSpec{"skills.json", BuildSkillsContent(std::nullopt, true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("duplicate skill id"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSkillsHashIsCorrupted) {
  WriteCompleteRuntimeBundle(
      {{"skills", ArtifactSpec{"skills.json", BuildSkillsContent(), true, true}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("artifact hash mismatch"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenMapsArtifactHashIsCorrupted) {
  WriteCompleteRuntimeBundle(
      {{"maps", ArtifactSpec{"maps.json", BuildMapsContent(), true, true}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("artifact hash mismatch"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSkillsFileIsMissing) {
  WriteCompleteRuntimeBundle(
      {{"skills", ArtifactSpec{"skills.json", BuildSkillsContent(), false, false}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to open"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenGatesArtifactFileIsMissing) {
  WriteCompleteRuntimeBundle(
      {{"gates", ArtifactSpec{"gates.json", BuildGatesContent(), false, false}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to open"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenSkillsJsonIsInvalid) {
  WriteCompleteRuntimeBundle(
      {{"skills", ArtifactSpec{"skills.json", "{ not-valid-json }\n"}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to parse skills json"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenDropsJsonIsInvalid) {
  WriteCompleteRuntimeBundle(
      {{"drops", ArtifactSpec{"drops.json", "{ not-valid-json }\n"}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to parse drops json"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenShopsJsonIsInvalid) {
  WriteCompleteRuntimeBundle(
      {{"shops", ArtifactSpec{"shops.json", "{ not-valid-json }\n"}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to parse shops json"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenMonsterSpawnsJsonIsInvalid) {
  WriteCompleteRuntimeBundle(
      {{"monster_spawns",
        ArtifactSpec{"monster_spawns.json", "{ not-valid-json }\n"}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to parse monster_spawns json"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenNpcsJsonIsInvalid) {
  WriteCompleteRuntimeBundle(
      {{"npcs", ArtifactSpec{"npcs.json", "{ not-valid-json }\n"}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to parse npcs json"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenNpcsArtifactHashIsCorrupted) {
  WriteCompleteRuntimeBundle(
      {{"npcs", ArtifactSpec{"npcs.json", BuildNpcsContent(), true, true}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("artifact hash mismatch"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenNpcsFileIsMissing) {
  WriteCompleteRuntimeBundle(
      {{"npcs", ArtifactSpec{"npcs.json", BuildNpcsContent(), false, false}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("failed to open"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenNpcsContainInvalidFields) {
  const auto invalid_npcs = json{{"npcs",
                                  json::array({
                                      {{"npc_id", 1},
                                       {"template_id", 0},
                                       {"name", "Potion Trader"},
                                       {"type", "MERCHANT"},
                                       {"map_id", 1},
                                       {"x", 10},
                                       {"y", 15},
                                       {"direction", 0},
                                       {"enabled", true},
                                       {"store_id", 77}},
                                  })}}
                               .dump(2) +
                           "\n";
  WriteCompleteRuntimeBundle(
      {{"npcs", ArtifactSpec{"npcs.json", invalid_npcs}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("template_id"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenMapsContainDuplicateMapIds) {
  WriteCompleteRuntimeBundle(
      {{"maps", ArtifactSpec{"maps.json", BuildMapsContent(true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("duplicate map_id"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenGatesContainDuplicateSourceCoordinates) {
  WriteCompleteRuntimeBundle(
      {{"gates", ArtifactSpec{"gates.json", BuildGatesContent(true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("duplicate gate source coordinate"), std::string::npos);
}

TEST_F(RuntimeConfigTest, LoadRuntimeDirFailsWhenDropsContainInvalidDropRate) {
  WriteCompleteRuntimeBundle(
      {{"drops", ArtifactSpec{"drops.json", BuildDropsContent(false, true)}}});

  std::string error;
  auto data = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir_, &error);

  EXPECT_FALSE(data.has_value());
  EXPECT_NE(error.find("drop_rate"), std::string::npos);
}

}  // namespace
}  // namespace mir2::config::test
