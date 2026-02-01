#include <gtest/gtest.h>

#include "client/game/entity_manager.h"

namespace {

mir2::render::Camera MakeCameraCentered(int x, int y, int viewport_w, int viewport_h) {
    mir2::render::Camera camera;
    camera.viewport_width = viewport_w;
    camera.viewport_height = viewport_h;
    camera.zoom = 1.0f;
    camera.center_on({x, y});
    return camera;
}

} // namespace

TEST(EntityManagerViewTest, FiltersEntitiesOutsideView) {
    mir2::game::EntityManager manager(1);

    mir2::game::Entity a;
    a.id = 1;
    a.position = {5, 5};
    manager.add_entity(a);

    mir2::game::Entity b;
    b.id = 2;
    b.position = {100, 100};
    manager.add_entity(b);

    auto camera = MakeCameraCentered(5, 5, 96, 64); // small view around (5,5)

    auto in_view = manager.get_entities_in_view(camera, 0);
    ASSERT_EQ(in_view.size(), 1u);
    EXPECT_EQ(in_view[0]->id, 1u);
}

TEST(EntityManagerViewTest, SortsByYThenX) {
    mir2::game::EntityManager manager(1);

    mir2::game::Entity a;
    a.id = 1;
    a.position = {4, 5};
    manager.add_entity(a);

    mir2::game::Entity b;
    b.id = 2;
    b.position = {3, 4};
    manager.add_entity(b);

    mir2::game::Entity c;
    c.id = 3;
    c.position = {5, 4};
    manager.add_entity(c);

    auto camera = MakeCameraCentered(4, 5, 400, 300);
    auto in_view = manager.get_entities_in_view(camera, 0);
    ASSERT_EQ(in_view.size(), 3u);

    // Expected order: y=4,x=3 (id=2) -> y=4,x=5 (id=3) -> y=5,x=4 (id=1)
    EXPECT_EQ(in_view[0]->id, 2u);
    EXPECT_EQ(in_view[1]->id, 3u);
    EXPECT_EQ(in_view[2]->id, 1u);
}
