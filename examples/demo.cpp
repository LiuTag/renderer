#include "renderer/model.hpp"
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace renderer;
int main(int argc, char **argv) {
    try {
        WindowConfig windowConfig;
        uint32_t frameLimit = 0, instanceCount = 900;
        std::string capture;
        std::string modelPath;
        bool threaded = false, shadows = true;
        TransparencyMode transparency = TransparencyMode::SortedAlpha;
        AntiAliasing antiAliasing = AntiAliasing::None;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc)
                    throw std::invalid_argument("Missing argument for " + argument);
                return argv[++i];
            };
            auto number = [&](uint32_t maximum) {
                const auto text = value();
                size_t parsed = 0;
                const auto result = std::stoul(text, &parsed);
                if (parsed != text.size() || text.front() == '-' || result > maximum)
                    throw std::invalid_argument("Invalid argument for " + argument);
                return static_cast<uint32_t>(result);
            };
            if (argument == "--hidden")
                windowConfig.visible = false;
            else if (argument == "--no-vsync")
                windowConfig.vsync = false;
            else if (argument == "--fullscreen")
                windowConfig.mode = WindowMode::ExclusiveFullscreen;
            else if (argument == "--borderless")
                windowConfig.mode = WindowMode::BorderlessFullscreen;
            else if (argument == "--monitor")
                windowConfig.fullscreen.monitorIndex = static_cast<int>(number(1024));
            else if (argument == "--frames")
                frameLimit = number(1000000);
            else if (argument == "--instances")
                instanceCount = number(100000);
            else if (argument == "--capture")
                capture = value();
            else if (argument == "--threaded")
                threaded = true;
            else if (argument == "--no-shadows")
                shadows = false;
            else if (argument == "--oit")
                transparency = TransparencyMode::WeightedBlendedOIT;
            else if (argument == "--fxaa")
                antiAliasing = AntiAliasing::FXAA;
            else if (argument == "--model")
                modelPath = value();
            else if (argument == "--help") {
                std::cout << "renderer_demo [--instances N] [--frames N] [--hidden] [--no-vsync] "
                             "[--capture image.ppm] [--threaded] [--no-shadows] [--oit] [--model "
                             "file.gltf] [--fxaa] [--fullscreen | --borderless] [--monitor N]\n"
                             "Space: pause rotation. F11: borderless. Alt+Enter: exclusive. "
                             "F: toggle FXAA. Esc: close.\n";
                return 0;
            } else
                throw std::invalid_argument("Unknown argument: " + argument);
        }
        if (!windowConfig.visible && frameLimit == 0)
            throw std::invalid_argument("--hidden requires a positive --frames limit");
#ifdef RENDERER_DEMO_VULKAN
        windowConfig.graphicsApi = GraphicsApi::Vulkan;
#endif
        Window window(windowConfig);
#ifdef RENDERER_DEMO_VULKAN
        Renderer renderer = Renderer::createVulkan(window, {.renderThread = threaded});
#else
        Renderer renderer = Renderer::createOpenGL(window, {.renderThread = threaded});
#endif
        const auto info = renderer.deviceInfo();
        std::cout << info.backend << " | " << info.device << " | " << info.version << '\n';
        AssetStore assets;
        const auto cube = assets.createMesh(geometry::cube());
        const auto floorMesh = assets.createMesh(geometry::plane(1));
        const auto checker = assets.createTexture(geometry::checkerboard());
        const auto ground = assets.createMaterial(
            {.baseColor = {0.25f, 0.31f, 0.4f, 1}, .baseColorTexture = checker});
        const Vec4 colors[] = {{0.02f, 0.65f, 0.8f, 1},
                               {1.0f, 0.3f, 0.06f, 1},
                               {0.3f, 0.16f, 0.7f, 1},
                               {0.08f, 0.55f, 0.36f, 1},
                               {0.8f, 0.12f, 0.3f, 1}};
        std::vector<MaterialInstanceHandle> materials;
        for (auto color : colors)
            materials.push_back(assets.createMaterial({.baseColor = color}));
        const auto dark = assets.createMaterial({.baseColor = {0.025f, 0.035f, 0.05f, 1}});
        Scene scene;
        auto sun = scene.createNode(scene.root(), "Sun");
        scene.setLocalTransform(sun, *Mat4::lookAt({10, 18, 12}, {0, 0, 0}).inverse());
        scene.setComponent(sun, Light{.intensity = 4});
        auto fill = scene.createNode(scene.root(), "Warm fill");
        scene.setLocalTransform(fill, Mat4::translation({-5, 4, 6}));
        scene.setComponent(fill, Light{.type = LightType::Point,
                                       .color = {1, 0.3f, 0.1f},
                                       .intensity = 40,
                                       .range = 18,
                                       .castShadow = false});
        const auto camera = scene.createNode(scene.root(), "MainCamera");
        scene.setLocalTransform(camera, *Mat4::lookAt({23, 22, 29}, {0, 0, -8}).inverse());
        scene.setComponent(camera,
                           Camera{.verticalFov = radians(55), .nearPlane = 0.1f, .farPlane = 500});
        const auto floor = scene.createNode(scene.root(), "Ground");
        scene.setLocalTransform(floor,
                                Mat4::translation({0, -0.55f, -10}) * Mat4::scaling({80, 1, 80}));
        scene.setComponent(floor, MeshRenderer{floorMesh, {ground}});
        const auto sphere = assets.createMesh(geometry::sphere());
        for (int i = 0; i < 5; ++i) {
            auto ball = scene.createNode(scene.root(), "PBR sphere");
            auto material =
                assets.createMaterial({.baseColor = {0.9f, 0.55f, 0.15f, 1},
                                       .metallic = static_cast<float>(i) / 4,
                                       .roughness = 0.12f + 0.17f * static_cast<float>(i)});
            scene.setLocalTransform(
                ball, Mat4::translation({-7 + 3.5f * static_cast<float>(i), 0.75f, 0}) *
                          Mat4::scaling({2.4f, 2.4f, 2.4f}));
            scene.setComponent(ball, MeshRenderer{sphere, {material}});
        }
        auto glass = assets.createMaterial({.baseColor = {0.05f, 0.5f, 0.9f, 0.35f},
                                            .alphaMode = AlphaMode::Blend,
                                            .roughness = 0.15f,
                                            .doubleSided = true});
        auto pane = scene.createNode(scene.root(), "Transparent pane");
        scene.setLocalTransform(pane, Mat4::translation({5, 1.4f, 4}) * Mat4::rotationY(-0.4f) *
                                          Mat4::scaling({3, 3, 0.08f}));
        scene.setComponent(pane, MeshRenderer{cube, {glass}});
        if (!modelPath.empty()) {
            auto model = assets.loadModel(modelPath);
            auto instance = model.instantiate(scene, scene.root(), "Imported Model");
            scene.setLocalTransform(instance.root,
                                    Mat4::translation({-5, 1, 5}) * Mat4::scaling({3, 3, 3}));
            for (const auto &warning : model.warnings())
                std::cout << "Import: " << warning << '\n';
        }
        const auto field = scene.createNode(scene.root(), "InstancedField");
        const auto side = static_cast<uint32_t>(
            std::ceil(std::sqrt(static_cast<float>(std::max(instanceCount, 1u)))));
        for (uint32_t i = 0; i < instanceCount; ++i) {
            const auto node = scene.createNode(field, "Cube");
            const float x =
                (static_cast<float>(i % side) - static_cast<float>(side - 1) * 0.5f) * 1.4f;
            const float z = -6 - static_cast<float>(i / side) * 1.4f;
            const float h = 0.65f + 0.6f * (0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.73f));
            scene.setLocalTransform(node, Mat4::translation({x, (h - 1) * 0.5f, z}) *
                                              Mat4::rotationY(static_cast<float>(i) * 0.13f) *
                                              Mat4::scaling({0.7f, h, 0.7f}));
            scene.setComponent(node, MeshRenderer{cube, {materials[i % materials.size()]}});
        }
        const auto car = scene.createNode(scene.root(), "Car");
        auto part = [&](const char *name, Vec3 position, Vec3 scale,
                        MaterialInstanceHandle material) {
            const auto node = scene.createNode(car, name);
            scene.setLocalTransform(node, Mat4::translation(position) * Mat4::scaling(scale));
            scene.setComponent(node, MeshRenderer{cube, {material}});
            return node;
        };
        part("Body", {0, 0.55f, 0}, {4, 0.8f, 1.8f}, materials[0]);
        part("Cabin", {-0.3f, 1.2f, 0}, {1.9f, 0.7f, 1.5f}, materials[2]);
        std::vector<NodeHandle> wheels;
        for (float x : {-1.25f, 1.25f})
            for (float z : {-0.95f, 0.95f})
                wheels.push_back(part("Wheel", {x, 0.1f, z}, {0.65f, 0.65f, 0.32f}, dark));
        RenderExtractor extractor;
        const auto start = std::chrono::steady_clock::now();
        uint32_t frame = 0;
        bool paused = false;
        auto pauseEvents = window.subscribe<KeyEvent>([&](const KeyEvent &event) {
            if (event.key == Key::Space && event.action == InputAction::Press)
                paused = !paused;
            if (event.key == Key::F && event.action == InputAction::Press)
                antiAliasing =
                    antiAliasing == AntiAliasing::None ? AntiAliasing::FXAA : AntiAliasing::None;
            if (event.action == InputAction::Press &&
                (event.key == Key::F11 ||
                 (event.key == Key::Enter && hasModifier(event.modifiers, Modifiers::Alt)))) {
                const auto fullscreen = event.key == Key::F11 ? WindowMode::BorderlessFullscreen
                                                              : WindowMode::ExclusiveFullscreen;
                window.setMode(window.mode() == WindowMode::Windowed ? fullscreen
                                                                     : WindowMode::Windowed,
                               windowConfig.fullscreen);
            }
        });
        float rotation = 0;
        RenderStats stats;
        auto previous = start;
        while (!window.shouldClose() && (frameLimit == 0 || frame < frameLimit)) {
            window.pollEvents();
            const auto now = std::chrono::steady_clock::now();
            const float dt =
                frameLimit ? 1.0f / 60.0f
                           : std::min(std::chrono::duration<float>(now - previous).count(), 0.1f);
            previous = now;
            if (!paused)
                rotation += dt * 0.45f;
            scene.setLocalTransform(car, Mat4::translation({0, 0, 4}) * Mat4::rotationY(rotation));
            auto snapshot = extractor.extract(scene, assets);
            stats = renderer.render(snapshot, RenderView{.cameraNode = camera,
                                                         .shadows = shadows,
                                                         .toneMapping = ToneMapping::ACES,
                                                         .transparency = transparency,
                                                         .antiAliasing = antiAliasing});
            if (frame % 30 == 0) {
                std::ostringstream title;
                title << info.backend << " | " << stats.visibleInstances << " visible | "
                      << stats.drawCalls << " draws | CPU " << std::fixed << std::setprecision(2)
                      << stats.prepareMilliseconds + stats.submitMilliseconds
                      << " ms | Space: pause | F11: fullscreen | FXAA: "
                      << (antiAliasing == AntiAliasing::FXAA ? "on" : "off");
                if (stats.gpuTimingValid)
                    title << " | GPU " << stats.gpuMilliseconds << " ms";
                window.setTitle(title.str());
            }
            renderer.present();
            ++frame;
        }
        if (!capture.empty() && frame > 0) {
            const auto parent = std::filesystem::path(capture).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            renderer.readback().savePpm(capture);
            std::cout << "Captured " << capture << '\n';
        }
        renderer.waitIdle();
        std::cout << "Frames: " << frame << ", visible: " << stats.visibleInstances
                  << ", culled: " << stats.culledInstances << ", draws: " << stats.drawCalls
                  << ", triangles: " << stats.triangles
                  << ", GL errors: " << renderer.debugErrorCount() << '\n';
        return renderer.debugErrorCount() ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
