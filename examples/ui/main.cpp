/** @file @brief 使用公开 UI SDK 展示基础游戏控件、RML/RCSS 换肤、输入捕获和双后端窗口/离屏输出。 */
#include <algorithm>
#include <chrono>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/ui/ui.hpp>
#include <renderer/window.hpp>
#include <stdexcept>
#include <thread>
using namespace renderer;
namespace {
TextureDesc icon(unsigned type) {
    TextureDesc d{32, 32, std::vector<uint8_t>(32 * 32 * 4), true};
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x) {
            auto i = (y * 32 + x) * 4;
            const bool shape =
                type == 0   ? x > 7 && x < 24 && y > 4 && y < 28
                : type == 1 ? (int(x) - 16) * (int(x) - 16) + (int(y) - 16) * (int(y) - 16) < 160
                            : x > 5 && x < 27 && y > 9 && y < 24;
            if (shape) {
                d.pixels[i] = type == 0 ? 158 : type == 1 ? 158 : 220;
                d.pixels[i + 1] = type == 0 ? 110 : type == 1 ? 170 : 110;
                d.pixels[i + 2] = type == 0 ? 63 : type == 1 ? 165 : 55;
                d.pixels[i + 3] = 255;
            }
        }
    return d;
}
std::filesystem::path fromUtf8(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
void require(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
} // namespace
int runUi(int argc, char **argv) {
    try {
        std::string backend = "auto";
        bool offscreen = false, hidden = false, threaded = false, selfTest = false;
        unsigned frames = 0;
        std::filesystem::path output = "ui.png", font = RENDERER_UI_FONT;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto value = [&] {
                if (++i >= argc)
                    throw std::invalid_argument("Missing UI argument value");
                return std::string(argv[i]);
            };
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
                selfTest = true;
            else if (a == "--frames")
                frames = unsigned(std::stoul(value()));
            else if (a == "--output")
                output = fromUtf8(value());
            else if (a == "--font")
                font = fromUtf8(value());
            else
                throw std::invalid_argument("Unknown UI option: " + a);
        }
        if (backend == "auto") {
#ifdef RENDERER_UI_VULKAN
            backend = "vulkan";
#else
            backend = "opengl";
#endif
        }
        require(backend == "opengl" || backend == "vulkan", "Invalid UI backend");
        if (offscreen && frames == 0)
            frames = selfTest ? 36 : 1;
        std::unique_ptr<Window> window;
        std::unique_ptr<Renderer> renderer;
        if (!offscreen)
            window = std::make_unique<Window>(WindowConfig{
                .width = 640,
                .height = 400,
                .title = "Renderer / Game UI",
                .visible = !hidden,
                .graphicsApi = backend == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::OpenGL});
        const RendererConfig config{.renderThread = threaded};
        if (backend == "vulkan") {
#ifdef RENDERER_UI_VULKAN
            renderer = std::make_unique<Renderer>(
                offscreen ? Renderer::createVulkanOffscreen({640, 400}, config)
                          : Renderer::createVulkan(*window, config));
#else
            throw std::runtime_error("Vulkan is unavailable in this SDK");
#endif
        } else {
#ifdef RENDERER_UI_OPENGL
            renderer = std::make_unique<Renderer>(
                offscreen ? Renderer::createOpenGLOffscreen({640, 400}, config)
                          : Renderer::createOpenGL(*window, config));
#else
            throw std::runtime_error("OpenGL is unavailable in this SDK");
#endif
        }
        UiContext ui({640, 400});
        if (window)
            ui.setPlatformServices({[&](const auto &value) { window->setClipboardText(value); },
                                    [&] { return window->clipboardText(); }});
        ui.loadFont(RENDERER_UI_FONT);
        if (font != std::filesystem::path(RENDERER_UI_FONT))
            ui.loadFont(font, true);
        for (unsigned i = 0; i < 3; ++i)
            ui.registerImage(i == 0 ? "wood" : i == 1 ? "stone" : "food", icon(i));
        const auto doc = ui.loadDocument(RENDERER_UI_DOCUMENT);
        ui.update(0);
        unsigned gathered = 0, changes = 0;
        std::vector<UiSubscription> actions;
        actions.push_back(ui.subscribe(doc, "action", "click", [&](const auto &) {
            ui.setText(doc, "status", "Gathered wood: " + std::to_string(++gathered));
        }));
        actions.push_back(ui.subscribe(doc, "music", "change", [&](const auto &) { ++changes; }));
        actions.push_back(ui.subscribe(doc, "volume", "change", [&](const UiEvent &e) {
            ui.setText(doc, "volume-label", std::to_string(int(std::stof(e.value))) + "%");
            ++changes;
        }));
        for (const auto *id : {"wood", "stone", "food"})
            actions.push_back(ui.subscribe(doc, id, "mouseover", [&, id](const auto &) {
                ui.setText(doc, "tip", std::string(id) + " / material for crafting");
            }));
        EventSubscription subscription;
        if (window) {
            const auto [iw, ih] = window->clientSize();
            const auto [w, h] = window->framebufferSize();
            ui.setViewport(
                {unsigned(w), unsigned(h), double(iw), double(ih), float(w) / float(iw)});
            subscription = window->subscribe([&](const WindowEvent &e) { ui.processEvent(e); });
        }
        AssetStore assets;
        Scene scene;
        const auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{});
        RenderExtractor extractor;
        auto snapshot = extractor.extract(scene, assets);
        RenderView view{.cameraNode = camera,
                        .clearColor = {.035f, .055f, .045f, 1},
                        .shadows = false,
                        .antiAliasing = AntiAliasing::FXAA};
        const auto inputPoint = [&](double x, double y) {
            if (window) {
                const auto [cw, ch] = window->clientSize();
                const auto [pw, ph] = window->framebufferSize();
                return Vec2{float(x * cw / pw), float(y * ch / ph)};
            }
            return Vec2{float(x), float(y)};
        };
        const auto click = [&](const char *id) {
            auto b = ui.element(doc, id).bounds;
            const auto point = inputPoint(b.x + b.width * .5, b.y + b.height * .5);
            const double x = point.x, y = point.y;
            ui.processEvent(MouseMoveEvent{x, y});
            ui.processEvent(
                MouseButtonEvent{MouseButton::Left, InputAction::Press, Modifiers::None, x, y});
            ui.processEvent(
                MouseButtonEvent{MouseButton::Left, InputAction::Release, Modifiers::None, x, y});
        };
        unsigned frame = 0;
        for (; (!frames || frame < frames) && (!window || !window->shouldClose()); ++frame) {
            if (window)
                window->pollEvents();
            if (selfTest) {
                if (frame == 4)
                    click("action");
                if (frame == 8)
                    click("music");
                if (frame == 12)
                    click("volume");
                if (frame == 16) {
                    ui.focus(doc, "name");
                    ui.processEvent(KeyEvent{Key::End});
                    ui.processEvent(TextInputEvent{U'!'});
                }
                if (frame == 22) {
                    auto r = ui.element(doc, "recipes").bounds;
                    const auto point = inputPoint(r.x + 10, r.y + 10);
                    ui.processEvent(MouseMoveEvent{point.x, point.y});
                    ui.processEvent(MouseScrollEvent{0, -3});
                }
            }
            ui.update(1. / 60);
            if (window)
                window->setTextInputArea(ui.textInputArea());
            view.overlay = ui.snapshot();
            const auto stats = renderer->render(snapshot, view);
            require(stats.overlayDrawCalls > 0, "UI did not render");
            if (window)
                renderer->present();
            if (!offscreen && !selfTest)
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        auto image = renderer->readback();
        image.savePng(output);
        renderer->waitIdle();
        require(renderer->debugErrorCount() == 0, "UI backend validation failed");
        if (selfTest) {
            require(gathered == 1 && changes >= 2, "UI example interaction failed");
            require(ui.element(doc, "name").value.find('!') != std::string::npos,
                    "UI example text input failed");
            require(image.pixels.size() == size_t(image.width) * image.height * 4,
                    "Invalid UI image");
        }
        for (const auto &m : ui.takeMessages())
            std::cerr << m << '\n';
        std::cout << renderer->deviceInfo().backend << " UI / " << frame << " frames / output "
                  << output << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> arguments;
    arguments.reserve(size_t(argc));
    for (int i = 0; i < argc; ++i) {
        auto value = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(value.begin(), value.end());
    }
    std::vector<char *> pointers;
    for (auto &s : arguments)
        pointers.push_back(s.data());
    return runUi(argc, pointers.data());
}
#else
int main(int argc, char **argv) {
    return runUi(argc, argv);
}
#endif
