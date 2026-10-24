/** @file @brief 地图分块、连续碰撞、四类图片粒子、昼夜天气及遮挡/闪白/描边的完整模块示例。 */
#include "../common/forest_assets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#ifdef RENDERER_WORLD_PHYSICS2D
#include <renderer/physics2d/physics2d.hpp>
#else
#include <renderer/collision/collision.hpp>
#endif
#include <renderer/environment/environment.hpp>
#include <renderer/particles/particles.hpp>
#include <renderer/presentation/presentation.hpp>
#include <renderer/tilemap/streaming.hpp>
#include <renderer/tilemap/tilemap.hpp>
#include <renderer/window.hpp>
#include <thread>
using namespace renderer;
namespace {
#ifdef RENDERER_WORLD_PHYSICS2D
using DemoBody = Body2DHandle;
class DemoPhysics {
    Physics2D world_;
    Body2DHandle player_;
    uint64_t events_ = 0;

  public:
    DemoPhysics() {
        Body2DDesc body;
        body.lockRotation = true;
        body.shapes[0].radius = .25f;
        body.shapes[0].friction = 0;
        player_ = world_.createBody(body);
        body.type = Body2DType::Static;
        body.shapes[0].radius = 2;
        body.shapes[0].sensor = true;
        world_.createBody(body);
    }
    DemoBody obstacle(Vec2 p, float radius) {
        Body2DDesc body;
        body.type = Body2DType::Static;
        body.position = p;
        body.shapes[0].radius = radius;
        return world_.createBody(body);
    }
    void remove(DemoBody body) {
        world_.destroyBody(body);
    }
    bool probe(Vec2 p, Vec2 delta) {
        return bool(world_.rayCast(p, delta, {.ignore = player_}));
    }
    Vec2 move(Vec2, Vec2 delta, double dt) {
        world_.setVelocity(player_,
                           dt > 0 ? Vec2{delta.x / float(dt), delta.y / float(dt)} : Vec2{});
        world_.advance(dt);
        events_ += world_.takeEvents().size();
        return world_.state(player_).position;
    }
    void bindPlayer(Scene &scene, NodeHandle node) {
        world_.bindNode(player_, scene, node);
    }
    void constrain(Vec2 p) {
        const auto current = world_.state(player_).position;
        if (current.x != p.x || current.y != p.y)
            world_.setTransform(player_, p);
    }
    void dynamic(Scene &scene, NodeHandle node, Vec2 p) {
        Body2DDesc body;
        body.position = p;
        body.linearDamping = .8f;
        body.shapes[0].radius = .35f;
        auto handle = world_.createBody(body);
        world_.bindNode(handle, scene, node);
    }
    void synchronize(Scene &scene, NodeHandle, Vec2) {
        world_.synchronize(scene, false);
    }
    uint64_t eventCount() const {
        return events_;
    }
    uint32_t bodyCount() const {
        return world_.statistics().bodies;
    }
};
#else
using DemoBody = ColliderHandle;
class DemoPhysics {
    CollisionWorld world_;

  public:
    DemoBody obstacle(Vec2 p, float radius) {
        return world_.add({.center = p, .radius = radius});
    }
    void remove(DemoBody body) {
        world_.remove(body);
    }
    bool probe(Vec2 p, Vec2 delta) {
        return !world_.moveCircle(p, .25f, delta).contacts.empty();
    }
    Vec2 move(Vec2 p, Vec2 delta, double) {
        return world_.moveCircle(p, .25f, delta).position;
    }
    void bindPlayer(Scene &, NodeHandle) {}
    void constrain(Vec2) {}
    void synchronize(Scene &scene, NodeHandle node, Vec2 p) {
        scene.setLocalTransform(node, Mat4::translation({p.x, 0, p.y}));
    }
    uint64_t eventCount() const {
        return 0;
    }
    uint32_t bodyCount() const {
        return 0;
    }
};
#endif
struct ChunkCache {
    std::mutex mutex;
    std::map<std::pair<int32_t, int32_t>, TileChunkData> saved;
};
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
Renderer makeRenderer(Window &window, GraphicsApi api, bool threaded) {
#ifdef RENDERER_WORLD_OPENGL
    if (api == GraphicsApi::OpenGL)
        return Renderer::createOpenGL(window, {.renderThread = threaded});
#endif
#ifdef RENDERER_WORLD_VULKAN
    if (api == GraphicsApi::Vulkan)
        return Renderer::createVulkan(window, {.renderThread = threaded});
#endif
    throw std::invalid_argument("Requested backend is unavailable");
}
} // namespace
int main(int argc, char **argv) {
    try {
        bool hidden = false, threaded = true, selfTest = false, streaming = false;
        uint32_t frames = 0;
        std::filesystem::path capture;
        std::filesystem::path streamDirectory;
        std::string backend = "auto";
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&] {
                if (++i >= argc)
                    throw std::invalid_argument("Missing argument value");
                return std::string(argv[i]);
            };
            if (argument == "--hidden")
                hidden = true;
            else if (argument == "--single-thread")
                threaded = false;
            else if (argument == "--threaded")
                threaded = true;
            else if (argument == "--self-test")
                selfTest = true;
            else if (argument == "--streaming")
                streaming = true;
            else if (argument == "--stream-cache") {
                streaming = true;
                streamDirectory = value();
            } else if (argument == "--backend")
                backend = value();
            else if (argument == "--capture")
                capture = value();
            else if (argument == "--frames") {
                auto text = value();
                size_t n = 0;
                auto count = std::stoul(text, &n);
                require(n == text.size() && count > 0 && count <= 1000000, "Invalid frame count");
                frames = uint32_t(count);
            } else
                throw std::invalid_argument("Unknown option: " + argument);
        }
        require(!hidden || frames > 0, "Hidden mode requires --frames");
        require(backend == "auto" || backend == "opengl" || backend == "vulkan", "Invalid backend");
        GraphicsApi api = GraphicsApi::OpenGL;
#ifndef RENDERER_WORLD_OPENGL
        api = GraphicsApi::Vulkan;
#endif
        if (backend == "vulkan")
            api = GraphicsApi::Vulkan;
        if (backend == "opengl")
            api = GraphicsApi::OpenGL;
        Window window({.width = 1100,
                       .height = 760,
                       .title = "Modular world / WASD / Space flash / N day-night / R weather / O "
                                "OIT / F FXAA / C contacts",
                       .visible = !hidden,
                       .vsync = !hidden,
                       .graphicsApi = api});
        auto engine = makeRenderer(window, api, threaded);
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        const auto art = assets.createTexture(forest_assets::makeAtlas());
        const auto entries = forest_assets::makeEntries();
        SpriteAtlas atlas(assets, art, entries);
        auto image = [&](int i) { return atlas.sprite(std::to_string(i)); };
        TextureDesc ground{64, 32, std::vector<uint8_t>(64 * 32 * 4), true};
        ground.sampling.mipmapped = false;
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                auto i = (size_t(y) * 64 + x) * 4;
                const bool dirt = x >= 32;
                const int jitter = int((x * 17 + y * 7) % 9);
                ground.pixels[i] = uint8_t((dirt ? 130 : 89) + jitter);
                ground.pixels[i + 1] = uint8_t((dirt ? 111 : 111) + jitter);
                ground.pixels[i + 2] = uint8_t((dirt ? 72 : 59) + jitter);
                ground.pixels[i + 3] = 255;
            }
        const auto groundTexture = assets.createTexture(ground);
        auto makeTile = [&](uint32_t x) {
            return assets.createSprite({.texture = groundTexture,
                                        .rect = {x, 0, 32, 32},
                                        .pivot = {.5f, .5f},
                                        .pixelsPerUnit = 32,
                                        .alphaMode = AlphaMode::Opaque});
        };
        const auto grass = makeTile(0), path = makeTile(32);
        TileMap map({.tileSize = 1, .chunkSize = 8, .origin = {-16, 0, -16}});
        if (!streaming)
            for (int z = 0; z < 32; ++z)
                for (int x = 0; x < 32; ++x)
                    map.set(x, z, std::abs(x - 16) < 2 || std::abs(z - 16) < 2 ? path : grass);
        std::unique_ptr<TileStreamer> streamer;
        if (streaming) {
            auto cache = std::make_shared<ChunkCache>();
            auto file = [streamDirectory](TileChunkCoord c) {
                return streamDirectory /
                       (std::to_string(c.x) + "_" + std::to_string(c.z) + ".rtile");
            };
            TileStreamConfig config;
            config.map = {.tileSize = 1, .chunkSize = 8, .origin = {-16, 0, -16}};
            streamer = std::make_unique<TileStreamer>(
                config,
                TileStreamSource{
                    .load =
                        [cache, file, streamDirectory](TileChunkCoord c, std::stop_token stop) {
                            if (!streamDirectory.empty() && std::filesystem::exists(file(c)))
                                return readTileChunk(file(c), stop);
                            {
                                std::lock_guard lock(cache->mutex);
                                if (const auto i = cache->saved.find({c.x, c.z});
                                    i != cache->saved.end())
                                    return i->second;
                            }
                            TileChunkData data;
                            data.chunkSize = 8;
                            for (uint32_t z = 0; z < 8; ++z)
                                for (uint32_t x = 0; x < 8; ++x) {
                                    const auto wx = int64_t(c.x) * 8 + x, wz = int64_t(c.z) * 8 + z;
                                    data.tiles.push_back(
                                        {x, z,
                                         std::abs(wx - 16) < 2 || std::abs(wz - 16) < 2 ? 2u : 1u});
                                }
                            return data;
                        },
                    .save =
                        [cache, file, streamDirectory](TileChunkCoord c, const TileChunkData &data,
                                                       std::stop_token stop) {
                            if (!streamDirectory.empty())
                                writeTileChunk(file(c), data, stop);
                            else {
                                std::lock_guard lock(cache->mutex);
                                cache->saved[{c.x, c.z}] = data;
                            }
                        },
                    .resolve =
                        [grass, path](uint32_t type, AssetStore &) {
                            if (type != 1 && type != 2)
                                throw std::runtime_error("Unknown world tile type");
                            return type == 1 ? grass : path;
                        }});
        }
        const auto camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                          .orthographicHeight = 16});
        scene.setLocalTransform(camera, *Mat4::lookAt({10, 10, 14}, {0, 0, 0}).inverse());
        const auto sun = scene.createNode(scene.root());
        scene.setComponent(sun, Light{});
        EnvironmentController environment({.dayLength = 90, .hour = 14});
        environment.setWeather(Weather::Rain);
        DemoPhysics collision;
        std::mt19937 random(42);
        std::uniform_real_distribution<float> jitter(-.3f, .3f);
        std::vector<BatchSprite> decoration;
        auto add = [&](SpriteHandle sprite, Vec3 p, uint32_t layer = 1) {
            auto n = scene.createNode(scene.root());
            SpriteRenderer r;
            r.sprite = sprite;
            r.layerMask = layer;
            scene.setComponent(n, r);
            scene.setLocalTransform(n, Mat4::translation(p));
            return n;
        };
        bool collisionExercised = false;
        for (int z = -4; z <= 4; ++z)
            for (int x = -5; x <= 5; ++x) {
                if (std::abs(x) <= 1 || std::abs(z) <= 1)
                    continue;
                Vec3 p{float(x) * 1.5f + jitter(random), 0, float(z) * 1.5f + jitter(random)};
                const bool tree = random() % 3 != 0;
                auto sprite = image(tree ? 16 + int(random() % 3) : 19 + int(random() % 2));
                if (!tree) {
                    auto d = assets.spriteDescription(sprite);
                    d.alphaMode = AlphaMode::Mask;
                    sprite = assets.createSprite(d);
                }
                add(sprite, p, tree ? 2u : 1u);
                collision.obstacle({p.x, p.z}, tree ? .4f : .5f);
                if (!collisionExercised) {
                    collisionExercised = collision.probe({p.x - 2, p.z}, {4, 0});
                }
                BatchSprite shadow;
                shadow.renderer.sprite = image(21);
                shadow.renderer.billboard = BillboardMode::Fixed;
                shadow.renderer.sortingLayer = -10;
                shadow.world =
                    Mat4::translation(p + Vec3{0, .012f, 0}) * Mat4::rotationX(radians(-90));
                decoration.push_back(shadow);
            }
        for (int i = 0; i < 90; ++i) {
            BatchSprite flower;
            flower.renderer.sprite = image(24);
            flower.renderer.sizeMultiplier = {.5f, .5f};
            flower.world = Mat4::translation(
                {float(int(random() % 140) - 70) / 10, .01f, float(int(random() % 120) - 60) / 10});
            flower.id = uint64_t(i) + 1000;
            decoration.push_back(flower);
        }
        SpriteBatch decorations;
        decorations.replace(assets, decoration);
        const auto playerRoot = scene.createNode(scene.root());
        collision.bindPlayer(scene, playerRoot);
#ifdef RENDERER_WORLD_PHYSICS2D
        for (const auto p : {Vec3{.8f, 0, .2f}, Vec3{-1.5f, 0, 1}})
            collision.dynamic(scene, add(image(19), p), {p.x, p.z});
#endif
        scene.setComponent(playerRoot, SortingGroup{});
        const auto player = scene.createNode(playerRoot);
        const auto hat = scene.createNode(playerRoot);
        SpriteRenderer playerData;
        playerData.sprite = image(0);
        playerData.layerMask = 1;
        playerData.outlineWidth = 1.5f;
        playerData.outlineColor = {1, .8f, .1f, 1};
        scene.setComponent(player, playerData);
        auto hatData = playerData;
        hatData.sprite = image(22);
        hatData.sortingOrder = 1;
        hatData.outlineWidth = 0;
        scene.setComponent(hat, hatData);
        SpritePresentation presentation({.occluderMask = 2, .excludedNodes = {player, hat}});
        TextureDesc glow{16, 16, std::vector<uint8_t>(16 * 16 * 4), false};
        glow.sampling.mipmapped = false;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const float d = std::sqrt(std::pow((float(x) + .5f - 8) / 8, 2.f) +
                                          std::pow((float(y) + .5f - 8) / 8, 2.f));
                auto i = size_t(y * 16 + x) * 4;
                glow.pixels[i] = glow.pixels[i + 1] = glow.pixels[i + 2] = 255;
                glow.pixels[i + 3] = uint8_t(std::clamp(1 - d, 0.f, 1.f) * 255);
            }
        auto particle = assets.createSprite({.texture = assets.createTexture(glow),
                                             .rect = {0, 0, 16, 16},
                                             .pivot = {.5f, .5f},
                                             .pixelsPerUnit = 16});
        auto fireConfig = particlePreset(ParticlePreset::Fire, particle);
        fireConfig.origin = {1, .1f, 0};
        fireConfig.capacity = 256;
        ParticleSystem fire(fireConfig);
        auto smokeConfig = particlePreset(ParticlePreset::Smoke, particle);
        smokeConfig.origin = {1, .5f, 0};
        smokeConfig.capacity = 128;
        ParticleSystem smoke(smokeConfig);
        auto rainConfig = particlePreset(ParticlePreset::Rain, particle);
        rainConfig.origin = {0, 7, 0};
        rainConfig.capacity = 1024;
        ParticleSystem rain(rainConfig);
        auto snowConfig = particlePreset(ParticlePreset::Snow, particle);
        snowConfig.origin = {0, 6, 0};
        snowConfig.capacity = 1024;
        ParticleSystem snow(snowConfig);
        bool forward = false, backward = false, left = false, right = false, editTile = false;
        int weather = 1;
        RenderView view{.cameraNode = camera,
                        .clearColor = {.07f, .09f, .05f, 1},
                        .shadows = false,
                        .antiAliasing = AntiAliasing::FXAA};
        view.contactShadows = true;
        view.contactShadowDistance = 1.5f;
        view.contactShadowThickness = .2f;
        auto keys = window.subscribe<KeyEvent>([&](const KeyEvent &e) {
            const bool on = e.action != InputAction::Release;
            switch (e.key) {
            case Key::W:
                forward = on;
                break;
            case Key::S:
                backward = on;
                break;
            case Key::A:
                left = on;
                break;
            case Key::D:
                right = on;
                break;
            default:
                break;
            }
            if (e.action != InputAction::Press)
                return;
            switch (e.key) {
            case Key::T:
                editTile = true;
                break;
            case Key::Escape:
                window.requestClose();
                break;
            case Key::Space:
                presentation.flash(player);
                break;
            case Key::N:
                environment.setHour(environment.state().hour < 6 ? 12 : 0);
                break;
            case Key::R:
                weather = (weather + 1) % 3;
                environment.setWeather(Weather(weather));
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
            case Key::C:
                view.contactShadows = !view.contactShadows;
                break;
            default:
                break;
            }
        });
        Vec2 position{};
        TileStreamStats streamStats;
        bool streamEdited = false;
        std::map<std::pair<int32_t, int32_t>, std::pair<NodeHandle, DemoBody>> streamedObjects;
        // 生命周期事件在逻辑线程处理，统一挂接场景物件和当前物理实现的碰撞体。
        auto streamEvents = [&] {
            for (const auto &event : streamer->takeEvents()) {
                const auto key = std::pair{event.chunk.x, event.chunk.z};
                if (event.kind == TileStreamEventKind::LoadFailed ||
                    event.kind == TileStreamEventKind::SaveFailed)
                    throw std::runtime_error("World streaming failed: " + event.message);
                if (event.kind == TileStreamEventKind::Loaded) {
                    const Vec3 p{-16 + (float(event.chunk.x) + .65f) * 8, 0,
                                 -16 + (float(event.chunk.z) + .65f) * 8};
                    const auto node = add(image(16), p, 2);
                    const auto collider = collision.obstacle({p.x, p.z}, .4f);
                    streamedObjects.emplace(key, std::pair{node, collider});
                } else if (event.kind == TileStreamEventKind::Unloaded) {
                    if (auto i = streamedObjects.find(key); i != streamedObjects.end()) {
                        scene.destroySubtree(i->second.first);
                        collision.remove(i->second.second);
                        streamedObjects.erase(i);
                    }
                }
            }
        };
        uint32_t frame = 0;
        RenderStats stats;
        auto previous = std::chrono::steady_clock::now();
        while (!window.shouldClose()) {
            window.pollEvents();
            const auto [width, height] = window.framebufferSize();
            if (width <= 0 || height <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            auto now = std::chrono::steady_clock::now();
            const double dt =
                hidden || selfTest
                    ? 1.0 / 60
                    : std::clamp(std::chrono::duration<double>(now - previous).count(), 0.0, .1);
            previous = now;
            if (selfTest) {
                if (frame == 2)
                    presentation.flash(player);
                if (frame == 8)
                    view.transparency = TransparencyMode::WeightedBlendedOIT;
                if (frame == 16) {
                    environment.setHour(0);
                    environment.setWeather(Weather::Snow);
                }
                if (frame == 24)
                    environment.setHour(14);
            }
            Vec2 delta{float(right) - float(left), float(backward) - float(forward)};
            if (selfTest)
                delta = {std::cos(float(frame) * .05f), std::sin(float(frame) * .05f)};
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (length > 0) {
                delta.x *= float(dt) * 2 / length;
                delta.y *= float(dt) * 2 / length;
            }
            position = collision.move(position, delta, dt);
            if (!streamer) {
                position.x = std::clamp(position.x, -13.f, 13.f);
                position.y = std::clamp(position.y, -13.f, 13.f);
                collision.constrain(position);
            } else
                scene.setLocalTransform(camera,
                                        *Mat4::lookAt({position.x + 10, 10, position.y + 14},
                                                      {position.x, 0, position.y})
                                             .inverse());
            collision.synchronize(scene, playerRoot, position);
            auto body = *scene.component<SpriteRenderer>(player);
            body.sprite = image(int(frame / 8 % 4));
            scene.setComponent(player, body);
            environment.advance(dt);
            environment.apply(scene, sun, view);
            auto state = environment.state();
            fire.advance(dt);
            smoke.advance(dt);
            rain.setRate(state.weather == Weather::Rain ? 300 * state.precipitation : 0);
            snow.setRate(state.weather == Weather::Snow ? 100 * state.precipitation : 0);
            rain.advance(dt);
            snow.advance(dt);
            if (streamer) {
                Vec3 focus{position.x, 0, position.y};
                if (selfTest && frame >= 20 && frame < 40)
                    focus.x += 100; // 验证快速移动后的过期任务和远块卸载。
                streamStats = streamer->update(focus, assets);
                streamEvents();
                if (selfTest && !streamEdited && frame >= 12 && frame < 20 &&
                    streamer->chunk({2, 2})) {
                    const auto data = streamer->chunk({2, 2});
                    const auto tile =
                        std::find_if(data->tiles.begin(), data->tiles.end(),
                                     [](const auto &t) { return t.x == 0 && t.z == 0; });
                    const auto type = tile != data->tiles.end() ? tile->type : 1;
                    streamer->setTile(16, 16, type == 1 ? 2 : 1, assets);
                    streamer->requestSave();
                    streamEdited = true;
                }
                if (editTile) {
                    const auto x = int32_t(std::floor(position.x + 16)),
                               z = int32_t(std::floor(position.y + 16));
                    const TileChunkCoord chunk{int32_t(std::floor(double(x) / 8)),
                                               int32_t(std::floor(double(z) / 8))};
                    if (auto data = streamer->chunk(chunk)) {
                        const auto lx = uint32_t(int64_t(x) - int64_t(chunk.x) * 8),
                                   lz = uint32_t(int64_t(z) - int64_t(chunk.z) * 8);
                        const auto tile =
                            std::find_if(data->tiles.begin(), data->tiles.end(),
                                         [&](const auto &t) { return t.x == lx && t.z == lz; });
                        streamer->setTile(
                            x, z, tile != data->tiles.end() && tile->type == 1 ? 2 : 1, assets);
                        streamer->requestSave();
                    }
                }
                editTile = false;
            }
            auto batches = streamer ? streamer->batches(assets) : map.batches(assets);
            batches.push_back(decorations);
            batches.push_back(fire.batch(assets));
            batches.push_back(smoke.batch(assets));
            batches.push_back(rain.batch(assets));
            batches.push_back(snow.batch(assets));
            auto snapshot = extractor.extract(scene, assets, batches);
            presentation.update(scene, snapshot, view, uint32_t(width), uint32_t(height),
                                {position.x, .8f, position.y}, dt);
            snapshot = extractor.extract(scene, assets, batches);
            stats = engine.render(snapshot, view);
            engine.present();
            if (frame % 60 == 0)
                window.setTitle(
                    "Modular world / " + engine.deviceInfo().backend + " / tiles " +
                    std::to_string(streamer ? streamStats.residentChunks * 64 : map.tileCount()) +
                    " / particles " +
                    std::to_string(fire.particles().size() + smoke.particles().size() +
                                   rain.particles().size() + snow.particles().size()) +
                    " / hour " + std::to_string(state.hour));
            ++frame;
            if (frames && frame >= frames)
                break;
        }
        if (!capture.empty()) {
            if (capture.has_parent_path())
                std::filesystem::create_directories(capture.parent_path());
            engine.readback().savePpm(capture.string());
        }
        engine.waitIdle();
        if (streamer) {
            streamer->requestSave();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (streamer->statistics().dirtyChunks || streamer->statistics().pendingSaves) {
                require(std::chrono::steady_clock::now() < deadline,
                        "Timed out saving streamed world");
                streamStats = streamer->update({position.x, 0, position.y}, assets);
                streamEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (selfTest)
            require((streamer ? streamEdited && streamStats.loaded > 25 &&
                                    streamStats.unloaded > 0 && streamStats.failures == 0
                              : map.chunkCount() == 16) &&
                        collisionExercised && stats.visibleInstances > 100 &&
                        !fire.particles().empty(),
                    "World self-test did not exercise modules");
        require(engine.debugErrorCount() == 0, "World graphics validation failed");
#ifdef RENDERER_WORLD_PHYSICS2D
        if (selfTest)
            require(collision.eventCount() > 0, "Box2D events were not exercised");
        std::cout << "Box2D bodies=" << collision.bodyCount()
                  << " events=" << collision.eventCount() << '\n';
#endif
        std::cout << engine.deviceInfo().backend << " world frames=" << frame
                  << " tiles=" << (streamer ? streamStats.residentChunks * 64 : map.tileCount())
                  << " chunks=" << (streamer ? streamStats.residentChunks : map.chunkCount())
                  << " visible=" << stats.visibleInstances << " draws=" << stats.sceneDrawCalls
                  << " errors=" << engine.debugErrorCount() << '\n';
        if (streamer)
            std::cout << "stream loaded=" << streamStats.loaded
                      << " unloaded=" << streamStats.unloaded
                      << " resident=" << streamStats.residentChunks
                      << " dirty=" << streamer->statistics().dirtyChunks << '\n';
        presentation.reset(scene);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
