#include <gtest/gtest.h>

#include "client/game/skill/skill_executor.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace mir2::client::skill {
namespace {

constexpr uint32_t kSkillId = 1001;

int64_t NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock::now().time_since_epoch())
      .count();
}

std::string CreateTempSkillsYaml() {
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                    ("skill_executor_test_" + std::to_string(timestamp) + ".yaml");
  std::ofstream output(path);
  output << R"(
skills:
  - id: 1001
    name: Test Skill
    mp_cost: 10
    cooldown_ms: 0
    cast_time_ms: 0
    target_type: self
)";
  output.close();
  return path.string();
}

ClientLearnedSkill MakeLearnedSkill(uint32_t skill_id = kSkillId) {
  ClientLearnedSkill learned{};
  learned.skill_id = skill_id;
  learned.level = 1;
  return learned;
}

}  // namespace

class SkillExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    yaml_path_ = CreateTempSkillsYaml();
    ASSERT_TRUE(manager_.load_templates_from_yaml(yaml_path_));
  }

  void TearDown() override {
    if (!yaml_path_.empty()) {
      std::filesystem::remove(yaml_path_);
    }
  }

  SkillManager manager_;
  std::string yaml_path_;
};

TEST_F(SkillExecutorTest, UseSkillSuccess) {
  ASSERT_TRUE(manager_.add_learned_skill(MakeLearnedSkill()));
  SkillExecutor executor(manager_);

  const auto result = executor.try_use_skill(kSkillId, 50, 0);
  EXPECT_EQ(result, SkillUseResult::Success);
}

TEST_F(SkillExecutorTest, UseSkillNotLearned) {
  SkillExecutor executor(manager_);

  const auto result = executor.try_use_skill(9999, 50, 0);
  EXPECT_EQ(result, SkillUseResult::NotLearned);
}

TEST_F(SkillExecutorTest, UseSkillOnCooldown) {
  ASSERT_TRUE(manager_.add_learned_skill(MakeLearnedSkill()));
  const int64_t now = NowMs();
  manager_.start_cooldown(kSkillId, 60000, now);

  SkillExecutor executor(manager_);
  const auto result = executor.try_use_skill(kSkillId, 50, 0);
  EXPECT_EQ(result, SkillUseResult::OnCooldown);
}

TEST_F(SkillExecutorTest, UseSkillNotEnoughMP) {
  ASSERT_TRUE(manager_.add_learned_skill(MakeLearnedSkill()));
  SkillExecutor executor(manager_);

  const auto result = executor.try_use_skill(kSkillId, 1, 0);
  EXPECT_EQ(result, SkillUseResult::NotEnoughMP);
}

TEST_F(SkillExecutorTest, UseSkillAlreadyCasting) {
  ASSERT_TRUE(manager_.add_learned_skill(MakeLearnedSkill()));
  manager_.start_casting(kSkillId, 0, 1000, NowMs());

  SkillExecutor executor(manager_);
  const auto result = executor.try_use_skill(kSkillId, 50, 0);
  EXPECT_EQ(result, SkillUseResult::AlreadyCasting);
}

TEST_F(SkillExecutorTest, UseSkillByHotkey) {
  ASSERT_TRUE(manager_.add_learned_skill(MakeLearnedSkill()));
  ASSERT_TRUE(manager_.bind_hotkey(1, kSkillId));

  SkillExecutor executor(manager_);
  uint32_t used_id = 0;
  executor.set_send_callback([&used_id](uint32_t skill_id, uint64_t /*target_id*/) {
    used_id = skill_id;
  });

  const auto result = executor.try_use_skill_by_hotkey(1, 50, 0);
  EXPECT_EQ(result, SkillUseResult::Success);
  EXPECT_EQ(used_id, kSkillId);
}

TEST_F(SkillExecutorTest, SendCallbackInvoked) {
  ASSERT_TRUE(manager_.add_learned_skill(MakeLearnedSkill()));

  SkillExecutor executor(manager_);
  int call_count = 0;
  uint32_t last_skill_id = 0;
  uint64_t last_target_id = 0;
  executor.set_send_callback([&](uint32_t skill_id, uint64_t target_id) {
    ++call_count;
    last_skill_id = skill_id;
    last_target_id = target_id;
  });

  const uint64_t target_id = 42;
  const auto result = executor.try_use_skill(kSkillId, 50, target_id);
  EXPECT_EQ(result, SkillUseResult::Success);
  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(last_skill_id, kSkillId);
  EXPECT_EQ(last_target_id, target_id);
}

}  // namespace mir2::client::skill
