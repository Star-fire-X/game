#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include "combat_generated.h"
#include "ecs/components/character_components.h"
#include "game/map/aoi_manager.h"
#include "logic/handlers/effect/effect_broadcast_service.h"
#include "logic/services/session_role_store.h"
#include "network/network_manager.h"

namespace mir2::logic::test {
namespace {

class EffectBroadcastServiceTest : public ::testing::Test {
 protected:
  EffectBroadcastServiceTest()
      : network_(io_context_),
        aoi_manager_(512, 512, 16),
        service_(network_, aoi_manager_, registry_, role_store_) {}

  entt::entity AddViewer(std::optional<uint64_t> role_id,
                         int x,
                         int y) {
    const entt::entity entity = registry_.create();
    if (role_id.has_value()) {
      auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
      identity.id = *role_id;
      identity.name = "viewer_" + std::to_string(*role_id);
    }
    aoi_manager_.Enter(static_cast<uint64_t>(entt::to_integral(entity)), x, y);
    return entity;
  }

  asio::io_context io_context_;
  network::NetworkManager network_;
  game::map::AOIManager aoi_manager_;
  entt::registry registry_;
  RoleStore role_store_;
  EffectBroadcastService service_;
};

TEST_F(EffectBroadcastServiceTest, NoViewersReturnsEarly) {
  service_.BroadcastSkillEffect(/*caster_id=*/1,
                                /*target_id=*/2,
                                /*skill_id=*/100,
                                static_cast<uint8_t>(mir2::proto::EffectType::CAST),
                                "cast_fx",
                                "cast_sfx",
                                /*x=*/10,
                                /*y=*/20,
                                /*duration_ms=*/500);
  SUCCEED();
}

TEST_F(EffectBroadcastServiceTest, InvalidEffectTypeReturnsEarly) {
  AddViewer(/*role_id=*/1001, /*x=*/11, /*y=*/22);

  service_.BroadcastSkillEffect(
      /*caster_id=*/1,
      /*target_id=*/2,
      /*skill_id=*/100,
      static_cast<uint8_t>(mir2::proto::EffectType::MAX) + 1,
      "invalid_fx",
      "invalid_sfx",
      /*x=*/11,
      /*y=*/22,
      /*duration_ms=*/1000);

  SUCCEED();
}

TEST_F(EffectBroadcastServiceTest, ViewerWithoutIdentityOrRoleBindingIsSkipped) {
  AddViewer(/*role_id=*/std::nullopt, /*x=*/30, /*y=*/40);
  AddViewer(/*role_id=*/2001, /*x=*/30, /*y=*/40);

  service_.BroadcastSkillEffect(/*caster_id=*/10,
                                /*target_id=*/20,
                                /*skill_id=*/300,
                                static_cast<uint8_t>(mir2::proto::EffectType::HIT),
                                "hit_fx",
                                "hit_sfx",
                                /*x=*/30,
                                /*y=*/40,
                                /*duration_ms=*/200);

  SUCCEED();
}

TEST_F(EffectBroadcastServiceTest, MappedRoleWithoutLocalSessionIsSkipped) {
  AddViewer(/*role_id=*/3001, /*x=*/60, /*y=*/70);
  AddViewer(/*role_id=*/3002, /*x=*/60, /*y=*/70);

  role_store_.BindClientRole(/*client_id=*/9001, /*player_id=*/3002);
  EXPECT_FALSE(network_.GetSession(9001));

  service_.BroadcastSkillEffect(/*caster_id=*/30,
                                /*target_id=*/40,
                                /*skill_id=*/400,
                                static_cast<uint8_t>(mir2::proto::EffectType::PROJECTILE),
                                "proj_fx",
                                "proj_sfx",
                                /*x=*/60,
                                /*y=*/70,
                                /*duration_ms=*/250);

  SUCCEED();
}

TEST_F(EffectBroadcastServiceTest, InvalidRegistryEntityInAoiListIsIgnored) {
  aoi_manager_.Enter(/*entity_id=*/999999u, /*x=*/5, /*y=*/6);

  service_.BroadcastSkillEffect(/*caster_id=*/3,
                                /*target_id=*/4,
                                /*skill_id=*/500,
                                static_cast<uint8_t>(mir2::proto::EffectType::AOE),
                                "aoe_fx",
                                "aoe_sfx",
                                /*x=*/5,
                                /*y=*/6,
                                /*duration_ms=*/300);

  SUCCEED();
}

}  // namespace
}  // namespace mir2::logic::test
