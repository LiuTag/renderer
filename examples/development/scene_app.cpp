// 二次开发手册配套：场景、不可变资源、模型、相机输入与专用渲染线程。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <renderer/model.hpp>
#include <renderer/renderer.hpp>
#include <renderer/window.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
/** 应用私有的轨道相机输入状态，由主线程事件回调更新，不是 SDK 接口。 */
struct OrbitInput {
    renderer::Vec3 target{};
    float yaw = renderer::radians(35), pitch = renderer::radians(25), distance = 6;
    double lastX = 0, lastY = 0;
    bool dragging = false, forward = false, backward = false, left = false, right = false;
    bool up = false, down = false, animated = true, oit = true, fxaa = false;
};
} // namespace

int main(int argc, char **argv) {
    using namespace renderer;
    try {
        bool hidden = false;
        bool enableFxaa = false;
        int frameLimit = 0;
        std::filesystem::path modelPath, capturePath;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::invalid_argument("Missing option value: " + argument);
                return argv[i];
            };
            if (argument == "--hidden")
                hidden = true;
            else if (argument == "--fxaa")
                enableFxaa = true;
            else if (argument == "--frames") {
                const auto text = value();
                size_t consumed = 0;
                frameLimit = std::stoi(text, &consumed);
                if (consumed != text.size() || frameLimit <= 0)
                    throw std::invalid_argument("--frames requires a positive integer");
            } else if (argument == "--model")
                modelPath = value();
            else if (argument == "--capture")
                capturePath = value();
            else
                throw std::invalid_argument("Unknown option: " + argument);
        }
        if (hidden && frameLimit == 0)
            throw std::invalid_argument("--hidden requires --frames");

        // 窗口先创建、最后析构；场景和输入由应用主线程维护。
        Window window({.width = 960,
                       .height = 640,
                       .title = "SDK development / orbit camera",
                       .visible = !hidden,
                       .vsync = !hidden});
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        auto engine = Renderer::createOpenGL(window, {.renderThread = true});
        OrbitInput input;
        input.fxaa = enableFxaa;

        const auto checker = assets.createTexture(geometry::checkerboard());
        const auto solid = assets.createMaterial({.baseColor = {0.85f, 0.35f, 0.12f, 1},
                                                  .baseColorTexture = checker,
                                                  .metallic = 0.2f,
                                                  .roughness = 0.4f});
        const auto ground =
            assets.createMaterial({.baseColor = {0.3f, 0.35f, 0.4f, 1}, .roughness = 0.8f});
        MaterialDesc glassDescription;
        glassDescription.baseColor = {0.15f, 0.6f, 0.9f, 0.35f};
        glassDescription.alphaMode = AlphaMode::Blend;
        glassDescription.doubleSided = true;
        const auto glass = assets.createMaterial(glassDescription);
        const auto cube = assets.createMesh(geometry::cube());
        const auto plane = assets.createMesh(geometry::plane(12));
        const auto group = scene.createNode(scene.root(), "Animated group");
        const auto object = scene.createNode(group, "Cube");
        scene.setComponent(object, MeshRenderer{cube, {solid}});
        const auto floor = scene.createNode(scene.root(), "Floor");
        scene.setComponent(floor, MeshRenderer{plane, {ground}});
        scene.setLocalTransform(floor, Mat4::translation({0, -0.65f, 0}));
        const auto transparent = scene.createNode(scene.root(), "Transparent shell");
        scene.setComponent(transparent, MeshRenderer{cube, {glass}});
        scene.setLocalTransform(transparent, Mat4::scaling({1.5f, 1.5f, 1.5f}));

        if (!modelPath.empty()) {
            const auto model = assets.loadModel(modelPath);
            for (const auto &warning : model.warnings())
                std::cout << "Model warning: " << warning << '\n';
            const auto instance = model.instantiate(scene, scene.root(), "Imported model");
            scene.setLocalTransform(instance.root, Mat4::translation({2, 0, 0}));
        }
        const auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{});
        const auto sun = scene.createNode(scene.root(), "Sun");
        scene.setComponent(sun, Light{});
        scene.setLocalTransform(sun, Mat4::rotationX(radians(-45)));

        // 保存所有订阅令牌；回调只更新输入状态，不直接访问渲染器。
        auto keys = window.subscribe<KeyEvent>([&](const KeyEvent &event) {
            const bool pressed = event.action != InputAction::Release;
            switch (event.key) {
            case Key::W:
                input.forward = pressed;
                break;
            case Key::S:
                input.backward = pressed;
                break;
            case Key::A:
                input.left = pressed;
                break;
            case Key::D:
                input.right = pressed;
                break;
            case Key::Q:
                input.down = pressed;
                break;
            case Key::E:
                input.up = pressed;
                break;
            case Key::Space:
                if (event.action == InputAction::Press)
                    input.animated = !input.animated;
                break;
            case Key::O:
                if (event.action == InputAction::Press)
                    input.oit = !input.oit;
                break;
            case Key::F:
                if (event.action == InputAction::Press)
                    input.fxaa = !input.fxaa;
                break;
            default:
                break;
            }
        });
        auto buttons = window.subscribe<MouseButtonEvent>([&](const auto &event) {
            if (event.button == MouseButton::Right) {
                input.dragging = event.action == InputAction::Press;
                input.lastX = event.x;
                input.lastY = event.y;
            }
        });
        auto motion = window.subscribe<MouseMoveEvent>([&](const auto &event) {
            if (input.dragging) {
                input.yaw -= static_cast<float>(event.x - input.lastX) * 0.005f;
                input.pitch =
                    std::clamp(input.pitch + static_cast<float>(event.y - input.lastY) * 0.005f,
                               radians(-85), radians(85));
            }
            input.lastX = event.x;
            input.lastY = event.y;
        });
        auto scroll = window.subscribe<MouseScrollEvent>([&](const auto &event) {
            input.distance = std::clamp(
                input.distance * std::exp(static_cast<float>(-event.yOffset) * 0.12f), 1.0f, 40.0f);
        });
        auto focus = window.subscribe<WindowFocusEvent>([&](const auto &event) {
            if (!event.focused) {
                // 失焦后可能收不到原按键的 Release，主动清除持续移动状态。
                input.forward = input.backward = input.left = input.right = false;
                input.up = input.down = input.dragging = false;
            }
        });

        RenderView view;
        view.cameraNode = camera;
        view.toneMapping = ToneMapping::ACES;
        auto previous = std::chrono::steady_clock::now();
        float angle = 0;
        int renderedFrames = 0;
        RenderStats lastStats;
        for (;;) {
            window.pollEvents();
            if (window.shouldClose())
                break;
            const auto [width, height] = window.framebufferSize();
            const auto now = std::chrono::steady_clock::now();
            const float dt =
                hidden ? 1.0f / 60
                       : std::min(std::chrono::duration<float>(now - previous).count(), 0.05f);
            previous = now;
            if (width <= 0 || height <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            const Vec3 forward{-std::sin(input.yaw), 0, -std::cos(input.yaw)};
            const Vec3 right{std::cos(input.yaw), 0, -std::sin(input.yaw)};
            input.target = input.target +
                           forward * (float(input.forward) - float(input.backward)) * (dt * 3) +
                           right * (float(input.right) - float(input.left)) * (dt * 3) +
                           Vec3{0, (float(input.up) - float(input.down)) * dt * 3, 0};
            const Vec3 offset{std::cos(input.pitch) * std::sin(input.yaw), std::sin(input.pitch),
                              std::cos(input.pitch) * std::cos(input.yaw)};
            const auto pose =
                Mat4::lookAt(input.target + offset * input.distance, input.target).inverse();
            if (!pose)
                throw std::runtime_error("Camera view matrix is not invertible");
            scene.setLocalTransform(camera, *pose); // camera 的父节点是单位变换根节点。
            if (input.animated)
                angle += dt * 0.6f;
            scene.setLocalTransform(group, Mat4::rotationY(angle));
            view.transparency =
                input.oit ? TransparencyMode::WeightedBlendedOIT : TransparencyMode::SortedAlpha;
            view.antiAliasing = input.fxaa ? AntiAliasing::FXAA : AntiAliasing::None;
            lastStats = engine.render(extractor.extract(scene, assets), view);
            engine.present();
            if (frameLimit && ++renderedFrames >= frameLimit)
                break;
        }
        engine.waitIdle();
        if (!capturePath.empty() && lastStats.sceneInstances != 0) {
            if (!capturePath.parent_path().empty())
                std::filesystem::create_directories(capturePath.parent_path());
            engine.readback().savePpm(capturePath.string());
        }
        std::cout << "Visible: " << lastStats.visibleInstances << ", draws: " << lastStats.drawCalls
                  << '\n';
        return engine.debugErrorCount() == 0 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
