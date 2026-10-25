/** @file @brief 无窗口验证相机坐标、缩放限幅、平滑、模式切换、父变换及失败原子性。 */
#include <cmath>
#include <iostream>
#include <limits>
#include <renderer/camera/camera_controller.hpp>
#include <stdexcept>
using namespace renderer;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(float a, float b, float tolerance = 1e-4f) {
    require(std::abs(a - b) < tolerance, "Camera numeric mismatch");
}
void near(Vec3 a, Vec3 b) {
    near(a.x, b.x);
    near(a.y, b.y);
    near(a.z, b.z);
}
void near(const Mat4 &a, const Mat4 &b) {
    for (size_t i = 0; i < 16; ++i)
        near(a.elements[i], b.elements[i]);
}
template <class F> void throws(F action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected camera error");
}
} // namespace
int main() {
    try {
        CameraControllerConfig config;
        config.pitch = 0;
        config.target = {1, 2, 3};
        CameraController orbit(config);
        near(orbit.state().position, {1, 2, 13});
        near(orbit.state().world, *Mat4::lookAt({1, 2, 13}, {1, 2, 3}).inverse());
        orbit.update(0, {.rotation = {radians(90), 0}, .pan = {2, 3}});
        near(orbit.state().position, {11, 5, 1});
        near(orbit.state().target, {1, 5, 1});
        orbit.update(0, {.zoom = 1e30f});
        near(orbit.state().distance, config.maxDistance);
        orbit.update(0, {.zoom = -1e30f});
        near(orbit.state().distance, config.minDistance);
        orbit.setProjection(Camera::Projection::Orthographic);
        orbit.update(0, {.zoom = 1e30f});
        near(orbit.state().orthographicHeight, config.maxOrthographicHeight);
        near(orbit.state().distance, config.minDistance);
        orbit.update(0, {.rotation = {0, 1000}, .zoom = -1e30f});
        near(orbit.state().pitch, config.maxPitch);
        near(orbit.state().orthographicHeight, config.minOrthographicHeight);
        require(orbit.state().world.isAffine(), "Pole-limited camera became invalid");
        std::cout << "PASS orbit axes, view-space pan and extreme zoom/pitch\n";

        config = {};
        config.mode = CameraControlMode::Follow;
        config.followOffset = {0, 5, 10};
        config.targetOffset = {0, 2, 0};
        config.smoothing = 8;
        CameraController oneStep(config), manySteps(config);
        near(oneStep.state().position, {0, 7, 10});
        oneStep.setTarget({10, 0, 0});
        manySteps.setTarget({10, 0, 0});
        oneStep.update(1);
        for (int i = 0; i < 60; ++i)
            manySteps.update(1. / 60);
        near(oneStep.state().world, manySteps.state().world);
        near(oneStep.state().target.x, float(10 * (1 - std::exp(-8))));
        const auto beforeZero = manySteps.state().world;
        manySteps.update(0);
        near(manySteps.state().world, beforeZero);
        manySteps.setTarget({50, 1, -3}, true);
        near(manySteps.state().target, {50, 3, -3});
        near(manySteps.state().position, {50, 8, 7});
        std::cout << "PASS follow offset, exponential damping and teleport\n";

        config = {};
        config.mode = CameraControlMode::Free;
        config.position = {};
        config.pitch = radians(45);
        CameraController fly(config);
        fly.update(1, {.movement = {0, 0, 1}});
        near(fly.state().position, {0, -5 / std::sqrt(2.f), -5 / std::sqrt(2.f)});
        fly.update(0, {.zoom = 1e30f});
        near(fly.state().verticalFov, config.maxVerticalFov);
        config.groundMovement = true;
        fly.reset(config);
        fly.update(1, {.movement = {1, 0, 1}});
        near(fly.state().position, {5 / std::sqrt(2.f), 0, -5 / std::sqrt(2.f)});
        fly.reset(config);
        fly.update(.5, {.movement = {0, 1, 0}, .boost = true});
        near(fly.state().position, {0, 10, 0});
        const auto initial = fly.state().world;
        fly.setTarget({100, 100, 100}, true);
        near(fly.state().world, initial);
        fly.setMode(CameraControlMode::Orbit);
        near(fly.state().world, initial);
        fly.update(0);
        near(fly.state().world, initial);
        fly.setMode(CameraControlMode::Follow);
        near(fly.state().world, initial);
        fly.update(0);
        near(fly.state().world, initial);
        fly.setMode(CameraControlMode::Free);
        near(fly.state().world, initial);
        config = {};
        config.yaw = radians(179);
        config.pitch = 0;
        config.smoothing = 2;
        fly.reset(config);
        fly.update(.1, {.rotation = {radians(2), 0}});
        require(std::abs(std::remainder(fly.state().yaw - config.yaw, radians(360))) < radians(1),
                "Yaw interpolation took the long arc");
        std::cout << "PASS free flight, ground movement, boost and continuous mode switches\n";

        Scene scene;
        auto parent = scene.createNode(scene.root());
        auto parentMatrix =
            Mat4::translation({3, -2, 5}) * Mat4::rotationY(.4f) * Mat4::scaling({2, 3, 4});
        parentMatrix(0, 1) = .25f;
        scene.setLocalTransform(parent, parentMatrix);
        auto camera = scene.createNode(parent);
        Camera component;
        component.nearPlane = .3f;
        component.farPlane = 321;
        component.aspectRatio = 1.7f;
        component.cullingMask = 7;
        scene.setComponent(camera, component);
        orbit.apply(scene, camera);
        near(scene.worldTransform(camera), orbit.state().world);
        auto applied = *scene.component<Camera>(camera);
        near(applied.nearPlane, .3f);
        near(applied.farPlane, 321);
        near(applied.aspectRatio, 1.7f);
        require(applied.cullingMask == 7 && applied.projection == Camera::Projection::Orthographic,
                "Camera parameters were overwritten");
        const auto oldLocal = scene.localTransform(camera);
        scene.setLocalTransform(parent, Mat4::scaling({1, 0, 1}));
        throws([&] { orbit.apply(scene, camera); });
        near(scene.localTransform(camera), oldLocal);
        auto empty = scene.createNode(scene.root());
        Scene other;
        throws([&] { orbit.apply(scene, empty); });
        throws([&] { orbit.apply(other, camera); });
        throws([&] { orbit.apply(scene, scene.root()); });
        auto compatible = scene.createNode(scene.root());
        scene.setComponent(compatible, Camera{.nearPlane = -2,
                                              .farPlane = 10,
                                              .projection = Camera::Projection::Orthographic});
        orbit.setProjection(Camera::Projection::Perspective);
        throws([&] { orbit.apply(scene, compatible); });
        near(scene.localTransform(compatible), Mat4::identity());
        require(scene.component<Camera>(compatible)->projection == Camera::Projection::Orthographic,
                "Failed apply partially changed Camera");
        std::cout << "PASS parent scale/shear, lens preservation and transactional apply\n";

        const auto old = orbit.state().world;
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        throws([&] { orbit.update(-1); });
        throws([&] { orbit.update(4.01); });
        throws([&] { orbit.update(nan); });
        throws([&] { orbit.update(1, {.rotation = {nan, 0}}); });
        throws([&] { orbit.update(1, {.movement = {2, 0, 0}}); });
        throws([&] { orbit.update(1, {.zoom = std::numeric_limits<float>::infinity()}); });
        throws([&] { orbit.setMode(CameraControlMode(999)); });
        throws([&] { orbit.setProjection(Camera::Projection(999)); });
        throws([&] { orbit.setTarget({nan, 0, 0}); });
        near(orbit.state().world, old);
        config = {};
        config.minPitch = radians(-90);
        throws([&] { orbit.reset(config); });
        near(orbit.state().world, old);
        config = {};
        config.mode = CameraControlMode::Follow;
        config.followOffset = {};
        throws([&] { orbit.reset(config); });
        config = {};
        config.minDistance = 0;
        throws([&] { orbit.reset(config); });
        config = {};
        config.smoothing = -1;
        throws([&] { orbit.reset(config); });
        auto moved = std::move(orbit);
        near(moved.state().world, old);
        throws([&] { orbit.state(); });
        orbit = std::move(moved);
        near(orbit.state().world, old);
        std::cout << "PASS invalid input/configuration and move lifetime\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
