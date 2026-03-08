/**
 * @file runtime_config.cc
 * @brief Runtime gameplay config snapshot loading.
 */

#include "config/runtime_config.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <openssl/sha.h>

#include <nlohmann/json.hpp>

namespace mir2::config {

namespace {

using nlohmann::json;

void SetError(std::string* error_out, const std::string& message) {
  if (error_out != nullptr) {
    *error_out = message;
  }
}

std::optional<std::string> ReadFile(const std::filesystem::path& path,
                                    std::string* error_out) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    SetError(error_out, "failed to open " + path.string());
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

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

constexpr std::array<std::string_view, 8> kRequiredArtifactNames = {
    "items",
    "skills",
    "maps",
    "gates",
    "drops",
    "shops",
    "monster_spawns",
    "npcs",
};

std::optional<ConfigManifest> ParseManifest(const json& root,
                                            std::string* error_out) {
  if (!root.is_object()) {
    SetError(error_out, "manifest root must be an object");
    return std::nullopt;
  }
  if (!root.contains("bundle_type") || !root["bundle_type"].is_string()) {
    SetError(error_out, "manifest missing bundle_type");
    return std::nullopt;
  }
  if (!root.contains("artifacts") || !root["artifacts"].is_array()) {
    SetError(error_out, "manifest missing artifacts array");
    return std::nullopt;
  }

  ConfigManifest manifest;
  manifest.bundle_type = root.value("bundle_type", "");
  manifest.schema_version = root.value("schema_version", 0);
  manifest.generated_at = root.value("generated_at", "");
  if (manifest.bundle_type != "gameplay") {
    SetError(error_out,
             "manifest bundle_type must be gameplay, got " + manifest.bundle_type);
    return std::nullopt;
  }
  if (manifest.schema_version != 1) {
    SetError(error_out, "manifest schema_version must be 1");
    return std::nullopt;
  }
  if (manifest.generated_at.empty()) {
    SetError(error_out, "manifest generated_at is required");
    return std::nullopt;
  }

  std::unordered_set<std::string> seen_names;
  for (const auto& artifact_json : root["artifacts"]) {
    if (!artifact_json.is_object()) {
      SetError(error_out, "manifest artifact entries must be objects");
      return std::nullopt;
    }

    ConfigArtifact artifact;
    artifact.name = artifact_json.value("name", "");
    artifact.file = artifact_json.value("file", "");
    artifact.hash = artifact_json.value("hash", "");
    if (!artifact_json.contains("row_count") ||
        !artifact_json.at("row_count").is_number_unsigned()) {
      SetError(error_out, "manifest artifact " + artifact.name + " missing row_count");
      return std::nullopt;
    }
    artifact.row_count = artifact_json.at("row_count").get<std::size_t>();
    if (artifact.name.empty() || artifact.file.empty() || artifact.hash.empty()) {
      SetError(error_out, "manifest artifact is incomplete");
      return std::nullopt;
    }
    if (!seen_names.insert(artifact.name).second) {
      SetError(error_out, "manifest artifact " + artifact.name + " is duplicated");
      return std::nullopt;
    }
    const bool known_artifact =
        std::find(kRequiredArtifactNames.begin(),
                  kRequiredArtifactNames.end(),
                  artifact.name) != kRequiredArtifactNames.end();
    if (!known_artifact) {
      SetError(error_out, "manifest artifact " + artifact.name + " is not supported");
      return std::nullopt;
    }
    manifest.artifacts.push_back(std::move(artifact));
  }

  for (const auto artifact_name : kRequiredArtifactNames) {
    const auto it = std::find_if(
        manifest.artifacts.begin(),
        manifest.artifacts.end(),
        [artifact_name](const ConfigArtifact& artifact) {
          return artifact.name == artifact_name;
        });
    if (it == manifest.artifacts.end()) {
      SetError(error_out,
               "manifest missing required artifact " + std::string(artifact_name));
      return std::nullopt;
    }
  }

  return manifest;
}

bool VerifyArtifact(const std::filesystem::path& runtime_dir,
                    const ConfigArtifact& artifact,
                    std::string* content_out,
                    std::string* error_out) {
  const auto path = runtime_dir / artifact.file;
  auto content = ReadFile(path, error_out);
  if (!content.has_value()) {
    return false;
  }

  const std::string actual_hash = Sha256Hex(*content);
  if (actual_hash != artifact.hash) {
    SetError(error_out,
             "artifact hash mismatch for " + artifact.name + " file=" + path.string());
    return false;
  }

  if (content_out != nullptr) {
    *content_out = std::move(*content);
  }
  return true;
}

bool VerifyRowCount(const ConfigArtifact& artifact,
                    std::size_t actual_row_count,
                    std::string* error_out) {
  if (artifact.row_count != actual_row_count) {
    SetError(error_out,
             "artifact row_count mismatch for " + artifact.name + ": expected " +
                 std::to_string(artifact.row_count) + " actual " +
                 std::to_string(actual_row_count));
    return false;
  }
  return true;
}

mir2::data::ItemTemplate ParseItemTemplate(const json& item_json) {
  mir2::data::ItemTemplate item;
  item.id = item_json.value("id", 0u);
  item.name = item_json.value("name", "");
  item.std_mode = item_json.value("std_mode", 0);
  item.shape = item_json.value("shape", 0);
  item.weight = item_json.value("weight", 0);
  item.ani_count = item_json.value("ani_count", 0);
  item.special_pwr = item_json.value("special_pwr", 0);
  item.item_desc = item_json.value("item_desc", 0);
  item.looks = item_json.value("looks", 0);
  item.dura_max = item_json.value("dura_max", 0);
  item.ac = item_json.value("ac", 0);
  item.mac = item_json.value("mac", 0);
  item.dc = item_json.value("dc", 0);
  item.mc = item_json.value("mc", 0);
  item.sc = item_json.value("sc", 0);
  item.need_type = item_json.value("need_type", 0);
  item.need_level = item_json.value("need_level", 0);
  item.need_class = item_json.value("need_class", 99);
  item.price = item_json.value("price", 0);
  item.stackable = item_json.value("stackable", false);
  item.stack_limit = item_json.value("stack_limit", 1);
  return item;
}

bool LoadItems(const std::string& content,
               std::unordered_map<uint32_t, mir2::data::ItemTemplate>* items_out,
               std::string* error_out) {
  if (items_out == nullptr) {
    SetError(error_out, "items output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse items json: ") + ex.what());
    return false;
  }

  if (!root.contains("items") || !root["items"].is_array()) {
    SetError(error_out, "items json missing items array");
    return false;
  }

  items_out->clear();
  for (const auto& item_json : root["items"]) {
    if (!item_json.is_object()) {
      SetError(error_out, "items entry must be an object");
      return false;
    }

    auto item = ParseItemTemplate(item_json);
    if (item.id == 0) {
      SetError(error_out, "items entry has invalid id=0");
      return false;
    }
    const auto [it, inserted] = items_out->emplace(item.id, std::move(item));
    if (!inserted) {
      SetError(error_out, "duplicate item id " + std::to_string(it->first));
      return false;
    }
  }

  return true;
}

bool ReadRequiredString(const json& node,
                        const char* key,
                        std::string* out,
                        std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  try {
    *out = node.at(key).get<std::string>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string("skills field ") + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

template <typename T>
bool ReadRequiredScalar(const json& node,
                        const char* key,
                        T* out,
                        std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  try {
    *out = node.at(key).get<T>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string("skills field ") + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

template <typename T, std::size_t N>
bool ReadRequiredArray(const json& node,
                       const char* key,
                       std::array<T, N>* out,
                       std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  if (!node.at(key).is_array()) {
    SetError(error_out, std::string("skills field ") + key + " must be an array");
    return false;
  }
  if (node.at(key).size() != N) {
    SetError(error_out,
             std::string("skills field ") + key + " must contain " +
                 std::to_string(N) + " entries");
    return false;
  }

  try {
    for (std::size_t i = 0; i < N; ++i) {
      (*out)[i] = node.at(key).at(i).get<T>();
    }
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string("skills field ") + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

template <typename EnumType>
bool ParseEnumByStringOrInt(const json& value,
                            std::initializer_list<std::pair<const char*, EnumType>> names,
                            EnumType* out) {
  if (value.is_number_integer()) {
    *out = static_cast<EnumType>(value.get<int>());
    return true;
  }
  if (!value.is_string()) {
    return false;
  }

  const std::string text = value.get<std::string>();
  for (const auto& [name, enum_value] : names) {
    if (text == name) {
      *out = enum_value;
      return true;
    }
  }
  return false;
}

bool ReadRequiredCharacterClass(const json& node,
                                const char* key,
                                mir2::common::CharacterClass* out,
                                std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  if (!ParseEnumByStringOrInt(
          node.at(key),
          {{"WARRIOR", mir2::common::CharacterClass::WARRIOR},
           {"MAGE", mir2::common::CharacterClass::MAGE},
           {"TAOIST", mir2::common::CharacterClass::TAOIST}},
          out)) {
    SetError(error_out, std::string("skills field ") + key + " invalid");
    return false;
  }
  return true;
}

bool ReadRequiredSkillType(const json& node,
                           const char* key,
                           mir2::common::SkillType* out,
                           std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  if (!ParseEnumByStringOrInt(
          node.at(key),
          {{"PHYSICAL", mir2::common::SkillType::PHYSICAL},
           {"MAGICAL", mir2::common::SkillType::MAGICAL},
           {"BUFF", mir2::common::SkillType::BUFF},
           {"DEBUFF", mir2::common::SkillType::DEBUFF},
           {"HEAL", mir2::common::SkillType::HEAL}},
          out)) {
    SetError(error_out, std::string("skills field ") + key + " invalid");
    return false;
  }
  return true;
}

bool ReadRequiredSkillTarget(const json& node,
                             const char* key,
                             mir2::common::SkillTarget* out,
                             std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  if (!ParseEnumByStringOrInt(
          node.at(key),
          {{"SELF", mir2::common::SkillTarget::SELF},
           {"SINGLE_ENEMY", mir2::common::SkillTarget::SINGLE_ENEMY},
           {"SINGLE_ALLY", mir2::common::SkillTarget::SINGLE_ALLY},
           {"AOE_ENEMY", mir2::common::SkillTarget::AOE_ENEMY},
           {"AOE_ALLY", mir2::common::SkillTarget::AOE_ALLY},
           {"AOE_ALL", mir2::common::SkillTarget::AOE_ALL}},
          out)) {
    SetError(error_out, std::string("skills field ") + key + " invalid");
    return false;
  }
  return true;
}

bool ReadRequiredAmuletType(const json& node,
                            const char* key,
                            mir2::common::AmuletType* out,
                            std::string* error_out) {
  if (!node.contains(key)) {
    SetError(error_out, std::string("skills entry missing required field ") + key);
    return false;
  }
  if (!ParseEnumByStringOrInt(
          node.at(key),
          {{"NONE", mir2::common::AmuletType::NONE},
           {"HOLY", mir2::common::AmuletType::HOLY},
           {"POISON", mir2::common::AmuletType::POISON},
           {"FIRE", mir2::common::AmuletType::FIRE},
           {"ICE", mir2::common::AmuletType::ICE}},
          out)) {
    SetError(error_out, std::string("skills field ") + key + " invalid");
    return false;
  }
  return true;
}

bool ParseSkillTemplate(const json& skill_json,
                        mir2::ecs::SkillTemplate* skill_out,
                        std::string* error_out) {
  if (skill_out == nullptr) {
    SetError(error_out, "skills output must not be null");
    return false;
  }

  mir2::ecs::SkillTemplate skill;
  if (!ReadRequiredScalar(skill_json, "id", &skill.id, error_out) || skill.id == 0) {
    if (error_out != nullptr && error_out->empty()) {
      *error_out = "skills entry has invalid id=0";
    }
    return false;
  }
  if (!ReadRequiredString(skill_json, "name", &skill.name, error_out)) {
    return false;
  }
  if (!ReadRequiredString(skill_json, "description", &skill.description, error_out)) {
    return false;
  }
  if (!ReadRequiredCharacterClass(skill_json, "required_class", &skill.required_class, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "required_level", &skill.required_level, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "max_level", &skill.max_level, error_out)) {
    return false;
  }
  if (!ReadRequiredArray(skill_json, "train_level_req", &skill.train_level_req, error_out)) {
    return false;
  }
  if (!ReadRequiredArray(skill_json, "train_points_req", &skill.train_points_req, error_out)) {
    return false;
  }
  if (!ReadRequiredSkillType(skill_json, "skill_type", &skill.skill_type, error_out)) {
    return false;
  }
  if (!ReadRequiredSkillTarget(skill_json, "target_type", &skill.target_type, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "is_universal", &skill.is_universal, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "is_passive", &skill.is_passive, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "mp_cost", &skill.mp_cost, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json,
                          "consumes_talisman",
                          &skill.consumes_talisman,
                          error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "talisman_cost", &skill.talisman_cost, error_out)) {
    return false;
  }
  if (!ReadRequiredAmuletType(skill_json, "required_amulet", &skill.required_amulet, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "amulet_cost", &skill.amulet_cost, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "cooldown_ms", &skill.cooldown_ms, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "cast_time_ms", &skill.cast_time_ms, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json,
                          "can_be_interrupted",
                          &skill.can_be_interrupted,
                          error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "range", &skill.range, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "aoe_radius", &skill.aoe_radius, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "min_power", &skill.min_power, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "max_power", &skill.max_power, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "def_power", &skill.def_power, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "def_max_power", &skill.def_max_power, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "train_lv", &skill.train_lv, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "duration_ms", &skill.duration_ms, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "stat_modifier", &skill.stat_modifier, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "dot_damage", &skill.dot_damage, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "dot_interval_ms", &skill.dot_interval_ms, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "effect_type", &skill.effect_type, error_out)) {
    return false;
  }
  if (!ReadRequiredScalar(skill_json, "effect_id", &skill.effect_id, error_out)) {
    return false;
  }
  if (!ReadRequiredString(skill_json, "animation_id", &skill.animation_id, error_out)) {
    return false;
  }
  if (!ReadRequiredString(skill_json, "sound_id", &skill.sound_id, error_out)) {
    return false;
  }

  *skill_out = std::move(skill);
  return true;
}

bool LoadSkills(const std::string& content,
                std::unordered_map<uint32_t, mir2::ecs::SkillTemplate>* skills_out,
                std::string* error_out) {
  if (skills_out == nullptr) {
    SetError(error_out, "skills output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse skills json: ") + ex.what());
    return false;
  }

  if (!root.contains("skills") || !root["skills"].is_array()) {
    SetError(error_out, "skills json missing skills array");
    return false;
  }

  skills_out->clear();
  for (const auto& skill_json : root["skills"]) {
    if (!skill_json.is_object()) {
      SetError(error_out, "skills entry must be an object");
      return false;
    }

    mir2::ecs::SkillTemplate skill;
    if (!ParseSkillTemplate(skill_json, &skill, error_out)) {
      return false;
    }

    const auto [it, inserted] = skills_out->emplace(skill.id, std::move(skill));
    if (!inserted) {
      SetError(error_out, "duplicate skill id " + std::to_string(it->first));
      return false;
    }
  }

  return true;
}

template <typename T>
bool ReadJsonNumber(const json& node,
                    const char* key,
                    T* out,
                    std::string* error_out,
                    const char* domain_name) {
  if (!node.contains(key)) {
    SetError(error_out,
             std::string(domain_name) + " entry missing required field " + key);
    return false;
  }
  try {
    *out = node.at(key).get<T>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string(domain_name) + " field " + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

bool ReadJsonString(const json& node,
                    const char* key,
                    std::string* out,
                    std::string* error_out,
                    const char* domain_name) {
  if (!node.contains(key)) {
    SetError(error_out,
             std::string(domain_name) + " entry missing required field " + key);
    return false;
  }
  try {
    *out = node.at(key).get<std::string>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string(domain_name) + " field " + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

bool ReadJsonBoolWithDefault(const json& node,
                             const char* key,
                             bool default_value,
                             bool* out,
                             std::string* error_out,
                             const char* domain_name) {
  if (!node.contains(key)) {
    *out = default_value;
    return true;
  }
  try {
    *out = node.at(key).get<bool>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string(domain_name) + " field " + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

template <typename T>
bool ReadJsonNumberWithDefault(const json& node,
                               const char* key,
                               T default_value,
                               T* out,
                               std::string* error_out,
                               const char* domain_name) {
  if (!node.contains(key)) {
    *out = default_value;
    return true;
  }
  try {
    *out = node.at(key).get<T>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string(domain_name) + " field " + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

bool ReadJsonStringWithDefault(const json& node,
                               const char* key,
                               const std::string& default_value,
                               std::string* out,
                               std::string* error_out,
                               const char* domain_name) {
  if (!node.contains(key)) {
    *out = default_value;
    return true;
  }
  try {
    *out = node.at(key).get<std::string>();
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string(domain_name) + " field " + key + " invalid: " + ex.what());
    return false;
  }
  return true;
}

bool ParseFixes(const json& node,
                std::vector<std::pair<int32_t, int32_t>>* fixes_out,
                std::string* error_out) {
  if (!node.is_array()) {
    SetError(error_out, "maps field fixes must be an array");
    return false;
  }

  fixes_out->clear();
  for (const auto& item : node) {
    if (!item.is_object()) {
      SetError(error_out, "maps fixes entries must be objects");
      return false;
    }
    int32_t x = 0;
    int32_t y = 0;
    if (!ReadJsonNumber(item, "x", &x, error_out, "maps fixes") ||
        !ReadJsonNumber(item, "y", &y, error_out, "maps fixes")) {
      return false;
    }
    fixes_out->emplace_back(x, y);
  }
  return true;
}

bool ParseQuestRequirements(
    const json& node,
    std::vector<mir2::game::map::QuestRequirement>* requirements_out,
    std::string* error_out) {
  if (!node.is_array()) {
    SetError(error_out, "maps field quest_requirements must be an array");
    return false;
  }

  requirements_out->clear();
  for (const auto& item : node) {
    if (!item.is_object()) {
      SetError(error_out, "maps quest_requirements entries must be objects");
      return false;
    }
    mir2::game::map::QuestRequirement requirement;
    if (!ReadJsonNumber(item,
                        "quest_id",
                        &requirement.quest_id,
                        error_out,
                        "maps quest_requirements") ||
        !ReadJsonNumber(item,
                        "quest_value",
                        &requirement.quest_value,
                        error_out,
                        "maps quest_requirements")) {
      return false;
    }
    requirements_out->push_back(requirement);
  }
  return true;
}

bool ParseSafeZones(const json& node,
                    std::vector<mir2::game::map::SafeZone>* safe_zones_out,
                    std::string* error_out) {
  if (!node.is_array()) {
    SetError(error_out, "maps field safe_zones must be an array");
    return false;
  }

  safe_zones_out->clear();
  for (const auto& item : node) {
    if (!item.is_object()) {
      SetError(error_out, "maps safe_zones entries must be objects");
      return false;
    }
    mir2::game::map::SafeZone zone;
    if (!ReadJsonNumber(item, "x", &zone.x, error_out, "maps safe_zones") ||
        !ReadJsonNumber(item, "y", &zone.y, error_out, "maps safe_zones") ||
        !ReadJsonNumber(item,
                        "radius",
                        &zone.radius,
                        error_out,
                        "maps safe_zones")) {
      return false;
    }
    safe_zones_out->push_back(zone);
  }
  return true;
}

bool ParseMapAttributes(const json& map_json,
                        mir2::game::map::MapAttributes* attributes_out,
                        std::string* error_out) {
  auto& attrs = *attributes_out;
  if (!ReadJsonBoolWithDefault(
          map_json, "is_safe_zone", attrs.is_safe_zone, &attrs.is_safe_zone, error_out, "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "is_pk_zone", attrs.is_pk_zone, &attrs.is_pk_zone, error_out, "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "no_teleport", attrs.no_teleport, &attrs.no_teleport, error_out, "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "no_drug", attrs.no_drug, &attrs.no_drug, error_out, "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "is_dark_map", attrs.is_dark_map, &attrs.is_dark_map, error_out, "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "no_recall", attrs.no_recall, &attrs.no_recall, error_out, "maps") ||
      !ReadJsonBoolWithDefault(map_json,
                               "no_random_move",
                               attrs.no_random_move,
                               &attrs.no_random_move,
                               error_out,
                               "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "fight_zone", attrs.fight_zone, &attrs.fight_zone, error_out, "maps") ||
      !ReadJsonBoolWithDefault(
          map_json, "fight3_zone", attrs.fight3_zone, &attrs.fight3_zone, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "min_level", attrs.min_level, &attrs.min_level, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "max_level", attrs.max_level, &attrs.max_level, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "mine_map", attrs.mine_map, &attrs.mine_map, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "dark_level", attrs.dark_level, &attrs.dark_level, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "exp_rate", attrs.exp_rate, &attrs.exp_rate, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "drop_rate", attrs.drop_rate, &attrs.drop_rate, error_out, "maps") ||
      !ReadJsonStringWithDefault(
          map_json, "home_map", attrs.home_map, &attrs.home_map, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "home_x", attrs.home_x, &attrs.home_x, error_out, "maps") ||
      !ReadJsonNumberWithDefault(
          map_json, "home_y", attrs.home_y, &attrs.home_y, error_out, "maps") ||
      !ReadJsonStringWithDefault(map_json,
                                 "pk_village_map",
                                 attrs.pk_village_map,
                                 &attrs.pk_village_map,
                                 error_out,
                                 "maps") ||
      !ReadJsonNumberWithDefault(map_json,
                                 "pk_village_x",
                                 attrs.pk_village_x,
                                 &attrs.pk_village_x,
                                 error_out,
                                 "maps") ||
      !ReadJsonNumberWithDefault(map_json,
                                 "pk_village_y",
                                 attrs.pk_village_y,
                                 &attrs.pk_village_y,
                                 error_out,
                                 "maps")) {
    return false;
  }

  if (map_json.contains("quest_requirements") &&
      !ParseQuestRequirements(map_json.at("quest_requirements"),
                              &attrs.quest_requirements,
                              error_out)) {
    return false;
  }
  if (map_json.contains("safe_zones") &&
      !ParseSafeZones(map_json.at("safe_zones"), &attrs.safe_zones, error_out)) {
    return false;
  }
  return true;
}

bool LoadMaps(const std::string& content,
              std::unordered_map<int32_t, ConfigData::RuntimeMapConfig>* maps_out,
              std::string* error_out) {
  if (maps_out == nullptr) {
    SetError(error_out, "maps output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse maps json: ") + ex.what());
    return false;
  }

  if (!root.contains("maps") || !root["maps"].is_array()) {
    SetError(error_out, "maps json missing maps array");
    return false;
  }

  maps_out->clear();
  for (const auto& map_json : root["maps"]) {
    if (!map_json.is_object()) {
      SetError(error_out, "maps entry must be an object");
      return false;
    }

    ConfigData::RuntimeMapConfig map_config;
    if (!ReadJsonNumber(map_json, "map_id", &map_config.map_id, error_out, "maps")) {
      return false;
    }
    if (map_config.map_id <= 0) {
      SetError(error_out, "maps entry has invalid map_id");
      return false;
    }
    if (!ParseMapAttributes(map_json, &map_config.attributes, error_out)) {
      return false;
    }
    const json empty_fixes = json::array();
    if (!ParseFixes(map_json.value("fixes", empty_fixes), &map_config.fixes, error_out)) {
      return false;
    }

    const auto [it, inserted] = maps_out->emplace(map_config.map_id, std::move(map_config));
    if (!inserted) {
      SetError(error_out, "duplicate map_id " + std::to_string(it->first));
      return false;
    }
  }
  return true;
}

bool LoadGates(const std::string& content,
               std::vector<mir2::game::map::GateInfo>* gates_out,
               std::string* error_out) {
  if (gates_out == nullptr) {
    SetError(error_out, "gates output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse gates json: ") + ex.what());
    return false;
  }

  if (!root.contains("gates") || !root["gates"].is_array()) {
    SetError(error_out, "gates json missing gates array");
    return false;
  }

  gates_out->clear();
  std::unordered_map<uint32_t, std::string> gate_ids;
  std::unordered_map<std::string, uint32_t> source_coords;
  for (const auto& gate_json : root["gates"]) {
    if (!gate_json.is_object()) {
      SetError(error_out, "gates entry must be an object");
      return false;
    }

    mir2::game::map::GateInfo gate;
    if (!ReadJsonNumber(gate_json, "gate_id", &gate.gate_id, error_out, "gates") ||
        !ReadJsonString(gate_json, "source_map", &gate.source_map, error_out, "gates") ||
        !ReadJsonNumber(gate_json, "source_x", &gate.source_x, error_out, "gates") ||
        !ReadJsonNumber(gate_json, "source_y", &gate.source_y, error_out, "gates") ||
        !ReadJsonString(gate_json, "target_map", &gate.target_map, error_out, "gates") ||
        !ReadJsonNumber(gate_json, "target_x", &gate.target_x, error_out, "gates") ||
        !ReadJsonNumber(gate_json, "target_y", &gate.target_y, error_out, "gates") ||
        !ReadJsonBoolWithDefault(
            gate_json, "require_item", false, &gate.require_item, error_out, "gates") ||
        !ReadJsonNumberWithDefault(gate_json,
                                   "required_item_id",
                                   0,
                                   &gate.required_item_id,
                                   error_out,
                                   "gates")) {
      return false;
    }

    if (gate.gate_id == 0) {
      SetError(error_out, "gates entry has invalid gate_id");
      return false;
    }
    if (gate.source_map.empty() || gate.target_map.empty()) {
      SetError(error_out, "gates entry requires source_map and target_map");
      return false;
    }
    if (gate.require_item && gate.required_item_id <= 0) {
      SetError(error_out,
               "gates required_item_id must be positive when require_item=true");
      return false;
    }
    if (!gate.require_item && gate.required_item_id != 0) {
      SetError(error_out,
               "gates required_item_id must be 0 when require_item=false");
      return false;
    }
    if (!gate_ids.emplace(gate.gate_id, gate.source_map).second) {
      SetError(error_out, "duplicate gate_id " + std::to_string(gate.gate_id));
      return false;
    }

    const std::string coord_key = gate.source_map + ":" +
                                  std::to_string(gate.source_x) + ":" +
                                  std::to_string(gate.source_y);
    if (!source_coords.emplace(coord_key, gate.gate_id).second) {
      SetError(error_out, "duplicate gate source coordinate " + coord_key);
      return false;
    }
    gates_out->push_back(std::move(gate));
  }

  std::sort(gates_out->begin(),
            gates_out->end(),
            [](const auto& lhs, const auto& rhs) { return lhs.gate_id < rhs.gate_id; });
  return true;
}

bool LoadDrops(
    const std::string& content,
    std::unordered_map<uint32_t, mir2::game::entity::MonsterDropTable>* tables_out,
    std::string* error_out) {
  if (tables_out == nullptr) {
    SetError(error_out, "drops output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse drops json: ") + ex.what());
    return false;
  }

  if (!root.contains("drop_tables") || !root["drop_tables"].is_array()) {
    SetError(error_out, "drops json missing drop_tables array");
    return false;
  }

  tables_out->clear();
  for (const auto& table_json : root["drop_tables"]) {
    if (!table_json.is_object()) {
      SetError(error_out, "drops table entry must be an object");
      return false;
    }

    mir2::game::entity::MonsterDropTable table;
    if (!ReadJsonNumber(table_json,
                        "monster_template_id",
                        &table.monster_template_id,
                        error_out,
                        "drops")) {
      return false;
    }
    if (table.monster_template_id == 0) {
      SetError(error_out, "drops entry has invalid monster_template_id");
      return false;
    }
    if (!table_json.contains("items") || !table_json.at("items").is_array()) {
      SetError(error_out, "drops table items must be an array");
      return false;
    }

    std::unordered_map<uint32_t, bool> item_ids;
    for (const auto& item_json : table_json.at("items")) {
      if (!item_json.is_object()) {
        SetError(error_out, "drops item entry must be an object");
        return false;
      }

      mir2::game::entity::DropItem item;
      if (!ReadJsonNumber(item_json, "item_id", &item.item_id, error_out, "drops") ||
          !ReadJsonNumber(item_json, "drop_rate", &item.drop_rate, error_out, "drops") ||
          !ReadJsonNumber(item_json, "min_count", &item.min_count, error_out, "drops") ||
          !ReadJsonNumber(item_json, "max_count", &item.max_count, error_out, "drops") ||
          !ReadJsonNumberWithDefault(
              item_json, "rarity", item.rarity, &item.rarity, error_out, "drops") ||
          !ReadJsonNumberWithDefault(item_json,
                                     "boss_bonus",
                                     item.boss_bonus,
                                     &item.boss_bonus,
                                     error_out,
                                     "drops")) {
        return false;
      }

      if (item.item_id == 0) {
        SetError(error_out, "drops item_id must be > 0");
        return false;
      }
      if (item.drop_rate < 0.0f || item.drop_rate > 1.0f) {
        SetError(error_out, "drops drop_rate must be in [0,1]");
        return false;
      }
      if (item.min_count < 0 || item.max_count < 0 || item.min_count > item.max_count) {
        SetError(
            error_out,
            "drops min_count and max_count must be non-negative with min_count <= max_count");
        return false;
      }
      if (!item_ids.emplace(item.item_id, true).second) {
        SetError(error_out,
                 "duplicate drop entry monster_template_id=" +
                     std::to_string(table.monster_template_id) +
                     " item_id=" + std::to_string(item.item_id));
        return false;
      }
      table.items.push_back(item);
    }

    std::sort(table.items.begin(),
              table.items.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.item_id < rhs.item_id; });
    const auto [it, inserted] =
        tables_out->emplace(table.monster_template_id, std::move(table));
    if (!inserted) {
      SetError(error_out,
               "duplicate monster_template_id " +
                   std::to_string(it->first) + " in drops");
      return false;
    }
  }
  return true;
}

bool LoadShops(const std::string& content,
               std::unordered_map<uint32_t, mir2::logic::ShopConfig>* shops_out,
               std::string* error_out) {
  if (shops_out == nullptr) {
    SetError(error_out, "shops output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse shops json: ") + ex.what());
    return false;
  }

  if (!root.contains("shops") || !root["shops"].is_array()) {
    SetError(error_out, "shops json missing shops array");
    return false;
  }

  shops_out->clear();
  for (const auto& shop_json : root["shops"]) {
    if (!shop_json.is_object()) {
      SetError(error_out, "shops entry must be an object");
      return false;
    }

    mir2::logic::ShopConfig shop;
    if (!ReadJsonNumber(shop_json, "store_id", &shop.store_id, error_out, "shops") ||
        !ReadJsonString(shop_json, "name", &shop.name, error_out, "shops") ||
        !ReadJsonNumber(shop_json, "buy_rate", &shop.buy_rate, error_out, "shops") ||
        !ReadJsonNumber(shop_json, "sell_rate", &shop.sell_rate, error_out, "shops")) {
      return false;
    }

    if (shop.store_id == 0) {
      SetError(error_out, "shops entry has invalid store_id");
      return false;
    }
    if (!shop_json.contains("items") || !shop_json.at("items").is_array()) {
      SetError(error_out, "shops items must be an array");
      return false;
    }

    std::unordered_map<uint32_t, bool> item_ids;
    for (const auto& item_json : shop_json.at("items")) {
      if (!item_json.is_object()) {
        SetError(error_out, "shops item entry must be an object");
        return false;
      }

      mir2::logic::ShopItem item;
      if (!ReadJsonNumber(item_json, "item_id", &item.item_id, error_out, "shops") ||
          !ReadJsonNumber(item_json, "price", &item.price, error_out, "shops") ||
          !ReadJsonNumber(item_json, "stock", &item.stock, error_out, "shops")) {
        return false;
      }
      if (item.item_id == 0) {
        SetError(error_out, "shops item_id must be > 0");
        return false;
      }
      if (item.price < 0) {
        SetError(error_out, "shops price must be >= 0");
        return false;
      }
      if (item.stock != -1 && item.stock < 0) {
        SetError(error_out, "shops stock must be -1 or >= 0");
        return false;
      }
      if (!item_ids.emplace(item.item_id, true).second) {
        SetError(error_out,
                 "duplicate shop item entry store_id=" +
                     std::to_string(shop.store_id) +
                     " item_id=" + std::to_string(item.item_id));
        return false;
      }
      shop.items.push_back(item);
    }

    std::sort(shop.items.begin(),
              shop.items.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.item_id < rhs.item_id; });
    const auto [it, inserted] = shops_out->emplace(shop.store_id, std::move(shop));
    if (!inserted) {
      SetError(error_out, "duplicate store_id " + std::to_string(it->first));
      return false;
    }
  }

  return true;
}

bool LoadMonsterSpawns(
    const std::string& content,
    std::unordered_map<uint32_t, mir2::game::entity::MonsterSpawnPoint>* spawn_points_out,
    std::string* error_out) {
  if (spawn_points_out == nullptr) {
    SetError(error_out, "monster_spawns output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out,
             std::string("failed to parse monster_spawns json: ") + ex.what());
    return false;
  }

  if (!root.contains("spawn_points") || !root["spawn_points"].is_array()) {
    SetError(error_out, "monster_spawns json missing spawn_points array");
    return false;
  }

  spawn_points_out->clear();
  for (const auto& spawn_json : root["spawn_points"]) {
    if (!spawn_json.is_object()) {
      SetError(error_out, "monster_spawns entry must be an object");
      return false;
    }

    mir2::game::entity::MonsterSpawnPoint spawn;
    if (!ReadJsonNumber(spawn_json, "spawn_id", &spawn.spawn_id, error_out, "monster_spawns") ||
        !ReadJsonNumber(spawn_json, "map_id", &spawn.map_id, error_out, "monster_spawns") ||
        !ReadJsonNumber(spawn_json, "center_x", &spawn.center_x, error_out, "monster_spawns") ||
        !ReadJsonNumber(spawn_json, "center_y", &spawn.center_y, error_out, "monster_spawns") ||
        !ReadJsonNumber(
            spawn_json, "spawn_radius", &spawn.spawn_radius, error_out, "monster_spawns") ||
        !ReadJsonNumber(spawn_json,
                        "monster_template_id",
                        &spawn.monster_template_id,
                        error_out,
                        "monster_spawns") ||
        !ReadJsonNumber(spawn_json,
                        "patrol_radius",
                        &spawn.patrol_radius,
                        error_out,
                        "monster_spawns") ||
        !ReadJsonNumber(spawn_json,
                        "respawn_interval",
                        &spawn.respawn_interval,
                        error_out,
                        "monster_spawns") ||
        !ReadJsonNumber(
            spawn_json, "max_count", &spawn.max_count, error_out, "monster_spawns") ||
        !ReadJsonNumber(
            spawn_json, "aggro_range", &spawn.aggro_range, error_out, "monster_spawns") ||
        !ReadJsonNumber(
            spawn_json, "attack_range", &spawn.attack_range, error_out, "monster_spawns")) {
      return false;
    }

    if (spawn.spawn_id == 0) {
      SetError(error_out, "monster_spawns spawn_id must be > 0");
      return false;
    }
    if (spawn.map_id == 0) {
      SetError(error_out, "monster_spawns map_id must be > 0");
      return false;
    }
    if (spawn.monster_template_id == 0) {
      SetError(error_out, "monster_spawns monster_template_id must be > 0");
      return false;
    }
    if (spawn.spawn_radius < 0) {
      SetError(error_out, "monster_spawns spawn_radius must be >= 0");
      return false;
    }
    if (spawn.patrol_radius < 0) {
      SetError(error_out, "monster_spawns patrol_radius must be >= 0");
      return false;
    }
    if (spawn.respawn_interval < 0.0f) {
      SetError(error_out, "monster_spawns respawn_interval must be >= 0");
      return false;
    }
    if (spawn.max_count <= 0) {
      SetError(error_out, "monster_spawns max_count must be > 0");
      return false;
    }

    const auto [it, inserted] =
        spawn_points_out->emplace(spawn.spawn_id, std::move(spawn));
    if (!inserted) {
      SetError(error_out, "duplicate spawn_id " + std::to_string(it->first));
      return false;
    }
  }

  return true;
}

bool LoadNpcs(const std::string& content,
              std::unordered_map<uint64_t, mir2::game::npc::NpcConfig>* npcs_out,
              std::string* error_out) {
  if (npcs_out == nullptr) {
    SetError(error_out, "npcs output must not be null");
    return false;
  }

  json root;
  try {
    root = json::parse(content);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse npcs json: ") + ex.what());
    return false;
  }

  if (!root.contains("npcs") || !root["npcs"].is_array()) {
    SetError(error_out, "npcs json missing npcs array");
    return false;
  }

  npcs_out->clear();
  for (const auto& npc_json : root["npcs"]) {
    if (!npc_json.is_object()) {
      SetError(error_out, "npcs entry must be an object");
      return false;
    }

    mir2::game::npc::NpcConfig npc;
    int32_t direction = 0;
    if (!ReadJsonNumber(npc_json, "npc_id", &npc.id, error_out, "npcs") ||
        !ReadJsonNumber(
            npc_json, "template_id", &npc.template_id, error_out, "npcs") ||
        !ReadJsonString(npc_json, "name", &npc.name, error_out, "npcs") ||
        !ReadJsonNumber(npc_json, "map_id", &npc.map_id, error_out, "npcs") ||
        !ReadJsonNumber(npc_json, "x", &npc.x, error_out, "npcs") ||
        !ReadJsonNumber(npc_json, "y", &npc.y, error_out, "npcs") ||
        !ReadJsonNumber(npc_json, "direction", &direction, error_out, "npcs") ||
        !ReadJsonNumber(npc_json, "store_id", &npc.store_id, error_out, "npcs")) {
      return false;
    }

    if (!npc_json.contains("enabled")) {
      SetError(error_out, "npcs entry missing required field enabled");
      return false;
    }
    try {
      npc.enabled = npc_json.at("enabled").get<bool>();
    } catch (const std::exception& ex) {
      SetError(error_out, std::string("npcs field enabled invalid: ") + ex.what());
      return false;
    }

    if (!npc_json.contains("type")) {
      SetError(error_out, "npcs entry missing required field type");
      return false;
    }
    if (!ParseEnumByStringOrInt(
            npc_json.at("type"),
            {{"MERCHANT", mir2::game::npc::NpcType::kMerchant}},
            &npc.type)) {
      SetError(error_out, "npcs field type must be MERCHANT");
      return false;
    }

    if (npc.id == 0) {
      SetError(error_out, "npcs npc_id must be > 0");
      return false;
    }
    if (npc.template_id == 0) {
      SetError(error_out, "npcs template_id must be > 0");
      return false;
    }
    if (npc.map_id == 0) {
      SetError(error_out, "npcs map_id must be > 0");
      return false;
    }
    if (npc.x < 0 || npc.y < 0) {
      SetError(error_out, "npcs coordinates must be >= 0");
      return false;
    }
    if (direction < 0 || direction > 7) {
      SetError(error_out, "npcs direction must be in [0,7]");
      return false;
    }
    if (npc.store_id == 0) {
      SetError(error_out, "npcs store_id must be > 0");
      return false;
    }
    npc.direction = static_cast<uint8_t>(direction);

    const auto [it, inserted] = npcs_out->emplace(npc.id, std::move(npc));
    if (!inserted) {
      SetError(error_out, "duplicate npc_id " + std::to_string(it->first));
      return false;
    }
  }

  return true;
}

}  // namespace

bool ConfigManifest::HasArtifact(std::string_view name) const {
  return FindArtifact(name) != nullptr;
}

const ConfigArtifact* ConfigManifest::FindArtifact(std::string_view name) const {
  const auto it = std::find_if(
      artifacts.begin(), artifacts.end(), [name](const ConfigArtifact& artifact) {
        return artifact.name == name;
      });
  return it != artifacts.end() ? &(*it) : nullptr;
}

const mir2::data::ItemTemplate* ConfigData::FindItem(uint32_t item_id) const {
  const auto it = items.find(item_id);
  return it != items.end() ? &it->second : nullptr;
}

const mir2::ecs::SkillTemplate* ConfigData::FindSkill(uint32_t skill_id) const {
  const auto it = skills.find(skill_id);
  return it != skills.end() ? &it->second : nullptr;
}

std::optional<ConfigData> ConfigBundleLoader::LoadFromRuntimeDir(
    const std::filesystem::path& runtime_dir,
    std::string* error_out) {
  const auto manifest_text = ReadFile(runtime_dir / "manifest.json", error_out);
  if (!manifest_text.has_value()) {
    return std::nullopt;
  }

  json manifest_json;
  try {
    manifest_json = json::parse(*manifest_text);
  } catch (const std::exception& ex) {
    SetError(error_out, std::string("failed to parse manifest json: ") + ex.what());
    return std::nullopt;
  }

  auto manifest = ParseManifest(manifest_json, error_out);
  if (!manifest.has_value()) {
    return std::nullopt;
  }

  ConfigData data;
  data.manifest = *manifest;

  std::string items_text;
  const auto* items_artifact = manifest->FindArtifact("items");
  if (!VerifyArtifact(runtime_dir, *items_artifact, &items_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadItems(items_text, &data.items, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*items_artifact, data.items.size(), error_out)) {
    return std::nullopt;
  }

  const auto* skills_artifact = manifest->FindArtifact("skills");
  std::string skills_text;
  if (!VerifyArtifact(runtime_dir, *skills_artifact, &skills_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadSkills(skills_text, &data.skills, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*skills_artifact, data.skills.size(), error_out)) {
    return std::nullopt;
  }

  const auto* maps_artifact = manifest->FindArtifact("maps");
  std::string maps_text;
  if (!VerifyArtifact(runtime_dir, *maps_artifact, &maps_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadMaps(maps_text, &data.maps, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*maps_artifact, data.maps.size(), error_out)) {
    return std::nullopt;
  }

  const auto* gates_artifact = manifest->FindArtifact("gates");
  std::string gates_text;
  if (!VerifyArtifact(runtime_dir, *gates_artifact, &gates_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadGates(gates_text, &data.gates, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*gates_artifact, data.gates.size(), error_out)) {
    return std::nullopt;
  }

  const auto* drops_artifact = manifest->FindArtifact("drops");
  std::string drops_text;
  if (!VerifyArtifact(runtime_dir, *drops_artifact, &drops_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadDrops(drops_text, &data.drop_tables, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*drops_artifact, data.drop_tables.size(), error_out)) {
    return std::nullopt;
  }

  const auto* shops_artifact = manifest->FindArtifact("shops");
  std::string shops_text;
  if (!VerifyArtifact(runtime_dir, *shops_artifact, &shops_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadShops(shops_text, &data.shops, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*shops_artifact, data.shops.size(), error_out)) {
    return std::nullopt;
  }

  const auto* spawns_artifact = manifest->FindArtifact("monster_spawns");
  std::string spawns_text;
  if (!VerifyArtifact(runtime_dir, *spawns_artifact, &spawns_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadMonsterSpawns(spawns_text, &data.monster_spawn_points, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*spawns_artifact, data.monster_spawn_points.size(), error_out)) {
    return std::nullopt;
  }

  const auto* npcs_artifact = manifest->FindArtifact("npcs");
  std::string npcs_text;
  if (!VerifyArtifact(runtime_dir, *npcs_artifact, &npcs_text, error_out)) {
    return std::nullopt;
  }
  if (!LoadNpcs(npcs_text, &data.npcs, error_out)) {
    return std::nullopt;
  }
  if (!VerifyRowCount(*npcs_artifact, data.npcs.size(), error_out)) {
    return std::nullopt;
  }

  return data;
}

std::shared_ptr<const ConfigData> ConfigStore::GetSnapshot() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return snapshot_;
}

bool ConfigStore::ReloadFromRuntimeDir(const std::filesystem::path& runtime_dir,
                                       std::string* error_out) {
  auto loaded = ConfigBundleLoader::LoadFromRuntimeDir(runtime_dir, error_out);
  if (!loaded.has_value()) {
    return false;
  }

  auto next_snapshot = std::make_shared<ConfigData>(std::move(*loaded));
  std::unique_lock<std::shared_mutex> lock(mutex_);
  snapshot_ = std::move(next_snapshot);
  return true;
}

}  // namespace mir2::config
