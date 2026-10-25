/** @file @brief 应用层组织十二个扩展：流式营地、物理角色、方向动画、交互、粒子、环境和声音。 */
#include "application.hpp"
#include "../common/forest_assets.hpp"
#include "audio_system.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <renderer/animation/sprite_animation.hpp>
#include <renderer/camera/camera_controller.hpp>
#include <renderer/collision/collision.hpp>
#include <renderer/environment/environment.hpp>
#include <renderer/interaction/picking.hpp>
#include <renderer/model.hpp>
#include <renderer/particles/particles.hpp>
#include <renderer/physics2d/physics2d.hpp>
#include <renderer/presentation/presentation.hpp>
#include <renderer/text/text.hpp>
#include <renderer/tilemap/streaming.hpp>
#include <renderer/ui/ui.hpp>
#include <renderer/window.hpp>
#include <thread>
#include <tuple>

namespace game_demo {
using namespace renderer;
namespace {
constexpr uint64_t playerId = 1, crateId = 2;
constexpr Vec3 campfirePosition{3, 0, 1};
CameraControllerConfig cameraConfig() {
    CameraControllerConfig config;
    config.mode = CameraControlMode::Follow;
    config.projection = Camera::Projection::Orthographic;
    const auto yaw = radians(20);
    config.followOffset = {std::sin(yaw) * 15, 10, std::cos(yaw) * 15};
    config.orthographicHeight = 14;
    config.minOrthographicHeight = 6;
    config.maxOrthographicHeight = 24;
    config.smoothing = 8;
    return config;
}
using ChunkKey = std::pair<int32_t, int32_t>;
uint64_t id(TileChunkCoord c) {
    return (uint64_t(uint32_t(c.x)) << 32) | uint32_t(c.z);
}
Vec3 world(Vec2 p) {
    return {p.x, 0, p.y};
}
float distance(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::filesystem::path chunkFile(const std::filesystem::path &root, TileChunkCoord c) {
    return root / (std::to_string(c.x) + "_" + std::to_string(c.z) + ".rtile");
}
struct Object {
    NodeHandle node;
    Body2DHandle body;
    ColliderHandle collider;
    TileChunkCoord chunk;
    uint32_t bit = 0;
};
struct Chunk {
    std::vector<Object> objects;
    SpriteBatch decorations;
};
struct Counters {
    uint64_t contacts = 0, sensors = 0, picks = 0, groundQueries = 0, collisionQueries = 0,
             saves = 0, edits = 0;
    uint64_t animationEvents = 0, frames = 0, collected = 0, pausedFrames = 0;
    uint64_t uiActions = 0;
    bool sorted = false, oit = false, fxaa = false, noFxaa = false, day = false, night = false,
         rain = false, snow = false;
};
} // namespace

struct Application::Impl {
    Options options;
    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    Physics2D physics{Physics2DConfig{.workerCount = 2}};
    CameraController cameraControl{cameraConfig()};
    TextSystem worldText;
    WorldTextId playerName = 0, fireName = 0, damageText = 0;
    float damageAge = 10;
    CollisionWorld collision;
    std::unique_ptr<AudioSystem> audio;
    std::unique_ptr<SpriteAtlas> atlas;
    SpriteHandle maskRock;
    std::unique_ptr<TileStreamer> streamer;
    std::map<ChunkKey, Chunk> chunks;
    std::unique_ptr<SpriteAnimationClip> idle, walk;
    SpriteAnimator animator;
    std::unique_ptr<ParticleSystem> fire, smoke, rain, snow, sparks;
    EnvironmentController environment{
        EnvironmentConfig{.dayLength = 120, .hour = 13, .weatherTransition = .35f}};
    SpritePresentation presentation{SpritePresentationConfig{.occluderMask = 2}};
    NodeHandle camera, sun, playerRoot, player, hat, marker, selected, rope;
    Body2DHandle playerBody, crateBody, logBody, anchorBody;
    RenderView view;
    RenderSnapshot displayed;
    RenderView displayedView;
    uint32_t displayedWidth = 0, displayedHeight = 0;
    uint32_t targetWidth = 0, targetHeight = 0;
    RenderStats renderStats;
    TileStreamStats streamStats;
    Counters stats;
    std::vector<EventSubscription> subscriptions;
    UiContext ui;
    UiDocumentId uiDocument = 0;
    std::vector<UiSubscription> uiSubscriptions;
    bool uiCaptured = false;
    std::vector<MouseButtonEvent> clicks;
    std::vector<ColliderHandle> nearby;
    std::optional<Vec3> destination;
    std::optional<std::tuple<TileChunkCoord, int32_t, int32_t, uint32_t>> edited;
    bool forward = false, backward = false, left = false, right = false, orbitLeft = false,
         orbitRight = false;
    bool walking = false, paused = false, grade = false, screenshot = false, close = false;
    float zoomDelta = 0, impactCooldown = 0;
    Vec2 position{};
    Vec3 facing{0, 0, 1};
    uint64_t nextObject = 100;

    explicit Impl(const Options &input)
        : options(input), targetWidth(input.width), targetHeight(input.height) {
        std::filesystem::create_directories(options.cache);
        createRenderer();
        audio = std::make_unique<AudioSystem>(options.cache / "audio", options.offlineAudio,
                                              options.audioCapture, campfirePosition);
        atlas = std::make_unique<SpriteAtlas>(
            assets, assets.createTexture(forest_assets::makeAtlas()), forest_assets::makeEntries());
        auto rock = assets.spriteDescription(image(19));
        rock.alphaMode = AlphaMode::Mask;
        rock.unlit = false;
        maskRock = assets.createSprite(rock);
        createActors();
        createText();
        createParticles();
        createMap();
        createUi();
        subscribe();
        // 启动阶段只等待首次周边发布；主循环后续读取完全由流式预算驱动。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            streamStats = streamer->update({}, assets);
            streamEvents();
            if (streamStats.residentChunks >= 25)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        require(streamStats.residentChunks >= 25, "Initial streamed map did not become ready");
        std::cout
            << "Forest Camp / " << renderer->deviceInfo().backend << " / "
            << (options.offscreen ? "offscreen" : "window") << " / audio "
            << (audio->statistics().offline ? "offline" : "device") << '\n'
            << "WASD move, Q/Z orbit, wheel zoom, left select, right move, E interact, Space hit\n"
            << "N day/night, R weather, O OIT, F FXAA, H shadows, C contacts, G grade, "
               "M music mute, B music, P pause, T edit, K save, F11 fullscreen, F12 PNG\n";
    }
    void createRenderer() {
        auto backend = options.backend;
        if (backend == "auto") {
#ifdef RENDERER_GAME_VULKAN
            backend = "vulkan";
#else
            backend = "opengl";
#endif
        }
        const auto api = backend == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::OpenGL;
#ifndef RENDERER_GAME_OPENGL
        if (api == GraphicsApi::OpenGL)
            throw std::invalid_argument("OpenGL backend is unavailable");
#endif
#ifndef RENDERER_GAME_VULKAN
        if (api == GraphicsApi::Vulkan)
            throw std::invalid_argument("Vulkan backend is unavailable");
#endif
        if (!options.offscreen)
            window = std::make_unique<Window>(
                WindowConfig{.width = int(options.width),
                             .height = int(options.height),
                             .title = "Forest Camp",
                             .visible = !options.hidden,
                             .vsync = !options.frames,
                             .mode = options.exclusive    ? WindowMode::ExclusiveFullscreen
                                     : options.fullscreen ? WindowMode::BorderlessFullscreen
                                                          : WindowMode::Windowed,
                             .graphicsApi = api});
        const RendererConfig config{.renderThread = options.threaded};
#ifdef RENDERER_GAME_OPENGL
        if (api == GraphicsApi::OpenGL) {
            renderer = std::make_unique<Renderer>(
                options.offscreen
                    ? Renderer::createOpenGLOffscreen({options.width, options.height}, config)
                    : Renderer::createOpenGL(*window, config));
            return;
        }
#endif
#ifdef RENDERER_GAME_VULKAN
        if (api == GraphicsApi::Vulkan) {
            renderer = std::make_unique<Renderer>(
                options.offscreen
                    ? Renderer::createVulkanOffscreen({options.width, options.height}, config)
                    : Renderer::createVulkan(*window, config));
            return;
        }
#endif
        throw std::invalid_argument("Requested game backend is unavailable");
    }
    SpriteHandle image(int index) const {
        return atlas->sprite(std::to_string(index));
    }
    NodeHandle sprite(SpriteHandle handle, Vec3 p, uint32_t layer = 1, NodeHandle parent = {}) {
        auto node = scene.createNode(parent ? parent : scene.root());
        scene.setComponent(node, SpriteRenderer{.sprite = handle, .layerMask = layer});
        scene.setLocalTransform(node, Mat4::translation(p));
        return node;
    }
    void shadow(NodeHandle parent, float scale) {
        auto node = sprite(image(21), {0, .014f, 0}, 4, parent);
        scene.setLocalTransform(node,
                                Mat4::translation({0, .014f, 0}) * Mat4::rotationX(radians(-90)));
        auto r = *scene.component<SpriteRenderer>(node);
        r.billboard = BillboardMode::Fixed;
        r.sizeMultiplier = {scale, scale * .6f};
        r.sortingLayer = -10;
        scene.setComponent(node, r);
    }
    void createActors() {
        camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic});
        sun = scene.createNode(scene.root(), "Sun");
        scene.setComponent(sun, Light{});
        view.cameraNode = camera;
        view.shadowResolution = 512;
        view.contactShadows = true;
        view.contactShadowDistance = 1.5f;
        view.contactShadowThickness = .2f;
        view.transparency =
            options.oit ? TransparencyMode::WeightedBlendedOIT : TransparencyMode::SortedAlpha;
        view.antiAliasing = options.fxaa ? AntiAliasing::FXAA : AntiAliasing::None;
        view.toneMapping = ToneMapping::ACES;
        playerRoot = scene.createNode(scene.root(), "Player");
        scene.setComponent(playerRoot, SortingGroup{});
        player = sprite(image(0), {}, 1, playerRoot);
        hat = sprite(image(22), {}, 1, playerRoot);
        auto hatData = *scene.component<SpriteRenderer>(hat);
        hatData.sortingOrder = 1;
        scene.setComponent(hat, hatData);
        shadow(playerRoot, .8f);
        Body2DDesc body;
        body.lockRotation = true;
        body.userData = playerId;
        body.shapes[0].radius = .25f;
        body.shapes[0].friction = 0;
        body.shapes[0].hitEvents = true;
        playerBody = physics.createBody(body);
        physics.bindNode(playerBody, scene, playerRoot);
        body.position = {.85f, 0};
        body.userData = crateId;
        body.linearDamping = .9f;
        body.shapes[0].type = Shape2DType::Box;
        body.shapes[0].halfExtents = {.28f, .28f};
        crateBody = physics.createBody(body);
        auto crate = scene.createNode(scene.root(), "Pushable crate");
        scene.setLocalTransform(crate, Mat4::translation({.85f, .3f, 0}) *
                                           Mat4::scaling({.55f, .6f, .55f}));
        auto wood = assets.createMaterial({.baseColor = {.5f, .28f, .08f, 1}, .roughness = .85f});
        scene.setComponent(crate, MeshRenderer{assets.createMesh(geometry::cube()), {wood}});
        physics.bindNode(crateBody, scene, crate, true);
        // 传感器提供营地进入事件；物理只更新 Scene，应用映射成音效。
        body = {};
        body.type = Body2DType::Static;
        body.userData = 10;
        body.position = {campfirePosition.x, campfirePosition.z};
        body.shapes[0].sensor = true;
        body.shapes[0].radius = 2;
        physics.createBody(body);
        auto light = scene.createNode(scene.root(), "Campfire light");
        scene.setLocalTransform(light, Mat4::translation(campfirePosition + Vec3{0, .8f, 0}));
        scene.setComponent(light, Light{.type = LightType::Point,
                                        .color = {1, .45f, .12f},
                                        .intensity = 4,
                                        .range = 7,
                                        .castShadow = false});
        marker = sprite(image(23), {0, .025f, 0}, 4);
        auto mark = *scene.component<SpriteRenderer>(marker);
        mark.billboard = BillboardMode::Fixed;
        mark.sortingLayer = -9;
        mark.sizeMultiplier = {.6f, .6f};
        scene.setComponent(marker, mark);
        scene.setVisible(marker, false);
        SpriteAnimationClipDesc idleDesc, walkDesc;
        for (int d = 0; d < 4; ++d) {
            idleDesc.directions.push_back({{image(d * 4), .5, {}}});
            std::vector<SpriteAnimationFrame> frames;
            for (int f = 0; f < 4; ++f)
                frames.push_back({image(d * 4 + f), .13, f == 1 || f == 3 ? "footstep" : ""});
            walkDesc.directions.push_back(frames);
        }
        idle = std::make_unique<SpriteAnimationClip>(idleDesc);
        walk = std::make_unique<SpriteAnimationClip>(walkDesc);
        animator.play(*idle);
        // 展示距离约束和不同碰撞形状，不让它们参与角色输入控制。
        body = {};
        body.type = Body2DType::Static;
        body.position = {-2, 2};
        body.shapes[0].radius = .15f;
        auto anchor = physics.createBody(body);
        anchorBody = anchor;
        body.type = Body2DType::Dynamic;
        body.position = {-1, 2};
        body.shapes[0].type = Shape2DType::Capsule;
        body.shapes[0].pointA = {-.2f, 0};
        body.shapes[0].pointB = {.2f, 0};
        body.shapes[0].radius = .2f;
        auto log = physics.createBody(body);
        logBody = log;
        auto stone = sprite(image(19), {-1, 0, 2});
        physics.bindNode(log, scene, stone, true);
        physics.createDistanceJoint(
            {.a = anchor, .b = log, .length = 1, .hertz = 2, .spring = true});
        physics.applyImpulse(log, {0, .8f});
        rope = scene.createNode(scene.root(), "Distance joint rope");
        scene.setComponent(rope, MeshRenderer{assets.createMesh(geometry::cube()), {wood}});
        body = {};
        body.type = Body2DType::Static;
        body.position = {-2, -2};
        body.shapes[0].type = Shape2DType::Polygon;
        body.shapes[0].vertices = {{-.4f, -.3f}, {.4f, -.3f}, {0, .4f}};
        physics.createBody(body);
        createModel();
    }
    void createModel() {
        auto file = options.model;
        if (file.empty()) {
            file = options.cache / "generated-statue.obj";
            std::ofstream out(file);
            out << "v -.5 0 -.5\nv .5 0 -.5\nv .5 0 .5\nv -.5 0 .5\nv 0 1.5 0\n"
                   "f 1 5 2\nf 2 5 3\nf 3 5 4\nf 4 5 1\nf 4 3 2 1\n";
            out.close();
            require(bool(out), "Cannot write generated OBJ");
        }
        auto model = assets.loadModel(file);
        auto instance = model.instantiate(scene, scene.root(), "Imported statue");
        scene.setLocalTransform(instance.root, Mat4::translation({-2, 0, -2}));
        if (options.model.empty()) {
            auto stone =
                assets.createMaterial({.baseColor = {.22f, .3f, .34f, 1}, .roughness = .8f});
            for (auto node : instance.nodes)
                if (auto mesh = scene.component<MeshRenderer>(node)) {
                    std::fill(mesh->materials.begin(), mesh->materials.end(), stone);
                    scene.setComponent(node, *mesh);
                }
        }
        auto sphere = scene.createNode(scene.root(), "PBR metal sphere");
        scene.setLocalTransform(sphere, Mat4::translation({-3, .55f, 0}) *
                                            Mat4::scaling({.65f, .65f, .65f}));
        auto material = assets.createMaterial(
            {.baseColor = {.85f, .55f, .12f, 1}, .metallic = .85f, .roughness = .2f});
        scene.setComponent(sphere, MeshRenderer{assets.createMesh(geometry::sphere()), {material}});
    }
    void createParticles() {
        TextureDesc texture{16, 16, std::vector<uint8_t>(1024), false};
        texture.sampling.mipmapped = false;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const auto i = size_t(y * 16 + x) * 4;
                texture.pixels[i] = texture.pixels[i + 1] = texture.pixels[i + 2] = 255;
                texture.pixels[i + 3] =
                    uint8_t(std::clamp(1 - std::hypot((float(x) - 7.5f) / 8, (float(y) - 7.5f) / 8),
                                       0.f, 1.f) *
                            255);
            }
        auto handle = assets.createSprite({.texture = assets.createTexture(texture),
                                           .rect = {0, 0, 16, 16},
                                           .pivot = {.5f, .5f},
                                           .pixelsPerUnit = 16});
        auto make = [&](ParticlePreset preset, Vec3 p, unsigned capacity) {
            auto c = particlePreset(preset, handle);
            c.origin = p;
            c.capacity = capacity;
            return std::make_unique<ParticleSystem>(c);
        };
        fire = make(ParticlePreset::Fire, campfirePosition + Vec3{0, .1f, 0}, 128);
        smoke = make(ParticlePreset::Smoke, campfirePosition + Vec3{0, .5f, 0}, 96);
        rain = make(ParticlePreset::Rain, {0, 7, 0}, 512);
        snow = make(ParticlePreset::Snow, {0, 6, 0}, 512);
        auto c = particlePreset(ParticlePreset::Fire, handle);
        c.rate = 0;
        c.capacity = 64;
        c.minLifetime = .15f;
        c.maxLifetime = .35f;
        c.velocityMin = {-1, 1, -1};
        c.velocityMax = {1, 2, 1};
        sparks = std::make_unique<ParticleSystem>(c);
    }
    void createMap() {
        TextureDesc pixels{64, 32, std::vector<uint8_t>(64 * 32 * 4), true};
        pixels.sampling.mipmapped = false;
        pixels.sampling.minFilter = pixels.sampling.magFilter = TextureFilter::Nearest;
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 64; ++x) {
                const auto i = (size_t(y) * 64 + x) * 4;
                const auto jitter = (x * 17 + y * 7) % 11;
                pixels.pixels[i] = uint8_t((x < 32 ? 90 : 132) + jitter);
                pixels.pixels[i + 1] = uint8_t(111 + jitter);
                pixels.pixels[i + 2] = uint8_t((x < 32 ? 61 : 78) + jitter);
                pixels.pixels[i + 3] = 255;
            }
        auto texture = assets.createTexture(pixels);
        auto tile = [&](unsigned x) {
            return assets.createSprite({.texture = texture,
                                        .rect = {x, 0, 32, 32},
                                        .pivot = {.5f, .5f},
                                        .pixelsPerUnit = 32,
                                        .alphaMode = AlphaMode::Opaque,
                                        .unlit = false});
        };
        const auto grass = tile(0), path = tile(32);
        const auto directory = options.cache / "chunks";
        TileStreamConfig config;
        config.map = {.tileSize = 1, .chunkSize = 8, .origin = {-4, 0, -4}};
        config.loadRadius = 2;
        config.unloadRadius = 3;
        config.maxResidentChunks = 128;
        config.maxCommitsPerUpdate = 3;
        config.maxUnloadsPerUpdate = 4;
        streamer = std::make_unique<TileStreamer>(
            config,
            TileStreamSource{
                .load =
                    [directory](TileChunkCoord c, std::stop_token stop) {
                        const auto file = chunkFile(directory, c);
                        if (std::filesystem::exists(file)) {
                            auto data = readTileChunk(file, stop);
                            if (data.chunkSize != 8 || data.payload.size() != 2 ||
                                data.payload[0] != 1)
                                throw std::runtime_error("Incompatible game chunk payload");
                            return data;
                        }
                        TileChunkData data;
                        data.chunkSize = 8;
                        data.payload = {1, 0};
                        for (unsigned z = 0; z < 8; ++z)
                            for (unsigned x = 0; x < 8; ++x) {
                                const auto wx = int64_t(c.x) * 8 + x - 4,
                                           wz = int64_t(c.z) * 8 + z - 4;
                                data.tiles.push_back(
                                    {x, z, std::abs(wx) < 1 || std::abs(wz) < 1 ? 2u : 1u});
                            }
                        return data;
                    },
                .save =
                    [directory](TileChunkCoord c, const TileChunkData &data, std::stop_token stop) {
                        writeTileChunk(chunkFile(directory, c), data, stop);
                    },
                .resolve =
                    [grass, path](uint32_t type, AssetStore &) {
                        if (type != 1 && type != 2)
                            throw std::runtime_error("Unknown tile type");
                        return type == 1 ? grass : path;
                    }});
    }
    void streamEvents() {
        for (const auto &event : streamer->takeEvents()) {
            if (event.kind == TileStreamEventKind::LoadFailed ||
                event.kind == TileStreamEventKind::SaveFailed)
                throw std::runtime_error("Game streaming failed: " + event.message);
            if (event.kind == TileStreamEventKind::Saved) {
                ++stats.saves;
                continue;
            }
            const ChunkKey key{event.chunk.x, event.chunk.z};
            if (event.kind == TileStreamEventKind::Unloaded) {
                if (auto it = chunks.find(key); it != chunks.end()) {
                    for (const auto &object : it->second.objects) {
                        if (object.node == selected)
                            selected = {};
                        scene.destroySubtree(object.node);
                        physics.destroyBody(object.body);
                        collision.remove(object.collider);
                    }
                    audio->removeAmbient(id(event.chunk));
                    chunks.erase(it);
                }
            } else if (event.kind == TileStreamEventKind::Loaded) {
                auto &chunk = chunks[key];
                const auto center = Vec3{float(event.chunk.x) * 8, 0, float(event.chunk.z) * 8};
                for (unsigned i = 0; i < 3; ++i) {
                    if (event.data->payload[1] & (1u << i))
                        continue;
                    const auto offset = i == 0   ? Vec3{2.4f, 0, -1.8f}
                                        : i == 1 ? Vec3{-2.4f, 0, 1.4f}
                                                 : Vec3{1.8f, 0, 2.8f};
                    const auto p = center + offset;
                    auto handle = image(
                        i < 2
                            ? 16 + int((uint32_t(event.chunk.x) ^ uint32_t(event.chunk.z) ^ i) % 3)
                            : 19);
                    if (i == 2)
                        handle = maskRock;
                    auto node = sprite(handle, p, 2);
                    shadow(node, i < 2 ? 1.1f : .7f);
                    auto r = *scene.component<SpriteRenderer>(node);
                    r.castShadow = i == 2;
                    r.receiveShadow = true;
                    scene.setComponent(node, r);
                    Body2DDesc b;
                    b.type = Body2DType::Static;
                    b.position = {p.x, p.z};
                    b.userData = nextObject++;
                    b.shapes[0].radius = i < 2 ? .4f : .5f;
                    auto body = physics.createBody(b);
                    auto collider = collision.add({.center = {p.x, p.z},
                                                   .radius = b.shapes[0].radius,
                                                   .userData = b.userData});
                    chunk.objects.push_back({node, body, collider, event.chunk, i});
                }
                std::vector<BatchSprite> flowers;
                for (unsigned i = 0; i < 6; ++i)
                    flowers.push_back(
                        {SpriteRenderer{.sprite = image(24), .sizeMultiplier = {.6f, .6f}},
                         Mat4::translation(center +
                                           Vec3{float(i % 3) * 2 - 2.5f, 0, float(i / 3) * 3 - 2}),
                         uint64_t(i + 1)});
                chunk.decorations.replace(assets, flowers);
                audio->addAmbient(id(event.chunk), center + Vec3{0, 1, 0});
            }
        }
    }
    void createText() {
        const auto font = worldText.loadFont(RENDERER_TEXT_FONT);
        WorldTextDesc d;
        d.style = {.font = font, .fontSize = 20};
        d.node = playerRoot;
        d.text = "Ranger";
        d.transform = Mat4::translation({0, 2.2f, 0});
        d.sizeMode = TextSizeMode::ScreenPixels;
        d.color = {1, .94f, .7f, 1};
        playerName = worldText.create(d);
        d.node = {};
        d.text = "Campfire";
        d.style.fontSize = 32;
        d.sizeMode = TextSizeMode::WorldUnits;
        d.pixelsPerUnit = 90;
        d.billboard = BillboardMode::Upright;
        d.transform = Mat4::translation(campfirePosition + Vec3{0, 1.6f, 0});
        d.color = {1, .7f, .3f, 1};
        fireName = worldText.create(d);
        d.node = playerRoot;
        d.text = "-12";
        d.style.fontSize = 26;
        d.sizeMode = TextSizeMode::ScreenPixels;
        d.billboard = BillboardMode::ViewPlane;
        d.depthTest = false;
        d.visible = false;
        d.color = {1, .35f, .2f, 1};
        d.transform = Mat4::translation({0, 1.5f, 0});
        damageText = worldText.create(d);
    }
    void updateText(double dt) {
        damageAge += float(dt);
        worldText.setVisible(damageText, damageAge < .8f);
        if (damageAge < .8f) {
            worldText.setTransform(damageText, Mat4::translation({0, 1.5f + damageAge, 0}));
            worldText.setColor(damageText, {1, .35f, .2f, 1 - damageAge / .8f});
        }
    }
    void createUi() {
        ui.setViewport({targetWidth, targetHeight});
        if (window) {
            const auto [w, h] = window->framebufferSize();
            const auto [cw, ch] = window->clientSize();
            ui.setViewport(
                {unsigned(w), unsigned(h), double(cw), double(ch), float(w) / float(cw)});
        }
        if (window)
            ui.setPlatformServices({[this](const auto &value) { window->setClipboardText(value); },
                                    [this] { return window->clipboardText(); }});
        ui.loadFont(RENDERER_UI_FONT);
        uiDocument = ui.loadDocument(RENDERER_GAME_UI_DOCUMENT);
        const auto on = [this](const char *id, const char *type, auto callback) {
            uiSubscriptions.push_back(
                ui.subscribe(uiDocument, id, type, [this, callback](const UiEvent &e) {
                    ++stats.uiActions;
                    callback(e);
                }));
        };
        on("pause", "click", [this](const auto &) {
            paused = !paused;
            destination.reset();
        });
        on("hit", "click", [this](const auto &) { strike(); });
        on("weather", "click", [this](const auto &) {
            environment.setWeather(Weather((int(environment.state().weather) + 1) % 3));
        });
        on("music", "change", [this](const UiEvent &e) { audio->setMusicEnabled(e.checked); });
        on("fxaa", "change", [this](const UiEvent &e) {
            view.antiAliasing = e.checked ? AntiAliasing::FXAA : AntiAliasing::None;
        });
        on("oit", "change", [this](const UiEvent &e) {
            view.transparency =
                e.checked ? TransparencyMode::WeightedBlendedOIT : TransparencyMode::SortedAlpha;
        });
        on("volume", "change",
           [this](const UiEvent &e) { audio->setVolume(std::stof(e.value) / 100.f); });
        ui.update(0);
    }
    void updateUi(double dt, unsigned w, unsigned h) {
        UiViewport viewport{w, h};
        if (window) {
            const auto [cw, ch] = window->clientSize();
            viewport.inputWidth = cw;
            viewport.inputHeight = ch;
            viewport.density = float(w) / float(cw);
        }
        ui.setViewport(viewport);
        ui.setText(uiDocument, "status",
                   paused
                       ? "Paused"
                       : "Day " + std::to_string(int(environment.state().hour)) + " / exploring");
        ui.setText(uiDocument, "resources", "Collected: " + std::to_string(stats.collected));
        ui.setText(uiDocument, "pause", paused ? "Resume" : "Pause");
        ui.setChecked(uiDocument, "fxaa", view.antiAliasing == AntiAliasing::FXAA);
        ui.setChecked(uiDocument, "oit", view.transparency == TransparencyMode::WeightedBlendedOIT);
        ui.setChecked(uiDocument, "music", audio->musicEnabled());
        ui.update(dt);
        if (window)
            window->setTextInputArea(ui.textInputArea());
        view.overlay = ui.snapshot();
        // 取走轮询副本，订阅已在 update 中分派，避免长期运行积累事件。
        ui.takeEvents();
    }
    void subscribe() {
        if (!window)
            return;
        subscriptions.push_back(window->subscribe([this](const WindowEvent &event) {
            uiCaptured = ui.processEvent(event);
            if (ui.capture().keyboard)
                forward = backward = left = right = orbitLeft = orbitRight = false;
        }));
        subscriptions.push_back(window->subscribe<KeyEvent>([this](const KeyEvent &event) {
            const bool down = event.action != InputAction::Release;
            if (uiCaptured && down) {
                forward = backward = left = right = orbitLeft = orbitRight = false;
                return;
            }
            switch (event.key) {
            case Key::W:
                forward = down;
                break;
            case Key::S:
                backward = down;
                break;
            case Key::A:
                left = down;
                break;
            case Key::D:
                right = down;
                break;
            case Key::Q:
                orbitLeft = down;
                break;
            case Key::Z:
                orbitRight = down;
                break;
            default:
                break;
            }
            if (event.action != InputAction::Press)
                return;
            if (event.key == Key::Enter && hasModifier(event.modifiers, Modifiers::Alt)) {
                window->setMode(window->mode() == WindowMode::ExclusiveFullscreen
                                    ? WindowMode::Windowed
                                    : WindowMode::ExclusiveFullscreen);
                return;
            }
            switch (event.key) {
            case Key::Escape:
                close = true;
                break;
            case Key::Space:
                strike();
                break;
            case Key::E:
                interact();
                break;
            case Key::N:
                environment.setHour(environment.state().hour < 6 ? 13 : 0);
                break;
            case Key::R:
                environment.setWeather(Weather((int(environment.state().weather) + 1) % 3));
                break;
            case Key::O:
                view.transparency = view.transparency == TransparencyMode::SortedAlpha
                                        ? TransparencyMode::WeightedBlendedOIT
                                        : TransparencyMode::SortedAlpha;
                break;
            case Key::F:
                view.antiAliasing = view.antiAliasing == AntiAliasing::None ? AntiAliasing::FXAA
                                                                            : AntiAliasing::None;
                break;
            case Key::H:
                view.shadows = !view.shadows;
                break;
            case Key::C:
                view.contactShadows = !view.contactShadows;
                break;
            case Key::G:
                grade = !grade;
                break;
            case Key::M:
                audio->toggleMusic();
                break;
            case Key::B:
                audio->switchMusic();
                break;
            case Key::P:
                paused = !paused;
                destination.reset();
                break;
            case Key::T:
                editTile();
                break;
            case Key::K:
                streamer->requestSave();
                break;
            case Key::F11:
                window->setMode(window->mode() == WindowMode::Windowed
                                    ? WindowMode::BorderlessFullscreen
                                    : WindowMode::Windowed);
                break;
            case Key::F12:
                screenshot = true;
                break;
            default:
                break;
            }
        }));
        subscriptions.push_back(window->subscribe<MouseButtonEvent>([this](const auto &event) {
            if (uiCaptured)
                return;
            if (event.action == InputAction::Press)
                clicks.push_back(event);
        }));
        subscriptions.push_back(window->subscribe<MouseScrollEvent>([this](const auto &event) {
            if (!uiCaptured)
                zoomDelta -= float(event.yOffset);
        }));
        subscriptions.push_back(window->subscribe<WindowFocusEvent>([this](const auto &event) {
            if (!event.focused)
                forward = backward = left = right = orbitLeft = orbitRight = false;
        }));
    }
    void select(NodeHandle node) {
        if (selected && scene.valid(selected)) {
            auto r = *scene.component<SpriteRenderer>(selected);
            r.outlineWidth = 0;
            scene.setComponent(selected, r);
        }
        selected = node;
        if (node && scene.valid(node)) {
            auto r = *scene.component<SpriteRenderer>(node);
            r.outlineWidth = 1.5f;
            r.outlineColor = {1, .8f, .1f, 1};
            scene.setComponent(node, r);
            audio->interact(scene.worldTransform(node).transformPoint({}));
            ++stats.picks;
        }
    }
    void processClicks() {
        if (!window || !displayedWidth || clicks.empty()) {
            clicks.clear();
            return;
        }
        const auto [cw, ch] = window->clientSize();
        if (cw <= 0 || ch <= 0) {
            clicks.clear();
            return;
        }
        SceneQuery query(displayed, displayedView, displayedWidth, displayedHeight,
                         {.layerMask = 2});
        for (const auto &click : clicks) {
            Vec2 pixel{float(click.x) * float(displayedWidth) / float(cw),
                       float(click.y) * float(displayedHeight) / float(ch)};
            const auto hit = query.pickSprite(pixel);
            if (click.button == MouseButton::Left && hit && hit->node && scene.valid(hit->node))
                select(hit->node);
            else if (click.button == MouseButton::Right || click.button == MouseButton::Left) {
                destination = query.groundPoint(pixel);
                ++stats.groundQueries;
                if (destination)
                    audio->interact(*destination);
            }
        }
        clicks.clear();
    }
    void strike() {
        damageAge = 0;
        presentation.flash(player);
        audio->impact(world(position));
        sparks->setOrigin(world(position) + Vec3{0, .6f, 0});
        sparks->burst(24);
    }
    void interact() {
        if (!selected || !scene.valid(selected))
            return;
        const auto point = scene.worldTransform(selected).transformPoint({});
        if (distance(position, {point.x, point.z}) > 3)
            return;
        for (auto &[key, chunk] : chunks) {
            (void)key;
            auto it = std::find_if(chunk.objects.begin(), chunk.objects.end(),
                                   [&](const Object &object) { return object.node == selected; });
            if (it == chunk.objects.end())
                continue;
            if (std::find(nearby.begin(), nearby.end(), it->collider) == nearby.end())
                return;
            const auto data = streamer->chunk(it->chunk);
            auto payload = data->payload;
            payload[1] |= uint8_t(1u << it->bit);
            streamer->setPayload(it->chunk, payload);
            streamer->requestSave();
            scene.destroySubtree(it->node);
            physics.destroyBody(it->body);
            collision.remove(it->collider);
            chunk.objects.erase(it);
            ++stats.collected;
            selected = {};
            audio->interact(point);
            sparks->setOrigin(point);
            sparks->burst(20);
            return;
        }
    }
    void editTile() {
        const int32_t x = int32_t(std::floor(position.x + 4)),
                      z = int32_t(std::floor(position.y + 4));
        const TileChunkCoord c{int32_t(std::floor(double(x) / 8)),
                               int32_t(std::floor(double(z) / 8))};
        const auto data = streamer->chunk(c);
        if (!data)
            return;
        const auto localX = uint32_t(int64_t(x) - int64_t(c.x) * 8),
                   localZ = uint32_t(int64_t(z) - int64_t(c.z) * 8);
        const auto current =
            std::find_if(data->tiles.begin(), data->tiles.end(),
                         [&](const StreamTile &t) { return t.x == localX && t.z == localZ; });
        const auto type = current != data->tiles.end() && current->type == 1 ? 2u : 1u;
        streamer->setTile(x, z, type, assets);
        streamer->requestSave();
        edited = std::tuple{c, x, z, type};
        ++stats.edits;
        audio->interact(world(position));
    }
    void script(unsigned frame) {
        if (!options.selfTest)
            return;
        if (frame == 45 || frame == 50 || frame == 52) {
            const auto b = ui.element(uiDocument, frame == 45 ? "hit" : "pause").bounds;
            // 自检不依赖真实键鼠，输入坐标映射与窗口事件使用同一入口。
            const auto [cw, ch] =
                window ? window->clientSize() : std::pair{int(targetWidth), int(targetHeight)};
            const auto [pw, ph] =
                window ? window->framebufferSize() : std::pair{int(targetWidth), int(targetHeight)};
            const double x = (b.x + b.width * .5) * cw / pw, y = (b.y + b.height * .5) * ch / ph;
            ui.processEvent(
                MouseButtonEvent{MouseButton::Left, InputAction::Press, Modifiers::None, x, y});
            ui.processEvent(
                MouseButtonEvent{MouseButton::Left, InputAction::Release, Modifiers::None, x, y});
        }
        right = frame < 30 || (frame >= 140 && frame < 175);
        forward = backward = left = false;
        if (frame == 40)
            view.transparency = TransparencyMode::WeightedBlendedOIT;
        if (frame == 55)
            view.antiAliasing = AntiAliasing::None;
        if (frame == 60) {
            environment.setHour(0);
            environment.setWeather(Weather::Rain);
        }
        if (frame == 75)
            view.antiAliasing = AntiAliasing::FXAA;
        if (frame == 80)
            editTile();
        if (frame == 100) {
            physics.setTransform(playerBody, {40, 0});
            position = {40, 0};
            cameraControl.setTarget(world(position), true);
            destination.reset();
        }
        if (frame == 130) {
            physics.setTransform(playerBody, {});
            position = {};
            cameraControl.setTarget(world(position), true);
            environment.setWeather(Weather::Snow);
        }
        if (frame == 160) {
            environment.setHour(13);
            environment.setWeather(Weather::Clear);
        }
        if (frame == 180)
            strike();
        if (frame == 185)
            paused = true;
        if (frame == 190) {
            grade = true;
            paused = false;
        }
        if (frame == 210) {
            grade = false;
            view.transparency = TransparencyMode::SortedAlpha;
        }
        if (frame == 220 && options.offscreen) {
            targetWidth = options.width + 16;
            targetHeight = options.height + 8;
            renderer->resizeOffscreen(targetWidth, targetHeight);
        }
        if (frame == 225 && options.offscreen) {
            targetWidth = options.width;
            targetHeight = options.height;
            renderer->resizeOffscreen(options.width, options.height);
        }
    }
    void simulate(double dt) {
        const auto before = physics.state(playerBody).position;
        Vec2 direction{float(right) - float(left), float(backward) - float(forward)};
        if (direction.x || direction.y)
            destination.reset();
        else if (destination) {
            direction = {destination->x - before.x, destination->z - before.y};
            if (std::hypot(direction.x, direction.y) < .15f) {
                direction = {};
                destination.reset();
            }
        }
        const auto magnitude = std::hypot(direction.x, direction.y);
        Vec2 velocity{};
        if (magnitude > 0) {
            velocity = {direction.x / magnitude * 2.6f, direction.y / magnitude * 2.6f};
            facing = world(velocity);
        }
        physics.setVelocity(playerBody, velocity);
        physics.advance(dt);
        physics.synchronize(scene, false);
        const auto a = physics.state(anchorBody).position, b = physics.state(logBody).position;
        scene.setLocalTransform(rope,
                                Mat4::translation({(a.x + b.x) * .5f, .04f, (a.y + b.y) * .5f}) *
                                    Mat4::rotationY(std::atan2(b.x - a.x, b.y - a.y)) *
                                    Mat4::scaling({.04f, .04f, distance(a, b)}));
        position = physics.state(playerBody).position;
        impactCooldown = std::max(0.f, impactCooldown - float(dt));
        for (const auto &event : physics.takeEvents()) {
            if (event.kind == Physics2DEventKind::SensorBegin) {
                ++stats.sensors;
                audio->interact(world(position));
            }
            if (event.kind == Physics2DEventKind::ContactBegin) {
                ++stats.contacts;
                if (impactCooldown == 0) {
                    audio->impact(world(position));
                    presentation.flash(player, {1, .4f, .2f, .5f}, .12f);
                    impactCooldown = .25f;
                }
            }
        }
        // collision 作为轻量交互宽阶段使用；角色运动完全由 Box2D 负责。
        nearby = collision.overlapCircle(position, 2.5f);
        ++stats.collisionQueries;
        const bool moving = distance(before, position) > .001f;
        if (moving != walking) {
            walking = moving;
            animator.play(walking ? *walk : *idle);
        }
        animator.advance(dt);
        for (const auto &event : animator.takeEvents())
            if (event.name == "footstep") {
                audio->footstep(world(position));
                ++stats.animationEvents;
            }
    }
    void cameraAndEnvironment(double dt) {
        const Vec3 focus = world(position);
        cameraControl.setTarget(focus);
        cameraControl.update(dt,
                             {.rotation = {float(dt) * (float(orbitRight) - float(orbitLeft)), 0},
                              .zoom = zoomDelta});
        zoomDelta = 0;
        cameraControl.apply(scene, camera);
        const auto cameraWorld = cameraControl.state().world;
        animator.selectDirection(facing, cameraWorld);
        animator.apply(scene, player);
        environment.advance(dt);
        environment.apply(scene, sun, view);
        const auto state = environment.state();
        if (grade) {
            view.colorMultiply = {1, .75f, .55f};
            view.saturation = .5f;
        } else
            view.saturation = 1;
        const auto rainAmount = state.weather == Weather::Rain ? state.precipitation : 0.f;
        const auto snowAmount = state.weather == Weather::Snow ? state.precipitation : 0.f;
        rain->setOrigin(focus + Vec3{0, 7, 0});
        snow->setOrigin(focus + Vec3{0, 6, 0});
        rain->setRate(rainAmount * 200);
        snow->setRate(snowAmount * 120);
        fire->advance(dt);
        smoke->advance(dt);
        rain->advance(dt);
        snow->advance(dt);
        sparks->advance(dt);
        const bool dark = state.hour < 6 || state.hour >= 18;
        stats.night |= dark;
        stats.day |= !dark;
        stats.rain |= rainAmount > .1f;
        stats.snow |= snowAmount > .1f;
        scene.setVisible(marker, bool(destination));
        if (destination)
            scene.setLocalTransform(marker, Mat4::translation(*destination + Vec3{0, .025f, 0}) *
                                                Mat4::rotationX(radians(-90)));
    }
    void automaticQueries(const RenderSnapshot &snapshot, unsigned w, unsigned h) {
        SceneQuery query(snapshot, view, w, h, {.layerMask = 2});
        if (query.groundPoint({float(w) * .5f, float(h) * .5f}))
            ++stats.groundQueries;
        for (const auto &[key, chunk] : chunks) {
            (void)key;
            for (const auto &object : chunk.objects) {
                const auto p = scene.worldTransform(object.node).transformPoint({0, 1.5f, 0});
                if (const auto pixel = query.worldToScreen(p))
                    if (const auto hit = query.pickSprite({pixel->x, pixel->y});
                        hit && hit->node && scene.valid(hit->node)) {
                        select(hit->node);
                        return;
                    }
            }
        }
        throw std::runtime_error("Automatic sprite selection missed all visible objects");
    }
    void flushMap() {
        streamer->requestSave();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            streamStats = streamer->update(world(position), assets);
            streamEvents();
            if (!streamStats.dirtyChunks && !streamStats.pendingSaves)
                return;
            require(std::chrono::steady_clock::now() < deadline, "Timed out saving game map");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
    }
    void verify(const Image &image) {
        const auto sound = audio->statistics();
        require(stats.frames == options.frames && renderStats.visibleInstances > 100,
                "Game did not render the populated world");
        require(stats.sorted && stats.oit && stats.fxaa && stats.noFxaa,
                "Game did not exercise rendering switches");
        require(stats.day && stats.night && stats.rain && stats.snow,
                "Game did not exercise environment transitions");
        require(stats.contacts > 0 && stats.sensors > 0 &&
                    physics.state(crateBody).position.x > .87f,
                "Game did not exercise Box2D pushing and sensor events");
        require(physics.statistics().joints > 0 && stats.collisionQueries > 0,
                "Game did not exercise physics joint/query");
        require(stats.picks > 0 && stats.groundQueries > 0 && stats.animationEvents > 0,
                "Game did not exercise picking/animation");
        require(streamStats.loaded > 25 && streamStats.unloaded > 0 && stats.saves > 0 &&
                    stats.edits > 0 && edited,
                "Game did not exercise streamed persistence");
        const auto [c, x, z, type] = *edited;
        const auto data = readTileChunk(chunkFile(options.cache / "chunks", c));
        const auto tile = std::find_if(data.tiles.begin(), data.tiles.end(), [&](const auto &t) {
            return t.x == uint32_t(int64_t(x) - int64_t(c.x) * 8) &&
                   t.z == uint32_t(int64_t(z) - int64_t(c.z) * 8);
        });
        require(tile != data.tiles.end() && tile->type == type,
                "Saved tile does not match edited state");
        require(sound.footsteps > 0 && sound.impacts > 0 && sound.interactions > 0 &&
                    sound.musicChanges >= 3 && sound.ambientStarted > 25 &&
                    sound.ambientStopped > 0 && sound.ended > 0,
                "Game audio events were not connected");
        if (sound.offline)
            require(sound.energy > 1 && sound.mixedFrames >= uint64_t(options.frames) * 800 - 1,
                    "Game mixer did not produce the expected PCM output");
        require(stats.pausedFrames > 0, "Game did not exercise independent audio/game clocks");
        require(stats.uiActions >= 3 && renderStats.overlayDrawCalls > 0,
                "Game UI controls were not connected to gameplay/rendering");
        require(worldText.statistics().publishedLabels >= 2 &&
                    worldText.statistics().publishedGlyphs > 0,
                "Game world text was not published");
        require(image.width > 0 && image.height > 0 &&
                    image.pixels.size() == size_t(image.width) * image.height * 4,
                "Game output image has invalid dimensions");
        size_t varied = 0;
        for (size_t i = 0; i < image.pixels.size(); i += 4)
            varied += image.pixels[i] != image.pixels[0] || image.pixels[i + 1] != image.pixels[1];
        require(varied > size_t(image.width) * image.height / 20, "Game output image is empty");
    }
    void report() {
        const auto sound = audio->statistics();
        std::ofstream out(options.cache / "session.json");
        out << "{\n  \"backend\": \"" << renderer->deviceInfo().backend << "\",\n"
            << "  \"frames\": " << stats.frames
            << ",\n  \"offscreen\": " << (options.offscreen ? "true" : "false") << ",\n"
            << "  \"visibleInstances\": " << renderStats.visibleInstances
            << ",\n  \"drawCalls\": " << renderStats.drawCalls << ",\n"
            << "  \"uiActions\": " << stats.uiActions
            << ",\n  \"textGlyphs\": " << worldText.statistics().publishedGlyphs
            << ",\n  \"uiDrawCalls\": " << renderStats.overlayDrawCalls << ",\n"
            << "  \"contacts\": " << stats.contacts << ",\n  \"sensorEvents\": " << stats.sensors
            << ",\n"
            << "  \"spritePicks\": " << stats.picks
            << ",\n  \"animationEvents\": " << stats.animationEvents << ",\n"
            << "  \"streamLoaded\": " << streamStats.loaded
            << ",\n  \"streamUnloaded\": " << streamStats.unloaded << ",\n"
            << "  \"savedChunks\": " << stats.saves
            << ",\n  \"musicChanges\": " << sound.musicChanges << ",\n"
            << "  \"footstepSounds\": " << sound.footsteps
            << ",\n  \"ambientStopped\": " << sound.ambientStopped << ",\n"
            << "  \"mixedFrames\": " << sound.mixedFrames
            << ",\n  \"mixedEnergy\": " << sound.energy << "\n}\n";
        out.close();
        require(bool(out), "Cannot write game session report");
        std::cout << "Game frames=" << stats.frames << " visible=" << renderStats.visibleInstances
                  << " loaded=" << streamStats.loaded << " unloaded=" << streamStats.unloaded
                  << " contacts=" << stats.contacts << " picks=" << stats.picks
                  << " footsteps=" << sound.footsteps << " music=" << sound.musicChanges
                  << " audioFrames=" << sound.mixedFrames << " audioEnergy=" << sound.energy
                  << " errors=" << renderer->debugErrorCount() << '\n';
    }
    void run() {
        auto previous = std::chrono::steady_clock::now();
        for (unsigned frame = 0; !close && (!options.frames || frame < options.frames); ++frame) {
            if (window) {
                window->pollEvents();
                if (window->shouldClose())
                    break;
            }
            const auto now = std::chrono::steady_clock::now();
            const double dt =
                options.frames
                    ? 1.0 / 60
                    : std::clamp(std::chrono::duration<double>(now - previous).count(), 0., .05);
            previous = now;
            script(frame);
            ui.update(0);
            processClicks();
            const double simulationDt = paused ? 0 : dt;
            if (paused)
                ++stats.pausedFrames;
            if (!paused)
                simulate(simulationDt);
            cameraAndEnvironment(simulationDt);
            streamStats = streamer->update(world(position), assets);
            streamEvents();
            const auto state = environment.state();
            audio->update(dt, world(position), state.hour < 6 || state.hour >= 18,
                          state.weather == Weather::Rain ? state.precipitation : 0,
                          state.weather == Weather::Snow ? state.precipitation : 0);
            auto batches = streamer->batches(assets);
            for (const auto &[key, chunk] : chunks) {
                (void)key;
                batches.push_back(chunk.decorations);
            }
            for (const auto *emitter :
                 {fire.get(), smoke.get(), rain.get(), snow.get(), sparks.get()})
                batches.push_back(emitter->batch(assets));
            auto [w, h] =
                window ? window->framebufferSize() : std::pair{int(targetWidth), int(targetHeight)};
            if (w <= 0 || h <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            updateText(simulationDt);
            batches.push_back(worldText.batch(scene, camera, unsigned(w), unsigned(h)));
            auto snapshot = extractor.extract(scene, assets, batches);
            if (options.selfTest && (frame == 20 || frame == 85))
                automaticQueries(snapshot, unsigned(w), unsigned(h));
            presentation.update(scene, snapshot, view, unsigned(w), unsigned(h),
                                world(position) + Vec3{0, .8f, 0}, simulationDt);
            snapshot = extractor.extract(scene, assets, batches);
            updateUi(dt, unsigned(w), unsigned(h));
            renderStats = renderer->render(snapshot, view);
            if (options.selfTest && options.offscreen && (frame == 221 || frame == 226)) {
                const auto resized = renderer->readback();
                require(resized.width == targetWidth && resized.height == targetHeight,
                        "Game offscreen resize failed");
            }
            if (window)
                renderer->present();
            displayed = std::move(snapshot);
            displayedView = view;
            displayedWidth = unsigned(w);
            displayedHeight = unsigned(h);
            stats.sorted |= view.transparency == TransparencyMode::SortedAlpha;
            stats.oit |= renderStats.transparencyResolveDrawCalls > 0;
            stats.fxaa |= renderStats.antiAliasingDrawCalls > 0;
            stats.noFxaa |= view.antiAliasing == AntiAliasing::None;
            ++stats.frames;
            if (screenshot) {
                renderer->readback().savePng(options.cache / "captures" /
                                             ("frame-" + std::to_string(frame) + ".png"));
                screenshot = false;
            }
            if (window && frame % 30 == 0)
                window->setTitle("Forest Camp / " + renderer->deviceInfo().backend + " / " +
                                 std::to_string(int(state.hour)) + "h / sprites " +
                                 std::to_string(renderStats.visibleInstances) + " / collected " +
                                 std::to_string(stats.collected) +
                                 " / WASD move, E interact, N/R/O/F/M controls");
            if (options.frames)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        flushMap();
        renderer->waitIdle();
        require(stats.frames > 0, "Game exited without rendering a frame");
        auto image = renderer->readback();
        const auto capture =
            options.capture.empty() ? options.cache / "last-frame.png" : options.capture;
        image.savePng(capture);
        audio->finish();
        require(renderer->debugErrorCount() == 0, "Game GPU validation errors");
        if (options.selfTest)
            verify(image);
        report();
        presentation.reset(scene);
    }
};
Application::Application(const Options &options) : impl_(std::make_unique<Impl>(options)) {}
Application::~Application() = default;
void Application::run() {
    impl_->run();
}
} // namespace game_demo
