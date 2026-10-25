/** @file @brief 使用公共 SDK 演示异步资源、增量上传、材质、实时双相机、中文文字和 UI 调试面板。 */
#include <cmath>
#include <iostream>
#include <optional>
#include <renderer/asset_loader.hpp>
#include <renderer/renderer.hpp>
#include <renderer/sprite.hpp>
#include <renderer/text/text.hpp>
#include <renderer/ui/ui.hpp>
#include <renderer/window.hpp>
#include <thread>
using namespace renderer;
namespace {
struct Options {
    std::string backend = "opengl";
    bool offscreen = false, hidden = false, threaded = true, selfTest = false;
    uint32_t frames = 0;
    std::filesystem::path output = "features.png", font, cache = "features-cache";
};
std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
TextureDesc treeImage() {
    TextureDesc d{96, 128, std::vector<uint8_t>(96 * 128 * 4), false};
    d.sampling.mipmapped = false;
    d.sampling.addressU = d.sampling.addressV = TextureAddress::Clamp;
    for (uint32_t y = 0; y < d.height; ++y)
        for (uint32_t x = 0; x < d.width; ++x) {
            const float px = float(x) - 48, py = float(y) - 80;
            const bool crown = px * px / 1500 + py * py / 1800 < 1 ||
                               (px + 15) * (px + 15) / 800 + (py + 18) * (py + 18) / 800 < 1;
            const bool trunk = std::abs(px) < 7 && y < 75;
            if (crown || trunk) {
                auto i = (size_t(y) * d.width + x) * 4;
                d.pixels[i] = crown ? 55 : 105;
                d.pixels[i + 1] = crown ? uint8_t(140 + y / 4) : 70;
                d.pixels[i + 2] = crown ? 92 : 42;
                d.pixels[i + 3] = 255;
            }
        }
    return d;
}
Renderer renderer(const Options &o, std::unique_ptr<Window> &window) {
    RendererConfig c;
    c.renderThread = o.threaded;
    c.assetUploadBudgetBytes = 64 * 1024;
    if (!o.offscreen) {
        WindowConfig w;
        w.title = "Renderer - resource and view features";
        w.width = 1100;
        w.height = 720;
        w.visible = !o.hidden;
        w.vsync = false;
        w.graphicsApi = o.backend == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::OpenGL;
        window = std::make_unique<Window>(w);
    }
    if (o.backend == "vulkan") {
#ifdef RENDERER_FEATURES_VULKAN
        return o.offscreen ? Renderer::createVulkanOffscreen({1100, 720}, c)
                           : Renderer::createVulkan(*window, c);
#else
        throw std::runtime_error("Vulkan backend is not installed");
#endif
    }
#ifdef RENDERER_FEATURES_OPENGL
    return o.offscreen ? Renderer::createOpenGLOffscreen({1100, 720}, c)
                       : Renderer::createOpenGL(*window, c);
#else
    throw std::runtime_error("OpenGL backend is not installed");
#endif
}
int run(const std::vector<std::string> &args) {
    Options o;
    for (size_t i = 1; i < args.size(); ++i) {
        const auto value = [&]() {
            if (i + 1 >= args.size())
                throw std::invalid_argument("Missing option value");
            return args[++i];
        };
        const auto &a = args[i];
        if (a == "--backend")
            o.backend = value();
        else if (a == "--offscreen")
            o.offscreen = true;
        else if (a == "--hidden")
            o.hidden = true;
        else if (a == "--threaded")
            o.threaded = true;
        else if (a == "--single-thread")
            o.threaded = false;
        else if (a == "--self-test")
            o.selfTest = true;
        else if (a == "--frames")
            o.frames = uint32_t(std::stoul(value()));
        else if (a == "--output")
            o.output = path(value());
        else if (a == "--font")
            o.font = path(value());
        else if (a == "--cache")
            o.cache = path(value());
        else if (a == "--help") {
            std::cout << "--backend opengl|vulkan --offscreen --hidden --frames N --threaded "
                         "--single-thread --font PATH --output PNG --cache DIR --self-test\n";
            return 0;
        } else
            throw std::invalid_argument("Unknown option: " + a);
    }
    if (o.backend != "opengl" && o.backend != "vulkan")
        throw std::invalid_argument("Invalid backend");
    if (o.offscreen && !o.frames)
        o.frames = 120;
    std::filesystem::create_directories(o.cache);
    std::unique_ptr<Window> window;
    auto gpu = renderer(o, window);
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    auto camera = scene.createNode(scene.root());
    auto mapCamera = scene.createNode(scene.root());
    auto portraitCamera = scene.createNode(scene.root());
    scene.setComponent(camera, Camera{.cullingMask = 1,
                                      .projection = Camera::Projection::Orthographic,
                                      .orthographicHeight = 13});
    scene.setLocalTransform(camera, *Mat4::lookAt({11, 9, 15}, {0, 0, 0}, {0, 1, 0}).inverse());
    scene.setComponent(mapCamera, Camera{.cullingMask = 2,
                                         .projection = Camera::Projection::Orthographic,
                                         .orthographicHeight = 17});
    scene.setLocalTransform(mapCamera, *Mat4::lookAt({0, 18, 0}, {0, 0, 0}, {0, 0, -1}).inverse());
    scene.setComponent(portraitCamera, Camera{.cullingMask = 4,
                                              .projection = Camera::Projection::Orthographic,
                                              .orthographicHeight = 4});
    auto ground = scene.createNode(scene.root());
    auto mesh = assets.createMesh(geometry::plane(18));
    auto groundMat = assets.createMaterial({.baseColor = {.065f, .15f, .11f, 1}, .unlit = true});
    scene.setComponent(ground, MeshRenderer{mesh, {groundMat}});
    auto cube = assets.createMesh(geometry::cube());
    auto actor = scene.createNode(scene.root());
    scene.setComponent(
        actor,
        MeshRenderer{
            cube, {assets.createMaterial({.baseColor = {.95f, .48f, .16f, 1}, .unlit = true})}, 5});
    std::vector<NodeHandle> trees;
    // 占位网格立即可见；后台图片完成后替换为共享精灵，应用不等待磁盘读取。
    auto placeholder = assets.createMaterial({.baseColor = {.12f, .35f, .25f, 1}, .unlit = true});
    for (int z = -3; z <= 3; ++z)
        for (int x = -4; x <= 4; ++x) {
            if (std::abs(x) < 2 && std::abs(z) < 2)
                continue;
            auto n = scene.createNode(scene.root());
            scene.setLocalTransform(n, Mat4::translation({float(x) * 1.65f, 0, float(z) * 1.9f}));
            scene.setComponent(n, MeshRenderer{cube, {placeholder}, 1});
            trees.push_back(n);
            auto marker = scene.createNode(n);
            scene.setLocalTransform(marker, Mat4::translation({0, .2f, 0}) *
                                                Mat4::scaling({.45f, .2f, .45f}));
            scene.setComponent(marker, MeshRenderer{cube, {placeholder}, 2});
        }
    auto generated = treeImage();
    Image image{generated.width, generated.height, generated.pixels};
    // PNG 保存为顶行顺序，资源解码会恢复核心要求的底行顺序。
    for (uint32_t y = 0; y < image.height / 2; ++y)
        for (uint32_t x = 0; x < image.width * 4; ++x)
            std::swap(image.pixels[size_t(y) * image.width * 4 + x],
                      image.pixels[size_t(image.height - 1 - y) * image.width * 4 + x]);
    image.savePng(o.cache / "tree.png");
    AssetLoader loader;
    auto request = loader.loadTexture(o.cache / "tree.png", false, 10, generated.sampling);
    UiContext ui({1100, 720});
    ui.loadFont(RENDERER_FEATURES_FONT);
    TextSystem text;
    auto latin = text.loadFont(RENDERER_FEATURES_FONT);
    auto worldFont = latin;
    if (o.font.empty()) {
#ifdef _WIN32
        if (std::filesystem::exists("C:/Windows/Fonts/msyh.ttc"))
            o.font = "C:/Windows/Fonts/msyh.ttc";
#endif
    }
    if (!o.font.empty()) {
        ui.loadFont(o.font, true);
        worldFont = text.loadFont(o.font);
        text.setFallbackFonts(std::span(&worldFont, 1));
    }
    TextStyle style;
    style.font = worldFont;
    style.fontSize = 24;
    text.prewarm(style, "森林营地 探险者 0123456789");
    WorldTextDesc label;
    label.style = style;
    label.text = worldFont == latin ? "Explorer" : "探险者";
    label.node = actor;
    label.transform = Mat4::translation({0, 1.3f, 0});
    label.sizeMode = TextSizeMode::ScreenPixels;
    text.create(label);
    auto map = gpu.createRenderTexture({300, 200});
    auto portrait = gpu.createRenderTexture({140, 140});
    ui.registerImage("map", map);
    ui.registerImage("portrait", portrait);
    auto document = ui.loadDocumentFromMemory(R"(<rml><head><style>
body{width:100%;height:100%;font-family:LatoLatin;font-size:17dp;color:#e5f4ed;pointer-events:none;}
div,h2,p{display:block;}
.card{position:absolute;background:#142a2bea;border:1dp #427569;padding:14dp;pointer-events:auto;}
#map{right:18dp;top:18dp;width:300dp;} #profile{right:18dp;bottom:18dp;width:300dp;}
#bar{left:18dp;bottom:18dp;width:640dp;}input{display:block;width:320dp;height:28dp;padding:6dp;margin:8dp 0;background-color:#24443d;color:white;tab-index:auto;}
h2{font-size:20dp;margin-bottom:10dp;}p{margin-bottom:8dp;}
</style></head><body><div class="card" id="map"><h2>实时小地图 / GPU camera</h2><img src="ui:map" width="300" height="200"/></div>
<div class="card" id="profile"><h2>角色预览</h2><img src="ui:portrait" width="140" height="140"/><p id="load">后台读取图片中...</p></div>
<div class="card" id="bar"><h2>森林营地 · 渲染功能示例</h2><p>风摆材质 · 加色光点 · 静态实例缓存 · 双相机</p><input id="name" type="text" value=""/><p>中文输入 / Ctrl+C / Ctrl+V / Esc 退出</p></div></body></rml>)");
    EventSubscription events;
    if (window) {
        ui.setPlatformServices({[&](const auto &s) { window->setClipboardText(s); },
                                [&] { return window->clipboardText(); }});
        events = window->subscribe([&](const WindowEvent &e) {
            const bool captured = ui.processEvent(e);
            if (auto k = std::get_if<KeyEvent>(&e);
                k && k->key == Key::Escape && k->action == InputAction::Press && !captured)
                window->requestClose();
        });
    }
    bool loaded = false;
    uint32_t count = 0;
    RenderStats stats;
    uint64_t reused = 0;
    auto view = RenderView{.cameraNode = camera};
    view.shadows = false;
    view.antiAliasing = AntiAliasing::FXAA;
    view.transparency = TransparencyMode::WeightedBlendedOIT;
    auto mv = view;
    mv.cameraNode = mapCamera;
    mv.antiAliasing = AntiAliasing::None;
    auto pv = mv;
    pv.cameraNode = portraitCamera;
    while ((!window || !window->shouldClose()) && (!o.frames || count < o.frames)) {
        if (window)
            window->pollEvents();
        for (const auto &r : loader.publish(assets)) {
            if (r.info.state != AssetLoadState::Published)
                throw std::runtime_error(r.info.error);
            MaterialDesc wind;
            wind.unlit = true;
            wind.doubleSided = true;
            wind.baseColorTexture = r.texture;
            wind.alphaMode = AlphaMode::Blend;
            wind.effect.vertex = "position.x+=sin(effectTime*1.8+position.y*4.0)*effectParameters."
                                 "x*(position.y+0.5);";
            wind.effect.parameters = {.045f, 0, 0, 0};
            wind.effect.boundsPadding = .15f;
            SpriteDesc sprite;
            sprite.texture = r.texture;
            sprite.rect = {0, 0, 96, 128};
            sprite.pixelsPerUnit = 64;
            sprite.material = assets.createMaterial(wind);
            auto handle = assets.createSprite(sprite);
            for (auto n : trees) {
                scene.removeComponent<MeshRenderer>(n);
                scene.setComponent(n, SpriteRenderer{.sprite = handle, .layerMask = 1});
            }
            loaded = true;
            ui.setText(document, "load", "异步资源已发布 / Shader 已生效");
            loader.forget(r.info.id);
            MaterialDesc glow;
            glow.unlit = true;
            glow.doubleSided = true;
            glow.alphaMode = AlphaMode::Blend;
            glow.blend = MaterialBlend::Additive;
            glow.baseColor = {1, .32f, .045f, .25f};
            auto glowMat = assets.createMaterial(glow);
            for (int i = 0; i < 7; ++i) {
                auto n = scene.createNode(actor);
                scene.setLocalTransform(
                    n, Mat4::translation({float(i - 3) * .17f, .9f + float(i % 3) * .16f, .1f}) *
                           Mat4::scaling({.09f, .09f, .09f}));
                scene.setComponent(n, MeshRenderer{cube, {glowMat}, 5});
            }
        }
        const float time = float(count) / 60;
        const Vec3 position{std::sin(time * .55f) * 2, .5f, std::cos(time * .55f) * 1.2f};
        scene.setLocalTransform(actor, Mat4::translation(position));
        scene.setLocalTransform(portraitCamera, *Mat4::lookAt(position + Vec3{3, 2, 4},
                                                              position + Vec3{0, .4f, 0}, {0, 1, 0})
                                                     .inverse());
        if (o.selfTest && count == 20) {
            ui.focus(document, "name");
            ui.processEvent(TextCompositionEvent{CompositionPhase::Begin});
            ui.processEvent(TextCompositionEvent{CompositionPhase::Update, "zhong", 5});
            ui.processEvent(TextCompositionEvent{CompositionPhase::Commit, "中文", 2});
            ui.processEvent(TextCompositionEvent{CompositionPhase::End});
        }
        if (o.selfTest && count == 40) {
            text.resetGlyphCache();
            text.prewarm(style, "森林营地 探险者 0123456789");
        }
        uint32_t width = 1100, height = 720;
        if (window) {
            auto [w, h] = window->framebufferSize();
            if (w <= 0 || h <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            width = uint32_t(w);
            height = uint32_t(h);
        }
        auto base = extractor.extract(scene, assets);
        mv.timeSeconds = pv.timeSeconds = view.timeSeconds = time;
        gpu.renderToTexture(base, mv, map);
        gpu.renderToTexture(base, pv, portrait);
        auto batch = text.batch(scene, camera, width, height);
        auto main = extractor.extract(scene, assets, std::span(&batch, 1));
        ui.setDebugPanel(stats, gpu.memoryStatistics(), assets.memoryStatistics());
        ui.update(1.0 / 60);
        if (window)
            window->setTextInputArea(ui.textInputArea());
        view.overlay = ui.snapshot();
        stats = gpu.render(main, view);
        ui.takeEvents();
        reused += stats.instanceReusedBytes;
        if (window)
            gpu.present();
        ++count;
    }
    gpu.waitIdle();
    gpu.readback().savePng(o.output);
    if (o.selfTest) {
        const auto rect = ui.element(document, "map").bounds;
        if (rect.x < 0 || rect.y < 0 || rect.x + rect.width > 1100 || rect.y + rect.height > 720)
            throw std::runtime_error("Preview panel layout outside viewport");
    }
    for (const auto &message : ui.takeMessages())
        std::cerr << message << '\n';
    if (o.selfTest && (!loaded || !reused || gpu.debugErrorCount() ||
                       ui.element(document, "name").value != "中文"))
        throw std::runtime_error("Feature integration verification failed");
    if (!loaded)
        std::cerr << "Image decoding has not completed; increase --frames\n";
    std::cout << gpu.deviceInfo().backend << ": " << count << " frames, reused " << reused
              << " instance bytes, texture request " << request << '\n';
    return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            auto s = std::filesystem::path(argv[i]).u8string();
            args.emplace_back(s.begin(), s.end());
        }
        return run(args);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char **argv) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
