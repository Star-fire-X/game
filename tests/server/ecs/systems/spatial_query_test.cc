/**
 * @file spatial_query_test.cc
 * @brief Comprehensive tests for SpatialQuery - spatial queries for ECS entities
 *
 * Test Coverage:
 * - Radius query: empty registry, single entity in/out of range, multiple entities,
 *   negative radius, zero radius, entity filter types
 * - Arc query: entities in arc, entities outside arc, 360-degree arc, narrow arc,
 *   zero/negative params, all 8 facing directions
 * - Line query: straight lines in all 8 directions, diagonal lines, length boundaries,
 *   zero length, entity at start (excluded by design)
 * - Entity filter: PLAYERS_ONLY filters non-players, MONSTERS_ONLY filters players,
 *   ALL includes everything, ENEMIES_ONLY treats non-players as enemies
 *
 * Priority: P0 Ultra-Critical (Score: 47)
 * Risk: Critical - Incorrect spatial queries break skill targeting (half-moon slash,
 *        assassination sword) and all AoE combat mechanics
 */

#include <gtest/gtest.h>
#include "ecs/systems/spatial_query.h"
#include "ecs/components/character_components.h"
#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace mir2::ecs::test {
namespace {

using mir2::common::Direction;
using mir2::common::Position;

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Test fixture for SpatialQuery
 *
 * Provides an entt::registry, a SpatialQuery instance, and helpers to create
 * entities with positions and optional player identity.
 */
class SpatialQueryTest : public ::testing::Test {
protected:
    void SetUp() override {
        query_ = std::make_unique<SpatialQuery>(registry_);
    }

    /**
     * @brief Place a non-player entity at the given position.
     * @return The created entity.
     */
    entt::entity PlaceEntity(int x, int y) {
        auto entity = registry_.create();
        auto& transform = registry_.emplace<TransformComponent>(entity);
        transform.position = Position{x, y};
        return entity;
    }

    /**
     * @brief Place a player entity (with CharacterIdentityComponent) at the given position.
     * @return The created entity.
     */
    entt::entity PlacePlayer(int x, int y) {
        auto entity = PlaceEntity(x, y);
        auto& identity = registry_.emplace<CharacterIdentityComponent>(entity);
        identity.id = next_char_id_++;
        identity.name = "Player" + std::to_string(identity.id);
        return entity;
    }

    /**
     * @brief Check whether a vector of entities contains a specific entity.
     */
    bool Contains(const std::vector<entt::entity>& entities, entt::entity target) const {
        return std::find(entities.begin(), entities.end(), target) != entities.end();
    }

    entt::registry registry_;
    std::unique_ptr<SpatialQuery> query_;
    CharacterId next_char_id_ = 1;
};

// ============================================================================
// Radius Query Tests
// ============================================================================

TEST_F(SpatialQueryTest, RadiusQuery_EmptyRegistry_ReturnsEmpty) {
    auto result = query_->get_entities_in_radius(Position{50, 50}, 10.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_SingleEntityInRange_ReturnsEntity) {
    auto entity = PlaceEntity(52, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, RadiusQuery_SingleEntityOutOfRange_ReturnsEmpty) {
    PlaceEntity(100, 100);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_EntityExactlyOnBoundary_IsIncluded) {
    // Distance from (50,50) to (53,54) = sqrt(9+16) = 5.0
    auto entity = PlaceEntity(53, 54);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, RadiusQuery_EntityJustBeyondBoundary_IsExcluded) {
    // Distance from (50,50) to (54,54) = sqrt(16+16) ~= 5.66, beyond radius 5.0
    PlaceEntity(54, 54);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_EntityAtCenter_IsIncluded) {
    auto entity = PlaceEntity(50, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, RadiusQuery_MultipleEntities_ReturnsOnlyInRange) {
    auto in1 = PlaceEntity(51, 50);
    auto in2 = PlaceEntity(50, 52);
    auto in3 = PlaceEntity(48, 48);  // dist = sqrt(4+4) ~= 2.83
    PlaceEntity(100, 200);           // far away
    PlaceEntity(60, 60);             // dist = sqrt(100+100) ~= 14.14

    auto result = query_->get_entities_in_radius(Position{50, 50}, 3.0f);
    EXPECT_EQ(result.size(), 3u);
    EXPECT_TRUE(Contains(result, in1));
    EXPECT_TRUE(Contains(result, in2));
    EXPECT_TRUE(Contains(result, in3));
}

TEST_F(SpatialQueryTest, RadiusQuery_NegativeRadius_ReturnsEmpty) {
    PlaceEntity(50, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, -1.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_ZeroRadius_ReturnsOnlyCenterEntity) {
    auto at_center = PlaceEntity(50, 50);
    PlaceEntity(51, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 0.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], at_center);
}

TEST_F(SpatialQueryTest, RadiusQuery_ZeroRadiusNoEntityAtCenter_ReturnsEmpty) {
    PlaceEntity(51, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 0.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_VeryLargeRadius_ReturnsAll) {
    auto e1 = PlaceEntity(0, 0);
    auto e2 = PlaceEntity(1000, 1000);
    auto e3 = PlaceEntity(-500, -500);
    auto result = query_->get_entities_in_radius(Position{0, 0}, 100000.0f);
    EXPECT_EQ(result.size(), 3u);
    EXPECT_TRUE(Contains(result, e1));
    EXPECT_TRUE(Contains(result, e2));
    EXPECT_TRUE(Contains(result, e3));
}

// ============================================================================
// Radius Query with Entity Filter Tests
// ============================================================================

TEST_F(SpatialQueryTest, RadiusQuery_FilterAll_IncludesPlayersAndMonsters) {
    auto player = PlacePlayer(51, 50);
    auto monster = PlaceEntity(52, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::ALL);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(Contains(result, player));
    EXPECT_TRUE(Contains(result, monster));
}

TEST_F(SpatialQueryTest, RadiusQuery_FilterPlayersOnly_ExcludesMonsters) {
    auto player = PlacePlayer(51, 50);
    PlaceEntity(52, 50);  // non-player
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::PLAYERS_ONLY);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], player);
}

TEST_F(SpatialQueryTest, RadiusQuery_FilterPlayersOnly_NoPlayersInRange_ReturnsEmpty) {
    PlaceEntity(51, 50);
    PlaceEntity(52, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::PLAYERS_ONLY);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_FilterMonstersOnly_ExcludesPlayers) {
    PlacePlayer(51, 50);
    auto monster = PlaceEntity(52, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::MONSTERS_ONLY);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], monster);
}

TEST_F(SpatialQueryTest, RadiusQuery_FilterEnemiesOnly_ExcludesPlayers) {
    PlacePlayer(51, 50);
    auto monster = PlaceEntity(52, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::ENEMIES_ONLY);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], monster);
}

TEST_F(SpatialQueryTest, RadiusQuery_FilterMonstersOnly_NoMonstersInRange_ReturnsEmpty) {
    PlacePlayer(51, 50);
    PlacePlayer(52, 50);
    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::MONSTERS_ONLY);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, RadiusQuery_MultiplePlayersAndMonsters_FilterCorrectly) {
    auto p1 = PlacePlayer(51, 50);
    auto p2 = PlacePlayer(50, 51);
    auto m1 = PlaceEntity(49, 50);
    auto m2 = PlaceEntity(50, 49);

    auto all = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::ALL);
    EXPECT_EQ(all.size(), 4u);

    auto players = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::PLAYERS_ONLY);
    EXPECT_EQ(players.size(), 2u);
    EXPECT_TRUE(Contains(players, p1));
    EXPECT_TRUE(Contains(players, p2));

    auto monsters = query_->get_entities_in_radius(Position{50, 50}, 5.0f, EntityFilter::MONSTERS_ONLY);
    EXPECT_EQ(monsters.size(), 2u);
    EXPECT_TRUE(Contains(monsters, m1));
    EXPECT_TRUE(Contains(monsters, m2));
}

// ============================================================================
// Arc Query Tests (half-moon slash / ban yue wan dao)
// ============================================================================

TEST_F(SpatialQueryTest, ArcQuery_EmptyRegistry_ReturnsEmpty) {
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 90.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_EntityInFrontArc_IsIncluded) {
    // Facing RIGHT (angle 0), entity at (53, 50) is directly right
    auto entity = PlaceEntity(53, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, ArcQuery_EntityBehindFacingDirection_IsExcluded) {
    // Facing RIGHT (angle 0), entity at (47, 50) is to the left
    PlaceEntity(47, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 90.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_EntityOutOfRadius_IsExcluded) {
    // In the right direction but beyond radius
    PlaceEntity(60, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 90.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_360Degrees_IncludesAllInRadius) {
    auto e1 = PlaceEntity(53, 50);   // right
    auto e2 = PlaceEntity(47, 50);   // left
    auto e3 = PlaceEntity(50, 53);   // below
    auto e4 = PlaceEntity(50, 47);   // above
    PlaceEntity(100, 100);           // far away

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 360.0f);
    EXPECT_EQ(result.size(), 4u);
    EXPECT_TRUE(Contains(result, e1));
    EXPECT_TRUE(Contains(result, e2));
    EXPECT_TRUE(Contains(result, e3));
    EXPECT_TRUE(Contains(result, e4));
}

TEST_F(SpatialQueryTest, ArcQuery_NarrowArc_OnlyDirectlyInFront) {
    // Facing RIGHT with a very narrow 10 degree arc
    auto direct = PlaceEntity(53, 50);  // exactly right, angle=0
    PlaceEntity(53, 52);                // right and slightly down, angle > 5 degrees

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 10.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], direct);
}

TEST_F(SpatialQueryTest, ArcQuery_ZeroArcDegrees_ReturnsEmpty) {
    PlaceEntity(53, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 0.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_NegativeArcDegrees_ReturnsEmpty) {
    PlaceEntity(53, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, -45.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_ZeroRadius_ReturnsEmpty) {
    PlaceEntity(50, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 0.0f, Direction::RIGHT, 90.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_NegativeRadius_ReturnsEmpty) {
    PlaceEntity(53, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, -5.0f, Direction::RIGHT, 90.0f);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, ArcQuery_FacingUp_IncludesEntitiesAbove) {
    // UP direction: angle = -90 degrees (atan2 of negative dy, zero dx)
    auto above = PlaceEntity(50, 47);   // directly above
    PlaceEntity(50, 53);                // directly below

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::UP, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], above);
}

TEST_F(SpatialQueryTest, ArcQuery_FacingDown_IncludesEntitiesBelow) {
    // DOWN direction: angle = 90 degrees
    PlaceEntity(50, 47);                // directly above
    auto below = PlaceEntity(50, 53);   // directly below

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::DOWN, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], below);
}

TEST_F(SpatialQueryTest, ArcQuery_FacingLeft_IncludesEntitiesLeft) {
    // LEFT direction: angle = 180 degrees
    auto left = PlaceEntity(47, 50);    // to the left
    PlaceEntity(53, 50);                // to the right

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::LEFT, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], left);
}

TEST_F(SpatialQueryTest, ArcQuery_FacingDownRight_IncludesDiagonalEntities) {
    // DOWN_RIGHT direction: angle = 45 degrees
    auto dr = PlaceEntity(53, 53);      // diagonal down-right
    PlaceEntity(47, 47);                // diagonal up-left (opposite)

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::DOWN_RIGHT, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], dr);
}

TEST_F(SpatialQueryTest, ArcQuery_FacingUpLeft_IncludesDiagonalEntities) {
    // UP_LEFT direction: angle = -135 degrees
    auto ul = PlaceEntity(47, 47);      // diagonal up-left
    PlaceEntity(53, 53);                // diagonal down-right (opposite)

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::UP_LEFT, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], ul);
}

TEST_F(SpatialQueryTest, ArcQuery_HalfMoonSlash_180DegreeArc) {
    // Simulate ban yue wan dao: 180 degree arc facing RIGHT
    auto right = PlaceEntity(53, 50);       // directly right
    auto up_right = PlaceEntity(53, 47);    // upper-right
    auto down_right = PlaceEntity(53, 53);  // lower-right
    PlaceEntity(47, 50);                    // directly left (should be excluded)

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 180.0f);
    EXPECT_GE(result.size(), 3u);
    EXPECT_TRUE(Contains(result, right));
    EXPECT_TRUE(Contains(result, up_right));
    EXPECT_TRUE(Contains(result, down_right));
}

// ============================================================================
// Line Query Tests (assassination sword / ci sha jian shu)
// ============================================================================

TEST_F(SpatialQueryTest, LineQuery_EmptyRegistry_ReturnsEmpty) {
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_EntityOnLine_IsIncluded) {
    // Facing RIGHT from (50,50), entity at (52,50) is 2 steps right
    auto entity = PlaceEntity(52, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_EntityOffLine_IsExcluded) {
    // Facing RIGHT from (50,50), entity at (52,51) is not on the line
    PlaceEntity(52, 51);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_EntityAtStartPosition_IsExcluded) {
    // Entity at the start position itself: steps = 0, which is < 1
    PlaceEntity(50, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_EntityBeyondLength_IsExcluded) {
    // Facing RIGHT, length=2, entity at (54,50) is 4 steps away
    PlaceEntity(54, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 2);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_EntityExactlyAtMaxLength_IsIncluded) {
    // Facing RIGHT, length=3, entity at (53,50) is exactly 3 steps
    auto entity = PlaceEntity(53, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_EntityAtStep1_IsIncluded) {
    // Facing RIGHT, entity at (51,50) is 1 step
    auto entity = PlaceEntity(51, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_ZeroLength_ReturnsEmpty) {
    PlaceEntity(51, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 0);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_NegativeLength_ReturnsEmpty) {
    PlaceEntity(51, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, -1);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_MultipleEntitiesOnLine_ReturnsAll) {
    auto e1 = PlaceEntity(51, 50);
    auto e2 = PlaceEntity(52, 50);
    auto e3 = PlaceEntity(53, 50);
    PlaceEntity(54, 50);  // beyond length

    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    EXPECT_EQ(result.size(), 3u);
    EXPECT_TRUE(Contains(result, e1));
    EXPECT_TRUE(Contains(result, e2));
    EXPECT_TRUE(Contains(result, e3));
}

TEST_F(SpatialQueryTest, LineQuery_EntityInOppositeDirection_IsExcluded) {
    // Facing RIGHT, entity to the LEFT at (48, 50)
    PlaceEntity(48, 50);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 5);
    EXPECT_TRUE(result.empty());
}

// --- All 8 directions ---

TEST_F(SpatialQueryTest, LineQuery_DirectionUp) {
    // UP: dx=0, dy=-1
    auto entity = PlaceEntity(50, 48);  // 2 steps up
    PlaceEntity(50, 52);                // wrong direction (down)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::UP, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionDown) {
    // DOWN: dx=0, dy=+1
    auto entity = PlaceEntity(50, 52);  // 2 steps down
    PlaceEntity(50, 48);                // wrong direction (up)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::DOWN, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionLeft) {
    // LEFT: dx=-1, dy=0
    auto entity = PlaceEntity(48, 50);  // 2 steps left
    PlaceEntity(52, 50);                // wrong direction (right)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::LEFT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionRight) {
    // RIGHT: dx=+1, dy=0
    auto entity = PlaceEntity(52, 50);  // 2 steps right
    PlaceEntity(48, 50);                // wrong direction (left)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionUpRight) {
    // UP_RIGHT: dx=+1, dy=-1 (diagonal)
    auto entity = PlaceEntity(52, 48);  // 2 steps up-right
    PlaceEntity(48, 52);                // wrong direction (down-left)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::UP_RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionDownRight) {
    // DOWN_RIGHT: dx=+1, dy=+1 (diagonal)
    auto entity = PlaceEntity(52, 52);  // 2 steps down-right
    PlaceEntity(48, 48);                // wrong direction (up-left)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::DOWN_RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionDownLeft) {
    // DOWN_LEFT: dx=-1, dy=+1 (diagonal)
    auto entity = PlaceEntity(48, 52);  // 2 steps down-left
    PlaceEntity(52, 48);                // wrong direction (up-right)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::DOWN_LEFT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_DirectionUpLeft) {
    // UP_LEFT: dx=-1, dy=-1 (diagonal)
    auto entity = PlaceEntity(48, 48);  // 2 steps up-left
    PlaceEntity(52, 52);                // wrong direction (down-right)
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::UP_LEFT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

// --- Diagonal precision tests ---

TEST_F(SpatialQueryTest, LineQuery_DiagonalUnequalSteps_IsExcluded) {
    // For diagonal directions, |dx| must equal |dy|
    // UP_RIGHT from (50,50): entity at (52, 49) has dx=2, dy=-1, unequal
    PlaceEntity(52, 49);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::UP_RIGHT, 5);
    EXPECT_TRUE(result.empty());
}

TEST_F(SpatialQueryTest, LineQuery_DiagonalEqualSteps_IsIncluded) {
    // UP_RIGHT from (50,50): entity at (53, 47) has dx=3, dy=-3, equal
    auto entity = PlaceEntity(53, 47);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::UP_RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_CardinalWithPerpendicularOffset_IsExcluded) {
    // Facing UP from (50,50): entity at (51,48) has dx=1 which is not 0
    PlaceEntity(51, 48);
    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::UP, 5);
    EXPECT_TRUE(result.empty());
}

// --- Assassination sword simulation ---

TEST_F(SpatialQueryTest, LineQuery_AssassinationSword_TwoCellPierce) {
    // ci sha jian shu: pierce through one target and hit the one behind
    // Facing RIGHT from (50,50), length=2
    auto front = PlaceEntity(51, 50);  // step 1
    auto back = PlaceEntity(52, 50);   // step 2

    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 2);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(Contains(result, front));
    EXPECT_TRUE(Contains(result, back));
}

TEST_F(SpatialQueryTest, LineQuery_Length1_OnlyImmediateNext) {
    auto close = PlaceEntity(51, 50);
    PlaceEntity(52, 50);  // step 2, beyond length 1

    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 1);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], close);
}

// ============================================================================
// Arc Query - Does Not Apply EntityFilter (by design)
// ============================================================================

TEST_F(SpatialQueryTest, ArcQuery_IncludesBothPlayersAndNonPlayers) {
    // The arc query does not take an EntityFilter parameter, so it includes
    // all entities with a TransformComponent that fall within the arc.
    auto player = PlacePlayer(53, 50);
    auto monster = PlaceEntity(54, 50);

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 90.0f);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(Contains(result, player));
    EXPECT_TRUE(Contains(result, monster));
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

TEST_F(SpatialQueryTest, RadiusQuery_NegativeCoordinates_WorksCorrectly) {
    auto entity = PlaceEntity(-3, -4);
    auto result = query_->get_entities_in_radius(Position{-5, -5}, 3.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, RadiusQuery_OriginCenter_WorksCorrectly) {
    auto entity = PlaceEntity(0, 0);
    auto result = query_->get_entities_in_radius(Position{0, 0}, 1.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, LineQuery_FromNegativeCoordinates_WorksCorrectly) {
    // Start at (-10, -10), facing RIGHT, entity at (-8, -10) is 2 steps
    auto entity = PlaceEntity(-8, -10);
    auto result = query_->get_entities_in_line(Position{-10, -10}, Direction::RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, ArcQuery_FromNegativeCoordinates_WorksCorrectly) {
    // Center at (-10, -10), facing RIGHT, entity at (-7, -10) is 3 units right
    auto entity = PlaceEntity(-7, -10);
    auto result = query_->get_entities_in_arc(Position{-10, -10}, 5.0f, Direction::RIGHT, 90.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], entity);
}

TEST_F(SpatialQueryTest, RadiusQuery_ManyEntities_ReturnsCorrectSubset) {
    // Place 20 entities in a grid pattern around center (50,50)
    std::vector<entt::entity> expected_in;
    std::vector<entt::entity> expected_out;

    for (int dx = -5; dx <= 5; dx += 1) {
        for (int dy = -5; dy <= 5; dy += 1) {
            auto e = PlaceEntity(50 + dx, 50 + dy);
            float dist_sq = static_cast<float>(dx * dx + dy * dy);
            if (dist_sq <= 9.0f) {  // radius = 3.0
                expected_in.push_back(e);
            } else {
                expected_out.push_back(e);
            }
        }
    }

    auto result = query_->get_entities_in_radius(Position{50, 50}, 3.0f);
    EXPECT_EQ(result.size(), expected_in.size());
    for (auto e : expected_in) {
        EXPECT_TRUE(Contains(result, e));
    }
    for (auto e : expected_out) {
        EXPECT_FALSE(Contains(result, e));
    }
}

TEST_F(SpatialQueryTest, LineQuery_AllDirections_SymmetryCheck) {
    // Place entities at distance 2 in all 8 directions and verify each direction
    // picks up exactly its own entity.
    struct DirEntity {
        Direction dir;
        int dx;
        int dy;
    };

    const DirEntity dirs[] = {
        {Direction::UP,         0, -2},
        {Direction::UP_RIGHT,   2, -2},
        {Direction::RIGHT,      2,  0},
        {Direction::DOWN_RIGHT, 2,  2},
        {Direction::DOWN,       0,  2},
        {Direction::DOWN_LEFT, -2,  2},
        {Direction::LEFT,      -2,  0},
        {Direction::UP_LEFT,   -2, -2},
    };

    std::vector<entt::entity> entities;
    for (const auto& d : dirs) {
        entities.push_back(PlaceEntity(50 + d.dx, 50 + d.dy));
    }

    for (size_t i = 0; i < 8; ++i) {
        auto result = query_->get_entities_in_line(Position{50, 50}, dirs[i].dir, 3);
        ASSERT_GE(result.size(), 1u) << "Direction index " << i << " should find at least 1 entity";
        EXPECT_TRUE(Contains(result, entities[i]))
            << "Direction index " << i << " should contain its corresponding entity";
    }
}

TEST_F(SpatialQueryTest, RadiusQuery_DefaultFilterIsAll) {
    // Verify that calling without an explicit filter argument defaults to ALL
    auto player = PlacePlayer(51, 50);
    auto monster = PlaceEntity(52, 50);

    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(Contains(result, player));
    EXPECT_TRUE(Contains(result, monster));
}

TEST_F(SpatialQueryTest, LineQuery_EntityWithNoTransform_IsIgnored) {
    // Create an entity without TransformComponent; should be invisible to queries
    auto bare_entity = registry_.create();
    registry_.emplace<CharacterIdentityComponent>(bare_entity);

    auto on_line = PlaceEntity(51, 50);

    auto result = query_->get_entities_in_line(Position{50, 50}, Direction::RIGHT, 3);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], on_line);
}

TEST_F(SpatialQueryTest, RadiusQuery_EntityWithNoTransform_IsIgnored) {
    // Create entity with only identity, no position
    auto bare_entity = registry_.create();
    registry_.emplace<CharacterIdentityComponent>(bare_entity);

    auto visible = PlaceEntity(51, 50);

    auto result = query_->get_entities_in_radius(Position{50, 50}, 5.0f);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], visible);
}

TEST_F(SpatialQueryTest, ArcQuery_EntityAtCenter_AngleIsUndefined) {
    // Entity exactly at center: atan2(0,0) = 0 degrees, which is RIGHT.
    // With a RIGHT-facing 90 degree arc, the entity should be included since
    // |0 - 0| = 0 <= 45 (half arc).
    auto entity = PlaceEntity(50, 50);
    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::RIGHT, 90.0f);
    // The distance is 0, which is within radius, and angle_between returns 0
    // for coincident points, matching RIGHT direction.
    EXPECT_TRUE(Contains(result, entity));
}

TEST_F(SpatialQueryTest, ArcQuery_FacingUpRight_135DegreeArc) {
    // UP_RIGHT direction: angle = -45 degrees
    // Half arc = 67.5 degrees, so entities within -112.5 to 22.5 degrees are included
    auto ur = PlaceEntity(52, 48);  // UP_RIGHT: atan2(-2, 2) = -45 deg
    auto r = PlaceEntity(53, 50);   // RIGHT: atan2(0, 3) = 0 deg, delta = 45 <= 67.5
    auto u = PlaceEntity(50, 47);   // UP: atan2(-3, 0) = -90 deg, delta = 45 <= 67.5
    PlaceEntity(47, 50);            // LEFT: atan2(0, -3) = 180 deg, delta > 67.5

    auto result = query_->get_entities_in_arc(Position{50, 50}, 5.0f, Direction::UP_RIGHT, 135.0f);
    EXPECT_GE(result.size(), 3u);
    EXPECT_TRUE(Contains(result, ur));
    EXPECT_TRUE(Contains(result, r));
    EXPECT_TRUE(Contains(result, u));
}

}  // namespace
}  // namespace mir2::ecs::test
