/** @file @brief 使用公开文字 SDK 展示路牌、Billboard 名字、遮挡、固定屏幕字号、动态文字和离屏输出。
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
#include <renderer/window.hpp>
#include <stdexcept>
#include <thread>
using namespace renderer;
namespace {
void require(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
int run(int argc, char **argv) {
    try {
        std::string backend = "auto";
        bool offscreen = false, hidden = false, threaded = false, selfTest = false;
        unsigned frames = 0;
        std::filesystem::path output = "text.png", fontPath = RENDERER_TEXT_FONT, fallbackPath;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&] {
                if (++i >= argc)
                    throw std::invalid_argument("Missing text example argument");
                return std::string(argv[i]);
            };
            if (arg == "--backend")
                backend = value();
            else if (arg == "--offscreen")
                offscreen = true;
            else if (arg == "--hidden")
                hidden = true;
            else if (arg == "--threaded")
                threaded = true;
            else if (arg == "--single-thread")
                threaded = false;
            else if (arg == "--self-test")
                selfTest = true;
            else if (arg == "--frames")
                frames = unsigned(std::stoul(value()));
            else if (arg == "--output")
                output = path(value());
            else if (arg == "--font")
                fontPath = path(value());
            else if (arg == "--fallback-font")
                fallbackPath = path(value());
            else if (arg == "--help") {
                std::cout << "renderer_text_demo [--backend opengl|vulkan] [--offscreen|--hidden] "
                             "[--frames N] [--threaded] [--self-test] [--output PNG] [--font TTF] "
                             "[--fallback-font TTF]\n";
                return 0;
            } else
                throw std::invalid_argument("Unknown world text option");
        }
        if (backend == "auto") {
#ifdef RENDERER_TEXT_VULKAN
            backend = "vulkan";
#else
            backend = "opengl";
#endif
        }
        require(backend == "opengl" || backend == "vulkan", "Invalid text backend");
        if (offscreen && !frames)
            frames = selfTest ? 36 : 1;
        std::unique_ptr<Window> window;
        std::unique_ptr<Renderer> renderer;
        if (!offscreen)
            window = std::make_unique<Window>(WindowConfig{
                .width = 960,
                .height = 600,
                .title = "Renderer / World Text",
                .visible = !hidden,
                .graphicsApi = backend == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::OpenGL});
        RendererConfig config{.renderThread = threaded};
        if (backend == "vulkan") {
#ifdef RENDERER_TEXT_VULKAN
            renderer = std::make_unique<Renderer>(
                offscreen ? Renderer::createVulkanOffscreen({960, 600}, config)
                          : Renderer::createVulkan(*window, config));
#else
            throw std::runtime_error("Vulkan backend unavailable");
#endif
        } else {
#ifdef RENDERER_TEXT_OPENGL
            renderer = std::make_unique<Renderer>(
                offscreen ? Renderer::createOpenGLOffscreen({960, 600}, config)
                          : Renderer::createOpenGL(*window, config));
#else
            throw std::runtime_error("OpenGL backend unavailable");
#endif
        }
        Scene scene;
        AssetStore assets;
        RenderExtractor extractor;
        TextSystem text;
        auto font = text.loadFont(fontPath);
        if (!fallbackPath.empty()) {
            auto fallback = text.loadFont(fallbackPath);
            text.setFallbackFonts(std::span{&fallback, 1});
        }
        auto camera = scene.createNode(scene.root(), "Camera");
        Camera cam;
        cam.verticalFov = radians(50);
        cam.orthographicHeight = 8;
        scene.setComponent(camera, cam);
        const auto cube = assets.createMesh(geometry::cube());
        auto box = [&](const char *name, Vec3 p, Vec3 scale, Vec4 color) {
            auto node = scene.createNode(scene.root(), name);
            auto material = assets.createMaterial({.baseColor = color, .unlit = true});
            scene.setComponent(node, MeshRenderer{cube, {material}});
            scene.setLocalTransform(node, Mat4::translation(p) * Mat4::scaling(scale));
            return node;
        };
        box("Ground", {0, -.18f, 0}, {14, .2f, 8}, {.08f, .13f, .11f, 1});
        box("Sign board", {-3, 1.1f, -.2f}, {3, 1.9f, .2f}, {.12f, .2f, .16f, 1});
        box("Occluding wall", {0, 1.1f, .6f}, {2.6f, 2.1f, .3f}, {.14f, .23f, .27f, 1});
        auto actor = box("Actor", {3, .5f, 0}, {.65f, 1, .65f}, {.25f, .5f, .65f, 1});
        WorldTextDesc d;
        d.style = {
            .font = font, .fontSize = 40, .maxWidth = 220, .alignment = TextAlignment::Center};
        d.text = "WAYPOINT\nFixed world text";
        d.transform = Mat4::translation({-3, .55f, 0});
        d.billboard = BillboardMode::Fixed;
        d.pixelsPerUnit = 95;
        d.color = {1, .85f, .5f, 1};
        const auto sign = text.create(d);
        (void)sign;
        d.style.maxWidth = 0;
        d.text = "Upright billboard";
        d.transform = Mat4::translation({-3, 2.5f, 0});
        d.billboard = BillboardMode::Upright;
        d.pixelsPerUnit = 100;
        const auto billboard = text.create(d);
        d.text = "Visible\nDepth OFF";
        d.depthTest = false;
        d.transform = Mat4::translation({0, .55f, 0});
        d.pixelsPerUnit = 85;
        d.billboard = BillboardMode::ViewPlane;
        d.color = {1, .35f, .2f, 1};
        const auto depth = text.create(d);
        d.depthTest = true;
        d.node = actor;
        d.text = "Ranger / 24 px";
        d.transform = Mat4::translation({0, 1.5f, 0});
        d.style.fontSize = 24;
        d.sizeMode = TextSizeMode::ScreenPixels;
        d.anchor = {.5f, 0};
        d.color = {.65f, 1, .85f, 1};
        const auto name = text.create(d);
        float yaw = 0, distance = 10;
        bool orbitLeft = false, orbitRight = false;
        auto toggleDepth = [&] {
            auto v = text.description(depth);
            v.depthTest = !v.depthTest;
            v.text = v.depthTest ? "Occluded\nDepth ON" : "Visible\nDepth OFF";
            text.setDescription(depth, v);
        };
        RenderView view{.cameraNode = camera,
                        .clearColor = {.025f, .035f, .045f, 1},
                        .shadows = false,
                        .antiAliasing = AntiAliasing::FXAA};
        EventSubscription input;
        if (window)
            input = window->subscribe([&](const WindowEvent &event) {
                if (const auto *e = std::get_if<KeyEvent>(&event)) {
                    const bool down = e->action != InputAction::Release;
                    if (e->key == Key::Q)
                        orbitLeft = down;
                    if (e->key == Key::E)
                        orbitRight = down;
                    if (e->action == InputAction::Press) {
                        if (e->key == Key::Escape)
                            window->requestClose();
                        if (e->key == Key::D)
                            toggleDepth();
                        if (e->key == Key::B) {
                            auto v = text.description(billboard);
                            v.billboard = BillboardMode((int(v.billboard) + 1) % 3);
                            text.setDescription(billboard, v);
                        }
                        if (e->key == Key::P) {
                            cam.projection = cam.projection == Camera::Projection::Perspective
                                                 ? Camera::Projection::Orthographic
                                                 : Camera::Projection::Perspective;
                            scene.setComponent(camera, cam);
                        }
                        if (e->key == Key::S) {
                            auto v = text.description(name);
                            v.sizeMode = v.sizeMode == TextSizeMode::ScreenPixels
                                             ? TextSizeMode::WorldUnits
                                             : TextSizeMode::ScreenPixels;
                            text.setDescription(name, v);
                        }
                        if (e->key == Key::O)
                            view.transparency = view.transparency == TransparencyMode::SortedAlpha
                                                    ? TransparencyMode::WeightedBlendedOIT
                                                    : TransparencyMode::SortedAlpha;
                        if (e->key == Key::F)
                            view.antiAliasing = view.antiAliasing == AntiAliasing::None
                                                    ? AntiAliasing::FXAA
                                                    : AntiAliasing::None;
                    }
                } else if (const auto *scroll = std::get_if<MouseScrollEvent>(&event))
                    distance = std::clamp(distance - float(scroll->yOffset), 5.f, 30.f);
                else if (const auto *focus = std::get_if<WindowFocusEvent>(&event))
                    if (!focus->focused)
                        orbitLeft = orbitRight = false;
            });
        std::cout << "Q/E orbit, wheel zoom, D depth, B billboard, P projection, S pixel/world "
                     "size, O OIT, F FXAA\n";
        unsigned frame = 0;
        bool depthOn = false, depthOff = false, screen = false, world = false;
        auto previous = std::chrono::steady_clock::now();
        for (; (!frames || frame < frames) && (!window || !window->shouldClose()); ++frame) {
            if (window)
                window->pollEvents();
            const auto now = std::chrono::steady_clock::now();
            const double dt =
                frames ? 1. / 60
                       : std::clamp(std::chrono::duration<double>(now - previous).count(), 0., .05);
            previous = now;
            yaw += float(dt) * (float(orbitRight) - float(orbitLeft));
            if (selfTest) {
                if (frame == 8 || frame == 16)
                    toggleDepth();
                if (frame == 20) {
                    cam.projection = Camera::Projection::Orthographic;
                    scene.setComponent(camera, cam);
                }
                if (frame == 24) {
                    auto v = text.description(name);
                    v.sizeMode = TextSizeMode::WorldUnits;
                    text.setDescription(name, v);
                }
                if (frame == 28) {
                    auto v = text.description(name);
                    v.sizeMode = TextSizeMode::ScreenPixels;
                    text.setDescription(name, v);
                }
                if (frame == 30) {
                    view.transparency = TransparencyMode::WeightedBlendedOIT;
                    view.antiAliasing = AntiAliasing::None;
                }
            }
            scene.setLocalTransform(
                camera,
                *Mat4::lookAt({std::sin(yaw) * distance, 3.2f, std::cos(yaw) * distance}, {0, 1, 0})
                     .inverse());
            scene.setLocalTransform(
                actor, Mat4::translation({3 + std::sin(float(frame) * .025f) * .4f, .5f, 0}));
            text.setText(name, "Ranger / " + std::to_string(frame) + " / 24 px");
            auto [w, h] = window ? window->framebufferSize() : std::pair{960, 600};
            if (w <= 0 || h <= 0)
                continue;
            auto batch = text.batch(scene, camera, unsigned(w), unsigned(h));
            auto snapshot = extractor.extract(scene, assets, std::span{&batch, 1});
            auto stats = renderer->render(snapshot, view);
            require(stats.visibleInstances > 4, "World text geometry missing");
            if (window)
                renderer->present();
            depthOn |= text.description(depth).depthTest;
            depthOff |= !text.description(depth).depthTest;
            screen |= text.description(name).sizeMode == TextSizeMode::ScreenPixels;
            world |= text.description(name).sizeMode == TextSizeMode::WorldUnits;
            if (window && !selfTest)
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        require(frame > 0, "World text example rendered no frames");
        renderer->readback().savePng(output);
        renderer->waitIdle();
        require(renderer->debugErrorCount() == 0, "World text backend error");
        if (selfTest)
            require(depthOn && depthOff && screen && world,
                    "World text self-test did not exercise modes");
        auto stats = text.statistics();
        std::cout << "Text: " << stats.cachedGlyphs << " cached glyphs, " << stats.atlasPages
                  << " pages, " << stats.publishedGlyphs << " published glyphs\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> values;
    for (int i = 0; i < argc; ++i) {
        auto s = std::filesystem::path(argv[i]).u8string();
        values.emplace_back(s.begin(), s.end());
    }
    std::vector<char *> pointers;
    for (auto &s : values)
        pointers.push_back(s.data());
    return run(argc, pointers.data());
}
#else
int main(int argc, char **argv) {
    return run(argc, argv);
}
#endif
