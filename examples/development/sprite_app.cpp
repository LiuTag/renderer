/** @file @brief 三维地面、方向序列帧、图片树石、脚底排序、鼠标交互与圆形障碍示例。 */
#include "../common/forest_assets.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <renderer/animation/sprite_animation.hpp>
#include <renderer/interaction/picking.hpp>
#include <renderer/window.hpp>
#include <stdexcept>
#include <string>
#include <thread>

using namespace renderer;
namespace {
struct Obstacle {
    Vec3 position;
    float radius;
};
struct Input {
    bool forward = false, backward = false, left = false, right = false, rotateLeft = false,
         rotateRight = false;
    bool fxaa = true, oit = false;
    float yaw = radians(20), height = 12;
    std::vector<MouseButtonEvent> clicks;
};
} // namespace
int main(int argc, char **argv) {
    try {
        bool hidden = false, threaded = false, selfTest = false;
        int limit = 0;
        std::filesystem::path capture;
        Input input;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::invalid_argument("Missing option value");
                return argv[i];
            };
            if (arg == "--hidden")
                hidden = true;
            else if (arg == "--threaded")
                threaded = true;
            else if (arg == "--oit")
                input.oit = true;
            else if (arg == "--no-fxaa")
                input.fxaa = false;
            else if (arg == "--self-test")
                selfTest = true;
            else if (arg == "--capture")
                capture = value();
            else if (arg == "--frames") {
                const auto text = value();
                size_t consumed = 0;
                limit = std::stoi(text, &consumed);
                if (limit <= 0 || consumed != text.size())
                    throw std::invalid_argument("Invalid frame limit");
            } else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        if (hidden && !limit)
            throw std::invalid_argument("--hidden requires --frames");
        WindowConfig windowConfig{
            .width = 1100,
            .height = 760,
            .title = "Sprite forest / WASD move / Q E rotate / click select or move",
            .visible = !hidden,
            .vsync = !hidden};
#ifdef RENDERER_DEMO_VULKAN
        windowConfig.graphicsApi = GraphicsApi::Vulkan;
#endif
        Window window(windowConfig);
#ifdef RENDERER_DEMO_VULKAN
        auto renderer = Renderer::createVulkan(window, {.renderThread = threaded});
#else
        auto renderer = Renderer::createOpenGL(window, {.renderThread = threaded});
#endif
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        SceneQueryFilter queryFilter;
        const auto texture = assets.createTexture(forest_assets::makeAtlas());
        const auto entries = forest_assets::makeEntries();
        SpriteAtlas atlas(assets, texture, entries);
        const auto sprite = [&](int i) { return atlas.sprite(std::to_string(i)); };
        const auto add = [&](NodeHandle parent, std::string_view name, SpriteHandle image,
                             Vec3 position) {
            const auto n = scene.createNode(parent, name);
            scene.setComponent(n, SpriteRenderer{.sprite = image});
            scene.setLocalTransform(n, Mat4::translation(position));
            return n;
        };
        const auto shadow = [&](Vec3 p, float scale) {
            const auto n = add(scene.root(), "Ground shadow", sprite(21), p);
            scene.setLocalTransform(n, Mat4::translation(p) * Mat4::rotationX(radians(-90)));
            auto r = *scene.component<SpriteRenderer>(n);
            r.billboard = BillboardMode::Fixed;
            r.sizeMultiplier = {scale, scale * .6f};
            r.sortingLayer = -10;
            queryFilter.excludedNodes.push_back(n);
            scene.setComponent(n, r);
            return n;
        };
        auto groundTexture = geometry::checkerboard(128, 24);
        for (size_t i = 0; i < groundTexture.pixels.size(); i += 4) {
            const bool light = groundTexture.pixels[i] > 100;
            groundTexture.pixels[i] = light ? 101 : 97;
            groundTexture.pixels[i + 1] = light ? 115 : 111;
            groundTexture.pixels[i + 2] = light ? 70 : 66;
        }
        const auto groundMaterial = assets.createMaterial(
            {.baseColorTexture = assets.createTexture(groundTexture), .unlit = true});
        const auto ground = scene.createNode(scene.root(), "Ground");
        scene.setComponent(ground, MeshRenderer{assets.createMesh(geometry::plane(34)),
                                                {groundMaterial},
                                                0xFFFFFFFFu,
                                                false,
                                                false});
        const auto camera = scene.createNode(scene.root(), "Camera");
        Camera cameraData;
        cameraData.projection = Camera::Projection::Orthographic;
        scene.setComponent(camera, cameraData);
        const auto playerRoot = scene.createNode(scene.root(), "Player ground anchor");
        scene.setComponent(playerRoot, SortingGroup{});
        const auto player = add(playerRoot, "Character", sprite(0), {});
        const auto hat = add(playerRoot, "Hat attachment", sprite(22), {});
        auto hatData = *scene.component<SpriteRenderer>(hat);
        hatData.sortingOrder = 1;
        queryFilter.excludedNodes.push_back(hat);
        scene.setComponent(hat, hatData);
        const auto playerShadow = shadow({0, .015f, 0}, .75f);
        const auto marker = add(scene.root(), "Move target", sprite(23), {0, .02f, 0});
        auto markerData = *scene.component<SpriteRenderer>(marker);
        markerData.billboard = BillboardMode::Fixed;
        markerData.sizeMultiplier = {.5f, .5f};
        queryFilter.excludedNodes.push_back(marker);
        markerData.sortingLayer = -9;
        scene.setComponent(marker, markerData);
        scene.setVisible(marker, false);
        std::vector<Obstacle> obstacles;
        std::vector<NodeHandle> objects;
        std::mt19937 random(42);
        std::uniform_real_distribution<float> jitter(-.5f, .5f);
        for (int z = -3; z <= 3; ++z)
            for (int x = -4; x <= 4; ++x) {
                if (std::abs(x) <= 1 && std::abs(z) <= 1)
                    continue;
                const Vec3 p{float(x) * 2 + jitter(random), 0, float(z) * 2 + jitter(random)};
                const bool tree = (x - z + 20) % 3 != 0;
                const int index = tree ? 16 + int(random() % 3) : 19 + int(random() % 2);
                const auto n = add(scene.root(), tree ? "Tree" : "Rock", sprite(index), p);
                objects.push_back(n);
                auto r = *scene.component<SpriteRenderer>(n);
                const float scale = .85f + float(random() % 25) / 100;
                r.sizeMultiplier = {scale, scale};
                r.flipX = (random() % 2) == 0;
                scene.setComponent(n, r);
                shadow(p + Vec3{0, .012f, 0}, tree ? 1.05f : .65f);
                obstacles.push_back({p, tree ? .36f : .42f});
            }
        for (int i = 0; i < 30; ++i) {
            Vec3 p{jitter(random) * 17, 0, jitter(random) * 13};
            const auto n = add(scene.root(), "Flower", sprite(24), p);
            auto r = *scene.component<SpriteRenderer>(n);
            queryFilter.excludedNodes.push_back(n);
            r.sizeMultiplier = {.55f, .55f};
            scene.setComponent(n, r);
        }
        SpriteAnimationClipDesc idleDesc, walkDesc;
        for (int d = 0; d < 4; ++d) {
            idleDesc.directions.push_back({{sprite(d * 4), .5, {}}});
            std::vector<SpriteAnimationFrame> frames;
            for (int f = 0; f < 4; ++f)
                frames.push_back({sprite(d * 4 + f), .13, f == 1 || f == 3 ? "footstep" : ""});
            walkDesc.directions.push_back(frames);
        }
        SpriteAnimationClip idle(idleDesc), walk(walkDesc);
        SpriteAnimator animator;
        animator.play(idle);
        auto keys = window.subscribe<KeyEvent>([&](const auto &e) {
            const bool pressed = e.action != InputAction::Release;
            switch (e.key) {
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
                input.rotateLeft = pressed;
                break;
            case Key::E:
                input.rotateRight = pressed;
                break;
            case Key::F:
                if (e.action == InputAction::Press)
                    input.fxaa = !input.fxaa;
                break;
            case Key::O:
                if (e.action == InputAction::Press)
                    input.oit = !input.oit;
                break;
            case Key::Escape:
                if (e.action == InputAction::Press)
                    window.requestClose();
                break;
            default:
                break;
            }
        });
        auto buttons = window.subscribe<MouseButtonEvent>([&](const auto &e) {
            if (e.action == InputAction::Press)
                input.clicks.push_back(e);
        });
        auto scroll = window.subscribe<MouseScrollEvent>([&](const auto &e) {
            input.height =
                std::clamp(input.height * std::exp(float(-e.yOffset) * .1f), 5.0f, 24.0f);
        });
        auto focus = window.subscribe<WindowFocusEvent>([&](const auto &e) {
            if (!e.focused)
                input.forward = input.backward = input.left = input.right = input.rotateLeft =
                    input.rotateRight = false;
        });
        RenderView view{.cameraNode = camera,
                        .clearColor = {.08f, .10f, .055f, 1},
                        .shadows = false,
                        .antiAliasing = AntiAliasing::FXAA};
        RenderSnapshot displayed;
        RenderView displayedView;
        int displayedWidth = 0, displayedHeight = 0;
        Vec3 position{}, facing{0, 0, 1};
        std::optional<Vec3> target;
        NodeHandle selected;
        bool walking = false;
        size_t footsteps = 0;
        int frame = 0;
        RenderStats stats;
        auto previous = std::chrono::steady_clock::now();
        const auto blocked = [&](Vec3 p) {
            for (const auto &o : obstacles) {
                const auto delta = p - o.position;
                if (dot(delta, delta) < (o.radius + .18f) * (o.radius + .18f))
                    return true;
            }
            return std::abs(p.x) > 15 || std::abs(p.z) > 15;
        };
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.shouldClose())
                break;
            const auto [width, height] = window.framebufferSize();
            const auto now = std::chrono::steady_clock::now();
            const float dt =
                hidden || selfTest
                    ? 1.0f / 60
                    : std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0f, .05f);
            previous = now;
            if (width <= 0 || height <= 0) {
                input.clicks.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (displayedWidth > 0 && !input.clicks.empty()) {
                SceneQuery query(displayed, displayedView, uint32_t(displayedWidth),
                                 uint32_t(displayedHeight), queryFilter);
                const auto [cw, ch] = window.clientSize();
                for (const auto &click : input.clicks) {
                    if (cw <= 0 || ch <= 0)
                        continue;
                    const Vec2 pixel{float(click.x) * float(displayedWidth) / float(cw),
                                     float(click.y) * float(displayedHeight) / float(ch)};
                    const auto hit = query.pickSprite(pixel);
                    if (click.button == MouseButton::Left && hit &&
                        std::find(objects.begin(), objects.end(), hit->node) != objects.end()) {
                        if (selected && scene.valid(selected)) {
                            auto r = *scene.component<SpriteRenderer>(selected);
                            r.tint = {1, 1, 1, 1};
                            scene.setComponent(selected, r);
                        }
                        selected = hit->node;
                        auto r = *scene.component<SpriteRenderer>(selected);
                        r.tint = {1.35f, 1.2f, .6f, 1};
                        scene.setComponent(selected, r);
                        std::cout << "Selected " << scene.name(selected)
                                  << " node=" << selected.index << '\n';
                    } else if (click.button == MouseButton::Left ||
                               click.button == MouseButton::Right)
                        target = query.groundPoint(pixel);
                }
            }
            input.clicks.clear();
            if (selfTest) {
                input.yaw += dt * .25f;
                target =
                    Vec3{std::sin(float(frame) * dt) * 2.2f, 0, std::cos(float(frame) * dt) * 2.2f};
            }
            input.yaw += (float(input.rotateLeft) - float(input.rotateRight)) * dt * 1.2f;
            const Vec3 back{std::sin(input.yaw), 0, std::cos(input.yaw)}, right{back.z, 0, -back.x};
            Vec3 direction = right * (float(input.right) - float(input.left)) +
                             back * (float(input.backward) - float(input.forward));
            const bool keyboard = dot(direction, direction) > 1e-6f;
            float remaining = std::numeric_limits<float>::infinity();
            if (keyboard)
                target.reset();
            else if (target) {
                direction = *target - position;
                remaining = std::sqrt(dot(direction, direction));
                if (remaining < .04f) {
                    target.reset();
                    direction = {};
                }
            }
            const bool moving = dot(direction, direction) > 1e-6f;
            const auto oldPosition = position;
            if (moving) {
                facing = normalized(direction);
                const auto step = facing * std::min(remaining, dt * 2.4f);
                Vec3 next = position + step;
                if (!blocked(next))
                    position = next;
                else {
                    next = {position.x + step.x, 0, position.z};
                    if (!blocked(next))
                        position = next;
                    next = {position.x, 0, position.z + step.z};
                    if (!blocked(next))
                        position = next;
                }
            }
            const bool actualMove = dot(position - oldPosition, position - oldPosition) > 1e-10f;
            if (actualMove != walking) {
                animator.play(actualMove ? walk : idle);
                walking = actualMove;
            }
            scene.setLocalTransform(playerRoot, Mat4::translation(position));
            scene.setLocalTransform(playerShadow, Mat4::translation(position + Vec3{0, .015f, 0}) *
                                                      Mat4::rotationX(radians(-90)));
            scene.setVisible(marker, bool(target));
            if (target)
                scene.setLocalTransform(marker, Mat4::translation(*target + Vec3{0, .025f, 0}) *
                                                    Mat4::rotationX(radians(-90)));
            cameraData.orthographicHeight = input.height;
            scene.setComponent(camera, cameraData);
            const auto cameraWorld = *Mat4::lookAt(back * 12 + Vec3{0, 9, 0}, {0, 0, 0}).inverse();
            scene.setLocalTransform(camera, cameraWorld);
            animator.advance(dt);
            animator.selectDirection(facing, cameraWorld);
            animator.apply(scene, player);
            for (const auto &event : animator.takeEvents())
                if (event.name == "footstep")
                    ++footsteps;
            view.transparency =
                input.oit ? TransparencyMode::WeightedBlendedOIT : TransparencyMode::SortedAlpha;
            view.antiAliasing = input.fxaa ? AntiAliasing::FXAA : AntiAliasing::None;
            displayed = extractor.extract(scene, assets);
            displayedView = view;
            displayedWidth = width;
            displayedHeight = height;
            stats = renderer.render(displayed, view);
            renderer.present();
            if (frame % 30 == 0)
                window.setTitle("Sprite forest / WASD move / Q E rotate / click select / F FXAA / "
                                "O OIT | draws " +
                                std::to_string(stats.sceneDrawCalls));
            ++frame;
            if (limit && frame >= limit)
                break;
        }
        if (!capture.empty())
            renderer.readback().savePpm(capture.string());
        renderer.waitIdle();
        if (selfTest) {
            if (stats.visibleInstances < 10 || footsteps == 0 || !displayed.spriteCount())
                throw std::runtime_error(
                    "Sprite demo self-test did not exercise animation and world");
            SceneQuery query(displayed, displayedView, uint32_t(displayedWidth),
                             uint32_t(displayedHeight), queryFilter);
            const auto screen = query.worldToScreen(position);
            if (!screen || !query.groundPoint({screen->x, screen->y}))
                throw std::runtime_error("Ground projection self-test failed");
        }
        std::cout << "sprites=" << displayed.spriteCount() << " visible=" << stats.visibleInstances
                  << " sceneDraws=" << stats.sceneDrawCalls << " footsteps=" << footsteps
                  << " GLerrors=" << renderer.debugErrorCount() << '\n';
        return renderer.debugErrorCount() ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
