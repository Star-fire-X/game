/**
 * @file effect_broadcaster_test.cc
 * @brief Comprehensive tests for EffectBroadcaster - skill effect broadcast system
 *
 * Test Coverage:
 * - No callback set (no-op, no crash)
 * - No effect_id and no sound_id (early return, callback not invoked)
 * - broadcast_cast_effect (effect_type=1, position from caster)
 * - broadcast_hit_effect overloads (caster_id=0 or provided, position from target)
 * - Entity identity resolution (CharacterIdentityComponent vs static_cast)
 * - Position resolution (CharacterStateComponent vs zero default)
 * - Effect ID resolution (effect_id numeric vs animation_id string)
 * - Duration resolution (cast_time_ms for cast, duration_ms for hit)
 *
 * Priority: P0 Critical (Score: 42)
 * Risk: Incorrect broadcast parameters break client-side visual/audio synchronization
 */

#include <gtest/gtest.h>
#include "ecs/systems/effect_broadcaster.h"
#include "ecs/components/character_components.h"
#include "ecs/components/skill_template_component.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace mir2::ecs::test {
namespace {

/**
 * @brief Captured callback arguments for verification
 */
struct CapturedBroadcast {
    uint64_t caster_id = 0;
    uint64_t target_id = 0;
    uint32_t skill_id = 0;
    uint8_t effect_type = 0;
    std::string effect_id;
    std::string sound_id;
    int x = 0;
    int y = 0;
    uint32_t duration_ms = 0;
};

/**
 * @brief Test fixture for EffectBroadcaster
 *
 * Provides an entt::registry, an EffectBroadcaster instance,
 * and a capture mechanism for broadcast callback arguments.
 */
class EffectBroadcasterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    broadcaster_ = std::make_unique<EffectBroadcaster>(registry_);
    captures_.clear();
  }

  /**
   * @brief Install a callback that records all broadcast invocations
   */
  void InstallCapture() {
    broadcaster_->set_broadcast_callback(
        [this](uint64_t caster_id, uint64_t target_id, uint32_t skill_id,
               uint8_t effect_type, const std::string& effect_id,
               const std::string& sound_id, int x, int y,
               uint32_t duration_ms) {
          CapturedBroadcast cap;
          cap.caster_id = caster_id;
          cap.target_id = target_id;
          cap.skill_id = skill_id;
          cap.effect_type = effect_type;
          cap.effect_id = effect_id;
          cap.sound_id = sound_id;
          cap.x = x;
          cap.y = y;
          cap.duration_ms = duration_ms;
          captures_.push_back(cap);
        });
  }

  /**
   * @brief Create a basic entity with identity and state components
   */
  entt::entity CreateCharacter(uint32_t char_id, int pos_x, int pos_y) {
    auto entity = registry_.create();
    CharacterIdentityComponent identity;
    identity.id = char_id;
    identity.name = "TestChar_" + std::to_string(char_id);
    registry_.emplace<CharacterIdentityComponent>(entity, identity);

    CharacterStateComponent state;
    state.position = {pos_x, pos_y};
    registry_.emplace<CharacterStateComponent>(entity, state);

    return entity;
  }

  /**
   * @brief Create a bare entity with no components
   */
  entt::entity CreateBareEntity() {
    return registry_.create();
  }

  /**
   * @brief Create a skill template with effect_id and sound_id
   */
  SkillTemplate CreateSkill(uint32_t id, uint8_t effect_id,
                            const std::string& animation_id,
                            const std::string& sound_id,
                            int cast_time_ms, int duration_ms) {
    SkillTemplate skill;
    skill.id = id;
    skill.name = "TestSkill_" + std::to_string(id);
    skill.effect_id = effect_id;
    skill.animation_id = animation_id;
    skill.sound_id = sound_id;
    skill.cast_time_ms = cast_time_ms;
    skill.duration_ms = duration_ms;
    skill.min_power = 10;
    skill.max_power = 20;
    skill.def_power = 5;
    skill.train_lv = 1;
    return skill;
  }

  entt::registry registry_;
  std::unique_ptr<EffectBroadcaster> broadcaster_;
  std::vector<CapturedBroadcast> captures_;
};

// ============================================================================
// No-Callback Safety Tests
// ============================================================================

/**
 * @brief Test: No callback set does not crash on broadcast_skill_effect
 */
TEST_F(EffectBroadcasterTest, NoCallbackSetNoCrash) {
  auto caster = CreateCharacter(100, 10, 20);
  auto target = CreateCharacter(200, 30, 40);
  SkillTemplate skill = CreateSkill(1, 5, "", "hit.wav", 0, 1000);

  // No callback installed; should not crash
  EXPECT_NO_THROW(broadcaster_->broadcast_skill_effect(caster, target, skill, 1));
  EXPECT_NO_THROW(broadcaster_->broadcast_cast_effect(caster, skill));
  EXPECT_NO_THROW(broadcaster_->broadcast_hit_effect(target, skill));
  EXPECT_NO_THROW(broadcaster_->broadcast_hit_effect(caster, target, skill));
}

// ============================================================================
// Early-Return Tests (No Effect / No Sound)
// ============================================================================

/**
 * @brief Test: No effect_id and no sound_id causes early return (callback not invoked)
 */
TEST_F(EffectBroadcasterTest, NoEffectIdAndNoSoundIdSkipsBroadcast) {
  InstallCapture();

  auto caster = CreateCharacter(100, 10, 20);
  auto target = CreateCharacter(200, 30, 40);
  // effect_id=0 (resolves to empty string), animation_id="" (empty), sound_id="" (empty)
  SkillTemplate skill = CreateSkill(1, 0, "", "", 0, 1000);

  broadcaster_->broadcast_skill_effect(caster, target, skill, 1);

  EXPECT_TRUE(captures_.empty());
}

// ============================================================================
// broadcast_cast_effect Tests
// ============================================================================

/**
 * @brief Test: broadcast_cast_effect sends effect_type=1 and position from caster
 */
TEST_F(EffectBroadcasterTest, BroadcastCastEffectTypeAndCasterPosition) {
  InstallCapture();

  auto caster = CreateCharacter(100, 50, 60);
  SkillTemplate skill = CreateSkill(10, 5, "", "cast.wav", 500, 2000);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  const auto& cap = captures_[0];
  EXPECT_EQ(cap.effect_type, 1u);  // kEffectCast
  EXPECT_EQ(cap.x, 50);
  EXPECT_EQ(cap.y, 60);
  EXPECT_EQ(cap.skill_id, 10u);
}

// ============================================================================
// broadcast_hit_effect Tests
// ============================================================================

/**
 * @brief Test: broadcast_hit_effect(target, skill) sends effect_type=3 and caster_id=0
 */
TEST_F(EffectBroadcasterTest, BroadcastHitEffectTargetOnlyCasterIdZero) {
  InstallCapture();

  auto target = CreateCharacter(200, 70, 80);
  SkillTemplate skill = CreateSkill(20, 3, "", "hit.wav", 0, 1500);

  broadcaster_->broadcast_hit_effect(target, skill);

  ASSERT_EQ(captures_.size(), 1u);
  const auto& cap = captures_[0];
  EXPECT_EQ(cap.effect_type, 3u);  // kEffectHit
  EXPECT_EQ(cap.caster_id, 0u);   // entt::null resolves to 0
  EXPECT_EQ(cap.target_id, 200u);
  EXPECT_EQ(cap.x, 70);           // position from target
  EXPECT_EQ(cap.y, 80);
}

/**
 * @brief Test: broadcast_hit_effect(caster, target, skill) sends both ids and target position
 */
TEST_F(EffectBroadcasterTest, BroadcastHitEffectBothEntitiesPositionFromTarget) {
  InstallCapture();

  auto caster = CreateCharacter(100, 10, 20);
  auto target = CreateCharacter(200, 90, 100);
  SkillTemplate skill = CreateSkill(30, 7, "", "slash.wav", 0, 2000);

  broadcaster_->broadcast_hit_effect(caster, target, skill);

  ASSERT_EQ(captures_.size(), 1u);
  const auto& cap = captures_[0];
  EXPECT_EQ(cap.effect_type, 3u);  // kEffectHit
  EXPECT_EQ(cap.caster_id, 100u);
  EXPECT_EQ(cap.target_id, 200u);
  EXPECT_EQ(cap.x, 90);           // position from target, not caster
  EXPECT_EQ(cap.y, 100);
}

// ============================================================================
// Entity Identity Resolution Tests
// ============================================================================

/**
 * @brief Test: Entity with CharacterIdentityComponent uses the id field
 */
TEST_F(EffectBroadcasterTest, EntityWithIdentityComponentUsesIdField) {
  InstallCapture();

  auto caster = CreateCharacter(12345, 10, 20);
  auto target = CreateCharacter(67890, 30, 40);
  SkillTemplate skill = CreateSkill(1, 1, "", "fx.wav", 0, 500);

  broadcaster_->broadcast_hit_effect(caster, target, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].caster_id, 12345u);
  EXPECT_EQ(captures_[0].target_id, 67890u);
}

/**
 * @brief Test: Entity without CharacterIdentityComponent uses static_cast of entity handle
 */
TEST_F(EffectBroadcasterTest, EntityWithoutIdentityComponentUsesStaticCast) {
  InstallCapture();

  auto caster = CreateBareEntity();
  auto target = CreateBareEntity();
  SkillTemplate skill = CreateSkill(1, 1, "", "fx.wav", 0, 500);

  // Entities without identity components should use static_cast<uint64_t>(entity)
  broadcaster_->broadcast_hit_effect(caster, target, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].caster_id, static_cast<uint64_t>(caster));
  EXPECT_EQ(captures_[0].target_id, static_cast<uint64_t>(target));
}

// ============================================================================
// Position Resolution Tests
// ============================================================================

/**
 * @brief Test: Entity with CharacterStateComponent provides correct position
 */
TEST_F(EffectBroadcasterTest, EntityWithStateComponentCorrectPosition) {
  InstallCapture();

  auto caster = CreateCharacter(1, 123, 456);
  SkillTemplate skill = CreateSkill(1, 2, "", "cast.wav", 100, 0);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].x, 123);
  EXPECT_EQ(captures_[0].y, 456);
}

/**
 * @brief Test: Entity without CharacterStateComponent yields zero position
 */
TEST_F(EffectBroadcasterTest, EntityWithoutStateComponentZeroPosition) {
  InstallCapture();

  auto caster = CreateBareEntity();
  SkillTemplate skill = CreateSkill(1, 2, "", "cast.wav", 100, 0);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].x, 0);
  EXPECT_EQ(captures_[0].y, 0);
}

// ============================================================================
// Effect ID Resolution Tests
// ============================================================================

/**
 * @brief Test: Skill with non-zero effect_id uses effect_id as string
 *
 * ResolveEffectId converts the numeric effect_id via std::to_string.
 */
TEST_F(EffectBroadcasterTest, SkillWithEffectIdUsesNumericString) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  // effect_id=42, animation_id="anim_fireball" -- effect_id takes priority
  SkillTemplate skill = CreateSkill(1, 42, "anim_fireball", "fire.wav", 0, 1000);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].effect_id, "42");
}

/**
 * @brief Test: Skill with effect_id=0 but non-empty animation_id uses animation_id
 */
TEST_F(EffectBroadcasterTest, SkillWithAnimationIdFallback) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  // effect_id=0, animation_id="anim_heal"
  SkillTemplate skill = CreateSkill(1, 0, "anim_heal", "heal.wav", 0, 1000);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].effect_id, "anim_heal");
}

/**
 * @brief Test: Skill with only sound_id (no effect_id, no animation_id) still broadcasts
 *
 * effect_id resolves to empty string, but sound_id is not empty, so the
 * early return is bypassed and the callback fires.
 */
TEST_F(EffectBroadcasterTest, SkillWithOnlySoundIdStillBroadcasts) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  // effect_id=0, animation_id="", sound_id="swing.wav"
  SkillTemplate skill = CreateSkill(1, 0, "", "swing.wav", 0, 500);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_TRUE(captures_[0].effect_id.empty());
  EXPECT_EQ(captures_[0].sound_id, "swing.wav");
}

// ============================================================================
// Duration Resolution Tests
// ============================================================================

/**
 * @brief Test: Cast effect uses cast_time_ms for duration
 */
TEST_F(EffectBroadcasterTest, CastEffectUsesCastTimeMs) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  // cast_time_ms=750, duration_ms=3000
  SkillTemplate skill = CreateSkill(1, 5, "", "cast.wav", 750, 3000);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].duration_ms, 750u);  // cast uses cast_time_ms
}

/**
 * @brief Test: Hit effect uses duration_ms for duration
 */
TEST_F(EffectBroadcasterTest, HitEffectUsesDurationMs) {
  InstallCapture();

  auto target = CreateCharacter(2, 30, 40);
  // cast_time_ms=750, duration_ms=3000
  SkillTemplate skill = CreateSkill(1, 5, "", "hit.wav", 750, 3000);

  broadcaster_->broadcast_hit_effect(target, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].duration_ms, 3000u);  // hit uses duration_ms
}

/**
 * @brief Test: Cast effect with cast_time_ms=0 falls back to duration_ms
 */
TEST_F(EffectBroadcasterTest, CastEffectZeroCastTimeFallsToDurationMs) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  // cast_time_ms=0, duration_ms=2000
  SkillTemplate skill = CreateSkill(1, 5, "", "cast.wav", 0, 2000);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].duration_ms, 2000u);  // falls back to duration_ms
}

/**
 * @brief Test: Hit effect with duration_ms=0 yields zero duration
 */
TEST_F(EffectBroadcasterTest, HitEffectZeroDurationMsYieldsZero) {
  InstallCapture();

  auto target = CreateCharacter(2, 30, 40);
  // cast_time_ms=500, duration_ms=0
  SkillTemplate skill = CreateSkill(1, 5, "", "hit.wav", 500, 0);

  broadcaster_->broadcast_hit_effect(target, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].duration_ms, 0u);
}

// ============================================================================
// Comprehensive Integration Tests
// ============================================================================

/**
 * @brief Test: Full broadcast_skill_effect with all fields populated
 */
TEST_F(EffectBroadcasterTest, FullBroadcastAllFieldsPopulated) {
  InstallCapture();

  auto caster = CreateCharacter(500, 100, 200);
  auto target = CreateCharacter(600, 300, 400);
  SkillTemplate skill = CreateSkill(99, 15, "anim_thunder", "thunder.wav", 1000, 5000);

  // Use a custom effect_type (e.g., 2)
  broadcaster_->broadcast_skill_effect(caster, target, skill, 2);

  ASSERT_EQ(captures_.size(), 1u);
  const auto& cap = captures_[0];
  EXPECT_EQ(cap.caster_id, 500u);
  EXPECT_EQ(cap.target_id, 600u);
  EXPECT_EQ(cap.skill_id, 99u);
  EXPECT_EQ(cap.effect_type, 2u);
  // effect_id=15, so numeric string "15" takes priority over animation_id
  EXPECT_EQ(cap.effect_id, "15");
  EXPECT_EQ(cap.sound_id, "thunder.wav");
  // effect_type=2 is not kEffectCast(1), target is valid -> position from target
  EXPECT_EQ(cap.x, 300);
  EXPECT_EQ(cap.y, 400);
  // effect_type=2 is not kEffectCast with cast_time_ms, falls to duration_ms
  EXPECT_EQ(cap.duration_ms, 5000u);
}

/**
 * @brief Test: Multiple sequential broadcasts accumulate in captures
 */
TEST_F(EffectBroadcasterTest, MultipleBroadcastsAccumulate) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  auto target = CreateCharacter(2, 30, 40);
  SkillTemplate skill = CreateSkill(1, 5, "", "fx.wav", 200, 1000);

  broadcaster_->broadcast_cast_effect(caster, skill);
  broadcaster_->broadcast_hit_effect(target, skill);
  broadcaster_->broadcast_hit_effect(caster, target, skill);

  EXPECT_EQ(captures_.size(), 3u);

  // Verify the effect_types are correct for each call
  EXPECT_EQ(captures_[0].effect_type, 1u);  // cast
  EXPECT_EQ(captures_[1].effect_type, 3u);  // hit (target only)
  EXPECT_EQ(captures_[2].effect_type, 3u);  // hit (caster + target)
}

/**
 * @brief Test: Position fallback when target is null and effect_type is not cast
 *
 * When effect_type != kEffectCast and target is entt::null, the position
 * should fall back to caster's position.
 */
TEST_F(EffectBroadcasterTest, PositionFallbackToSenderWhenTargetNull) {
  InstallCapture();

  auto caster = CreateCharacter(1, 55, 66);
  SkillTemplate skill = CreateSkill(1, 5, "", "fx.wav", 0, 1000);

  // Manually call with effect_type=3 (hit) but target=null
  broadcaster_->broadcast_skill_effect(caster, entt::null, skill, 3);

  ASSERT_EQ(captures_.size(), 1u);
  // Target is null, so position falls back to caster
  EXPECT_EQ(captures_[0].x, 55);
  EXPECT_EQ(captures_[0].y, 66);
}

/**
 * @brief Test: Sound ID is passed through correctly from skill template
 */
TEST_F(EffectBroadcasterTest, SoundIdPassedThroughCorrectly) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  SkillTemplate skill = CreateSkill(1, 1, "", "explosion_big.ogg", 0, 500);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].sound_id, "explosion_big.ogg");
}

/**
 * @brief Test: Skill ID is forwarded correctly to callback
 */
TEST_F(EffectBroadcasterTest, SkillIdForwardedCorrectly) {
  InstallCapture();

  auto caster = CreateCharacter(1, 10, 20);
  SkillTemplate skill = CreateSkill(777, 1, "", "fx.wav", 0, 500);

  broadcaster_->broadcast_cast_effect(caster, skill);

  ASSERT_EQ(captures_.size(), 1u);
  EXPECT_EQ(captures_[0].skill_id, 777u);
}

}  // namespace
}  // namespace mir2::ecs::test
