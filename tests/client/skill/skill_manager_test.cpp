#include <gtest/gtest.h>

#include "client/game/skill/skill_manager.h"

namespace mir2::client::skill {

TEST(SkillManagerTest, AddLearnedSkill) {
  SkillManager manager;
  ClientLearnedSkill skill{};
  skill.skill_id = 1;

  EXPECT_TRUE(manager.add_learned_skill(skill));
  EXPECT_TRUE(manager.has_skill(1));
}

TEST(SkillManagerTest, RemoveLearnedSkill) {
  SkillManager manager;
  ClientLearnedSkill skill{};
  skill.skill_id = 2;

  ASSERT_TRUE(manager.add_learned_skill(skill));
  EXPECT_TRUE(manager.remove_learned_skill(2));
  EXPECT_FALSE(manager.has_skill(2));
}

TEST(SkillManagerTest, MaxSkillsLimit) {
  SkillManager manager;

  for (size_t i = 0; i < SkillManager::kMaxSkills; ++i) {
    ClientLearnedSkill skill{};
    skill.skill_id = static_cast<uint32_t>(i + 1);
    EXPECT_TRUE(manager.add_learned_skill(skill));
  }

  ClientLearnedSkill extra{};
  extra.skill_id = static_cast<uint32_t>(SkillManager::kMaxSkills + 1);
  EXPECT_FALSE(manager.add_learned_skill(extra));
}

TEST(SkillManagerTest, HotkeyBinding) {
  SkillManager manager;
  ClientLearnedSkill skill{};
  skill.skill_id = 3;

  ASSERT_TRUE(manager.add_learned_skill(skill));
  EXPECT_TRUE(manager.bind_hotkey(1, 3));
  EXPECT_EQ(manager.get_skill_by_hotkey(1), 3u);

  const auto* learned = manager.get_learned_skill(3);
  ASSERT_NE(learned, nullptr);
  EXPECT_EQ(learned->hotkey, 1);
}

TEST(SkillManagerTest, HotkeyUnbind) {
  SkillManager manager;
  ClientLearnedSkill skill{};
  skill.skill_id = 4;

  ASSERT_TRUE(manager.add_learned_skill(skill));
  ASSERT_TRUE(manager.bind_hotkey(1, 4));
  EXPECT_TRUE(manager.unbind_hotkey(1));
  EXPECT_EQ(manager.get_skill_by_hotkey(1), 0u);

  const auto* learned = manager.get_learned_skill(4);
  ASSERT_NE(learned, nullptr);
  EXPECT_EQ(learned->hotkey, 0);
}

TEST(SkillManagerTest, CooldownTracking) {
  SkillManager manager;
  const uint32_t skill_id = 10;
  const int64_t start_ms = 1000;
  const int64_t duration_ms = 1000;

  manager.start_cooldown(skill_id, duration_ms, start_ms);
  EXPECT_FALSE(manager.is_ready(skill_id, start_ms));
  EXPECT_FALSE(manager.is_ready(skill_id, start_ms + duration_ms - 1));
  EXPECT_TRUE(manager.is_ready(skill_id, start_ms + duration_ms));
}

TEST(SkillManagerTest, GetRemainingCooldown) {
  SkillManager manager;
  const uint32_t skill_id = 11;
  const int64_t start_ms = 100;
  const int64_t duration_ms = 1000;

  manager.start_cooldown(skill_id, duration_ms, start_ms);
  EXPECT_EQ(manager.get_remaining_cooldown_ms(skill_id, start_ms), 1000);
  EXPECT_EQ(manager.get_remaining_cooldown_ms(skill_id, start_ms + 500), 500);
  EXPECT_EQ(manager.get_remaining_cooldown_ms(skill_id, start_ms + 1200), 0);
}

TEST(SkillManagerTest, CastingState) {
  SkillManager manager;
  const uint32_t skill_id = 20;
  const uint64_t target_id = 99;
  const int64_t start_ms = 100;
  const int64_t cast_time_ms = 1000;

  manager.start_casting(skill_id, target_id, cast_time_ms, start_ms);
  EXPECT_TRUE(manager.is_casting());

  const CastingState& state = manager.get_casting_state();
  EXPECT_EQ(state.skill_id, skill_id);
  EXPECT_EQ(state.target_id, target_id);
  EXPECT_FLOAT_EQ(state.get_progress(start_ms), 0.0f);
  EXPECT_NEAR(state.get_progress(start_ms + 500), 0.5f, 0.01f);
  EXPECT_TRUE(state.is_complete(start_ms + cast_time_ms));
}

TEST(SkillManagerTest, CancelCasting) {
  SkillManager manager;
  manager.start_casting(21, 123, 500, 1000);
  manager.cancel_casting();

  EXPECT_FALSE(manager.is_casting());
  const CastingState& state = manager.get_casting_state();
  EXPECT_EQ(state.skill_id, 0u);
  EXPECT_EQ(state.target_id, 0u);
}

}  // namespace mir2::client::skill
