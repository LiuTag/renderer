/** @file @brief 碰撞句柄、层过滤、连续扫掠、滑动及初始穿透恢复测试。 */
#include "sprite_fixture.hpp"
#include <renderer/collision/collision.hpp>
using namespace sprite_test;
int main() {
    try {
        CollisionWorld world;
        auto circle = world.add({.center = {0, 0}, .radius = 1, .layerMask = 1});
        require(world.overlapCircle({2, 0}, 1, 1).size() == 1);
        require(world.overlapCircle({0, 0}, 1, 2).empty());
        auto motion = world.moveCircle({-10, 0}, .5f, {20, 0});
        near(motion.position.x, -1.5f, .01f);
        require(!motion.contacts.empty());
        auto inside = world.moveCircle({0, 0}, .5f, {});
        require(std::sqrt(inside.position.x * inside.position.x +
                          inside.position.y * inside.position.y) >= 1.49f);
        world.remove(circle);
        require(!world.valid(circle));
        throws([&] { world.description(circle); });
        auto box = world.add({.shape = ColliderShape::Box, .halfExtents = {1, 10}, .radius = 0});
        require(box.index == circle.index && box.generation != circle.generation);
        require(!world.valid(circle));
        throws([&] { world.remove(circle); });
        auto slide = world.moveCircle({-3, 0}, .5f, {6, 2});
        near(slide.position.x, -1.5f, .01f);
        near(slide.position.y, 2, .01f);
        CollisionWorld corner;
        corner.add({.shape = ColliderShape::Box, .halfExtents = {1, 1}});
        require(corner.overlapCircle({-1.4f, -1.4f}, .5f).empty());
        const auto diagonal = corner.moveCircle({-1.4f, -1.4f}, .5f, {4, 4});
        require(!diagonal.contacts.empty() && diagonal.position.x < -1 && diagonal.position.y < -1);
        CollisionWorld second;
        auto h = second.add({});
        require(!world.valid(h));
        throws([&] { world.moveCircle({}, 0, {}); });
        std::cout << "collision tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
