/** @file @brief 并排展示独立图片/纹理数组/BC3 数组，以及位图和 SDF 文字缩放。 */
#include <array>
#include <cmath>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
#include <renderer/texture.hpp>
#include <renderer/window.hpp>
using namespace renderer;
namespace {
std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
TextureDesc image(unsigned kind) {
    TextureDesc d{96, 128, std::vector<uint8_t>(96 * 128 * 4, 0), true};
    d.sampling.addressU = d.sampling.addressV = TextureAddress::Clamp;
    const std::array<std::array<uint8_t, 3>, 4> colors{
        {{70, 185, 130}, {230, 155, 65}, {95, 150, 230}, {200, 100, 180}}};
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 96; ++x) {
            float px = float(x) - 48, py = float(y) - 80;
            bool crown = px * px / (1500 + kind * 80) + py * py / 1450 < 1;
            bool trunk = std::abs(px) < 6 && y < 76;
            if (!crown && !trunk)
                continue;
            auto i = (size_t(y) * 96 + x) * 4;
            for (unsigned c = 0; c < 3; ++c)
                d.pixels[i + c] = crown ? colors[kind][c]
                                        : uint8_t(c == 0   ? 135
                                                  : c == 1 ? 90
                                                           : 55);
            d.pixels[i + 3] = 255;
        }
    return d;
}
int run(const std::vector<std::string> &args) {
    std::string backend = "opengl";
    bool offscreen = false, hidden = false, threaded = true, self = false;
    unsigned frames = 0;
    std::filesystem::path output = "texture-text.png", font, cache = "texture-text-cache";
    for (size_t i = 1; i < args.size(); ++i) {
        auto value = [&]() {
            if (i + 1 >= args.size())
                throw std::invalid_argument("Missing option value");
            return args[++i];
        };
        const auto &a = args[i];
        if (a == "--backend")
            backend = value();
        else if (a == "--offscreen")
            offscreen = true;
        else if (a == "--hidden")
            hidden = true;
        else if (a == "--threaded")
            threaded = true;
        else if (a == "--single-thread")
            threaded = false;
        else if (a == "--self-test")
            self = true;
        else if (a == "--frames")
            frames = unsigned(std::stoul(value()));
        else if (a == "--output")
            output = path(value());
        else if (a == "--cache")
            cache = path(value());
        else if (a == "--font")
            font = path(value());
        else if (a == "--help") {
            std::cout << "--backend opengl|vulkan --offscreen --hidden --threaded --single-thread "
                         "--frames N --font FONT --output PNG --cache DIR --self-test\nQ/E zoom; F "
                         "FXAA; Escape quit\n";
            return 0;
        } else
            throw std::invalid_argument("Unknown option: " + a);
    }
    if (backend != "opengl" && backend != "vulkan")
        throw std::invalid_argument("Invalid backend");
    if (offscreen && !frames)
        frames = 60;
    std::unique_ptr<Window> window;
    RendererConfig config;
    config.renderThread = threaded;
    if (!offscreen) {
        WindowConfig w;
        w.width = 1100;
        w.height = 720;
        w.title = "Texture arrays / BC3 / SDF";
        w.visible = !hidden;
        w.vsync = false;
        w.graphicsApi = backend == "opengl" ? GraphicsApi::OpenGL : GraphicsApi::Vulkan;
        window = std::make_unique<Window>(w);
    }
    auto create = [&]() -> Renderer {
        if (backend == "vulkan") {
#ifdef RENDERER_TEXTURE_VULKAN
            return offscreen ? Renderer::createVulkanOffscreen({1100, 720}, config)
                             : Renderer::createVulkan(*window, config);
#else
            throw std::runtime_error("Vulkan backend is unavailable");
#endif
        }
#ifdef RENDERER_TEXTURE_OPENGL
        return offscreen ? Renderer::createOpenGLOffscreen({1100, 720}, config)
                         : Renderer::createOpenGL(*window, config);
#else
        throw std::runtime_error("OpenGL backend is unavailable");
#endif
    };
    auto gpu = create();
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    TextSystem text;
    auto camera = scene.createNode(scene.root());
    Camera cam;
    cam.projection = Camera::Projection::Orthographic;
    cam.orthographicHeight = 10;
    scene.setComponent(camera, cam);
    scene.setLocalTransform(camera, Mat4::translation({0, 0, 12}));
    auto latin = text.loadFont(RENDERER_TEXTURE_FONT);
#ifdef _WIN32
    if (font.empty() && std::filesystem::exists("C:/Windows/Fonts/msyh.ttc"))
        font = "C:/Windows/Fonts/msyh.ttc";
#endif
    if (!font.empty()) {
        auto chinese = text.loadFont(font);
        text.setFallbackFonts(std::span(&chinese, 1));
    }
    std::array<TextureDesc, 4> pictures{image(0), image(1), image(2), image(3)};
    auto array = texture::makeArray(pictures);
    auto packed = texture::compress(array, TextureCompression::BC3);
    std::filesystem::create_directories(cache);
    texture::saveDds(packed, cache / "sprites.dds");
    auto compressed = assets.loadTexture(cache / "sprites.dds");
    auto rawArray = assets.createTexture(array);
    std::array<SpriteHandle, 4> separate{}, raw{}, bc{};
    for (unsigned i = 0; i < 4; ++i) {
        SpriteDesc d;
        d.rect = {0, 0, 96, 128};
        d.pixelsPerUnit = 128;
        d.pivot = {.5f, .5f};
        d.alphaMode = AlphaMode::Mask;
        d.texture = assets.createTexture(pictures[i]);
        separate[i] = assets.createSprite(d);
        d.textureLayer = i;
        d.texture = rawArray;
        raw[i] = assets.createSprite(d);
        d.texture = compressed;
        bc[i] = assets.createSprite(d);
    }
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned i = 0; i < 8; ++i) {
            auto n = scene.createNode(scene.root());
            auto sprite = row == 0 ? separate[i % 4] : row == 1 ? raw[i % 4] : bc[i % 4];
            scene.setLocalTransform(
                n, Mat4::translation({-5.5f + float(i) * 1.55f, 2.7f - float(row) * 1.75f, 0}));
            scene.setComponent(n,
                               SpriteRenderer{.sprite = sprite, .billboard = BillboardMode::Fixed});
        }
    auto label = [&](std::string str, Vec3 pos, unsigned size, TextRasterMode mode, bool screen,
                     Vec4 color) {
        WorldTextDesc d;
        d.style.font = latin;
        d.style.fontSize = size;
        d.style.rasterMode = mode;
        d.text = std::move(str);
        d.transform = Mat4::translation(pos);
        d.anchor = {0, 0};
        d.color = color;
        d.pixelsPerUnit = 24;
        d.sizeMode = screen ? TextSizeMode::ScreenPixels : TextSizeMode::WorldUnits;
        d.depthTest = false;
        return text.create(d);
    };
    label("Texture arrays / BC3 / SDF", {-6.4f, 4.15f, 0}, 30, TextRasterMode::Sdf, true,
          {.9f, .98f, 1, 1});
    label("RGBA8 separate images", {-6.4f, 3.45f, 0}, 19, TextRasterMode::Sdf, true,
          {.65f, .85f, .8f, 1});
    label("RGBA8 array: one shared material", {-6.4f, 1.7f, 0}, 19, TextRasterMode::Sdf, true,
          {.65f, .85f, .8f, 1});
    label("BC3 array + authored mip chain", {-6.4f, -.05f, 0}, 19, TextRasterMode::Sdf, true,
          {.65f, .85f, .8f, 1});
    label("BITMAP", {-5.5f, -3.1f, 0}, 24, TextRasterMode::Bitmap, false, {1, 1, 1, 1});
    auto sdf = label("SDF", {1.1f, -3.1f, 0}, 24, TextRasterMode::Sdf, false, {1, 1, 1, 1});
    auto desc = text.description(sdf);
    desc.style.outlineColor = {.02f, .25f, .24f, 1};
    desc.style.outlineWidth = 1;
    text.setDescription(sdf, desc);
    auto statsLabel =
        label("", {-6.4f, -4.3f, 0}, 17, TextRasterMode::Sdf, true, {.85f, .9f, 1, 1});
    text.prewarm({.font = latin, .fontSize = 24, .rasterMode = TextRasterMode::Sdf},
                 "Texture arrays / BC3 / SDF RGBA8 BITMAP");
    RenderView view;
    view.cameraNode = camera;
    view.shadows = false;
    view.clearColor = {.025f, .045f, .065f, 1};
    RenderStats stats;
    unsigned frame = 0;
    uint64_t reused = 0;
    EventSubscription events;
    if (window)
        events = window->subscribe<KeyEvent>([&](const KeyEvent &e) {
            if (e.action == InputAction::Release)
                return;
            if (e.key == Key::Escape)
                window->requestClose();
            if (e.key == Key::Q)
                cam.orthographicHeight = std::max(6.f, cam.orthographicHeight - .5f);
            if (e.key == Key::E)
                cam.orthographicHeight = std::min(16.f, cam.orthographicHeight + .5f);
            if (e.key == Key::F)
                view.antiAliasing = view.antiAliasing == AntiAliasing::None ? AntiAliasing::FXAA
                                                                            : AntiAliasing::None;
            scene.setComponent(camera, cam);
        });
    while ((!window || !window->shouldClose()) && (!frames || frame < frames)) {
        if (window)
            window->pollEvents();
        if (self && frame == 16) {
            cam.orthographicHeight = 8;
            scene.setComponent(camera, cam);
        }
        if (self && frame == 32) {
            text.resetGlyphCache();
        }
        if (self && frame == 44) {
            cam.orthographicHeight = 10;
            scene.setComponent(camera, cam);
        }
        uint32_t width = 1100, height = 720;
        if (window) {
            auto [w, h] = window->framebufferSize();
            if (w <= 0 || h <= 0)
                continue;
            width = uint32_t(w);
            height = uint32_t(h);
        }
        text.setText(statsLabel, "RGBA8 array " + std::to_string(array.pixels.size()) +
                                     " B | BC3 + mips " + std::to_string(packed.data.size()) +
                                     " B | Q/E zoom | F FXAA");
        auto batch = text.batch(scene, camera, width, height);
        auto snapshot = extractor.extract(scene, assets, std::span(&batch, 1));
        stats = gpu.render(snapshot, view);
        reused += stats.instanceReusedBytes;
        if (window)
            gpu.present();
        ++frame;
    }
    gpu.waitIdle();
    gpu.readback().savePng(output);
    auto memory = gpu.memoryStatistics();
    if (self && (!reused || !memory.compressedTextureBytes || memory.textureArrayLayers < 8 ||
                 gpu.debugErrorCount()))
        throw std::runtime_error("Texture/text integration failed");
    std::cout << gpu.deviceInfo().backend << ": " << stats.sceneDrawCalls << " scene draws, "
              << memory.compressedTextureBytes << " compressed GPU bytes, "
              << text.statistics().cachedGlyphs << " cached glyphs\n";
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
        return run({argv, argv + argc});
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
