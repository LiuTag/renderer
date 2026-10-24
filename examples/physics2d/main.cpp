/** @file @brief 仅使用公共 SDK 的 Box2D 推挤、触发、查询和场景同步示例，无需窗口。 */
#include <cmath>
#include <iostream>
#include <renderer/physics2d/physics2d.hpp>
#include <stdexcept>
using namespace renderer;
int main() {
    try {
        Physics2D world({.workerCount = 2});
        Scene scene;
        Body2DDesc actor;
        actor.lockRotation = true;
        actor.shapes[0].radius = .3f;
        const auto player = world.createBody(actor);
        actor.position = {1, 0};
        actor.userData = 42;
        const auto crate = world.createBody(actor);
        Body2DDesc trigger;
        trigger.type = Body2DType::Static;
        trigger.shapes[0].radius = 3;
        trigger.shapes[0].sensor = true;
        world.createBody(trigger);
        auto node = scene.createNode(scene.root());
        scene.setLocalTransform(node, Mat4::translation({1, .5f, 0}));
        world.bindNode(crate, scene, node);
        bool entered = false, contact = false;
        for (int i = 0; i < 120; ++i) {
            world.setVelocity(player, {2, 0});
            world.step();
            world.synchronize(scene, false);
            for (const auto &event : world.takeEvents()) {
                entered |= event.kind == Physics2DEventKind::SensorBegin;
                contact |= event.kind == Physics2DEventKind::ContactBegin;
            }
        }
        const auto position = world.state(crate).position;
        const auto hit = world.rayCast({position.x - 2, 0}, {4, 0}, {.ignore = player});
        if (!entered || !contact || position.x < 2 || !hit || hit->body != crate ||
            std::abs(scene.worldTransform(node)(1, 3) - .5f) > .001f)
            throw std::runtime_error("Box2D integration self-test failed");
        std::cout << "Box2D crate=" << position.x << " sensor=" << entered << " contact=" << contact
                  << " bodies=" << world.statistics().bodies << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
