#include <gtest/gtest.h>

#include "config/runtime_config.h"
#include "ecs/skill_registry.h"

namespace mir2::ecs::test {
namespace {

using mir2::config::ConfigArtifact;
using mir2::config::ConfigData;
using mir2::config::ConfigManifest;

SkillTemplate MakeSkillTemplate(uint32_t id,
                                mir2::common::CharacterClass required_class,
                                uint8_t required_level,
                                mir2::common::SkillType skill_type,
                                mir2::common::SkillTarget target_type) {
  SkillTemplate skill;
  skill.id = id;
  skill.name = "skill_" + std::to_string(id);
  skill.required_class = required_class;
  skill.required_level = required_level;
  skill.skill_type = skill_type;
  skill.target_type = target_type;
  skill.description = "desc";
  skill.max_level = 3;
  return skill;
}

ConfigData MakeConfigDataWithSkills(std::initializer_list<SkillTemplate> skills,
                                    bool include_artifact = true) {
  ConfigData data;
  data.manifest.bundle_type = "gameplay";
  data.manifest.schema_version = 1;
  data.manifest.generated_at = "2026-03-07T00:00:00Z";
  if (include_artifact) {
    data.manifest.artifacts.push_back(
        ConfigArtifact{
            .name = "skills",
            .file = "skills.json",
            .hash = "abcdef",
            .row_count = 2,
        });
  }
  for (auto skill : skills) {
    data.skills.emplace(skill.id, std::move(skill));
  }
  return data;
}

class SkillRegistryRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override { SkillRegistry::instance().clear(); }
  void TearDown() override { SkillRegistry::instance().clear(); }
};

TEST_F(SkillRegistryRuntimeTest, ReplaceAllFromConfigDataLoadsSkillsAndReportsMetadata) {
  auto data = MakeConfigDataWithSkills({
      MakeSkillTemplate(3,
                        mir2::common::CharacterClass::WARRIOR,
                        7,
                        mir2::common::SkillType::PHYSICAL,
                        mir2::common::SkillTarget::SINGLE_ENEMY),
      MakeSkillTemplate(11,
                        mir2::common::CharacterClass::MAGE,
                        19,
                        mir2::common::SkillType::MAGICAL,
                        mir2::common::SkillTarget::SINGLE_ENEMY),
  });

  const auto result = SkillRegistry::instance().ReplaceAllFromConfigData(data);

  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_TRUE(result.artifact_present);
  EXPECT_EQ(result.artifact_file, "skills.json");
  EXPECT_EQ(result.artifact_hash, "abcdef");
  EXPECT_EQ(result.generated_at, "2026-03-07T00:00:00Z");
  EXPECT_EQ(result.loaded_skill_count, 2u);
  ASSERT_NE(SkillRegistry::instance().get_skill(3), nullptr);
  ASSERT_NE(SkillRegistry::instance().get_skill(11), nullptr);
  EXPECT_EQ(SkillRegistry::instance().size(), 2u);
}

TEST_F(SkillRegistryRuntimeTest, ReplaceAllFromConfigDataClearsRegistryWhenArtifactMissing) {
  SkillRegistry::instance().register_skill(MakeSkillTemplate(
      3,
      mir2::common::CharacterClass::WARRIOR,
      7,
      mir2::common::SkillType::PHYSICAL,
      mir2::common::SkillTarget::SINGLE_ENEMY));
  ASSERT_EQ(SkillRegistry::instance().size(), 1u);

  ConfigData empty_data = MakeConfigDataWithSkills({}, false);
  const auto result = SkillRegistry::instance().ReplaceAllFromConfigData(empty_data);

  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.artifact_present);
  EXPECT_TRUE(result.artifact_file.empty());
  EXPECT_TRUE(result.artifact_hash.empty());
  EXPECT_EQ(result.loaded_skill_count, 0u);
  EXPECT_EQ(SkillRegistry::instance().size(), 0u);
  EXPECT_EQ(SkillRegistry::instance().get_skill(3), nullptr);
}

TEST_F(SkillRegistryRuntimeTest, ReplaceAllFromConfigDataReplacesExistingSkillsWithoutResidualIds) {
  auto first = MakeConfigDataWithSkills({
      MakeSkillTemplate(3,
                        mir2::common::CharacterClass::WARRIOR,
                        7,
                        mir2::common::SkillType::PHYSICAL,
                        mir2::common::SkillTarget::SINGLE_ENEMY),
      MakeSkillTemplate(4,
                        mir2::common::CharacterClass::WARRIOR,
                        25,
                        mir2::common::SkillType::PHYSICAL,
                        mir2::common::SkillTarget::SINGLE_ENEMY),
  });
  auto second = MakeConfigDataWithSkills({
      MakeSkillTemplate(11,
                        mir2::common::CharacterClass::MAGE,
                        19,
                        mir2::common::SkillType::MAGICAL,
                        mir2::common::SkillTarget::SINGLE_ENEMY),
  });

  ASSERT_TRUE(SkillRegistry::instance().ReplaceAllFromConfigData(first).success);
  ASSERT_TRUE(SkillRegistry::instance().ReplaceAllFromConfigData(second).success);

  EXPECT_EQ(SkillRegistry::instance().get_skill(3), nullptr);
  EXPECT_EQ(SkillRegistry::instance().get_skill(4), nullptr);
  ASSERT_NE(SkillRegistry::instance().get_skill(11), nullptr);
  EXPECT_EQ(SkillRegistry::instance().size(), 1u);
}

TEST_F(SkillRegistryRuntimeTest, ReplaceAllFromConfigDataIsIdempotentForSameSnapshot) {
  auto data = MakeConfigDataWithSkills({
      MakeSkillTemplate(3,
                        mir2::common::CharacterClass::WARRIOR,
                        7,
                        mir2::common::SkillType::PHYSICAL,
                        mir2::common::SkillTarget::SINGLE_ENEMY),
  });

  const auto first = SkillRegistry::instance().ReplaceAllFromConfigData(data);
  const auto second = SkillRegistry::instance().ReplaceAllFromConfigData(data);

  ASSERT_TRUE(first.success);
  ASSERT_TRUE(second.success);
  EXPECT_EQ(first.loaded_skill_count, second.loaded_skill_count);
  EXPECT_EQ(first.generated_at, second.generated_at);
  EXPECT_EQ(SkillRegistry::instance().size(), 1u);
  ASSERT_NE(SkillRegistry::instance().get_skill(3), nullptr);
}

}  // namespace
}  // namespace mir2::ecs::test
