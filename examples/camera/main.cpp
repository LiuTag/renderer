/** @file @brief 只依赖公开 camera SDK，无图形设备验证三种控制模式与带父节点的场景写回。 */
#include <cmath>
#include <iostream>
#include <renderer/camera/camera_controller.hpp>
#include <stdexcept>
using namespace renderer;
int main() {
    try {
        Scene scene;
        const auto group = scene.createNode(scene.root(), "Camera parent");
        scene.setLocalTransform(group, Mat4::translation({5, 0, 0}));
        const auto camera = scene.createNode(group, "Camera");
        scene.setComponent(camera, Camera{});
        CameraController controller;
        controller.update(1. / 60, {.rotation = {.2f, .1f}, .pan = {1, 0}, .zoom = -1});
        controller.apply(scene, camera);
        const auto orbit = controller.state();
        controller.setMode(CameraControlMode::Follow);
        controller.setTarget({3, 0, 0});
        controller.update(1. / 60);
        controller.apply(scene, camera);
        const auto follow = controller.state();
        controller.setMode(CameraControlMode::Free);
        controller.update(.25, {.movement = {0, 0, 1}, .boost = true});
        controller.apply(scene, camera);
        const auto free = controller.state();
        const auto actual = scene.worldTransform(camera);
        for (size_t i = 0; i < 16; ++i)
            if (std::abs(actual.elements[i] - free.world.elements[i]) > 1e-4f)
                throw std::runtime_error("Camera SDK world transform mismatch");
        if (!orbit.world.isAffine() || !follow.world.isAffine() || !free.world.isAffine())
            throw std::runtime_error("Camera SDK returned invalid pose");
        std::cout << "Orbit -> Follow -> Free; scene camera at " << free.position.x << ','
                  << free.position.y << ',' << free.position.z << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
