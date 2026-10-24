#include "internal/window.hpp"
#include "renderer/renderer.hpp"
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Exception, class F> void rejects(F function, const char *message) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error(message);
}
std::array<int, 4> placement(GLFWwindow *window) {
    std::array<int, 4> result{};
    glfwGetWindowPos(window, &result[0], &result[1]);
    glfwGetWindowSize(window, &result[2], &result[3]);
    return result;
}
} // namespace
int main() {
    try {
        rejects<std::invalid_argument>([] { Window window({.mode = static_cast<WindowMode>(99)}); },
                                       "Invalid initial window mode was accepted");
        rejects<std::invalid_argument>(
            [] { Window window({.visible = false, .mode = WindowMode::ExclusiveFullscreen}); },
            "Hidden exclusive fullscreen creation was accepted");
        {
            Window window({.width = 420,
                           .height = 260,
                           .visible = false,
                           .vsync = false,
                           .mode = WindowMode::BorderlessFullscreen});
            auto state = detail::WindowAccess::state(window);
            require(window.mode() == WindowMode::BorderlessFullscreen &&
                        !glfwGetWindowMonitor(state->native) &&
                        !glfwGetWindowAttrib(state->native, GLFW_DECORATED) &&
                        !glfwGetWindowAttrib(state->native, GLFW_VISIBLE),
                    "Initial borderless mode did not preserve hidden visibility");
            rejects<std::logic_error>([&] { window.setMode(WindowMode::ExclusiveFullscreen); },
                                      "Hidden window entered exclusive fullscreen");
            window.setMode(WindowMode::Windowed);
            auto rect = placement(state->native);
            require(rect[2] == 420 && rect[3] == 260 &&
                        glfwGetWindowAttrib(state->native, GLFW_DECORATED),
                    "Initial fullscreen did not restore configured windowed size");
        }
        {
            Window window(
                {.width = 512, .height = 320, .title = "Fullscreen tests", .vsync = false});
            window.pollEvents();
            auto state = detail::WindowAccess::state(window);
            auto *native = state->native;
            glfwSetWindowPos(native, 80, 90);
            window.pollEvents();
            const auto original = placement(native);
            const auto desktop = *glfwGetVideoMode(glfwGetPrimaryMonitor());
            const auto checkRestored = [&] {
                window.pollEvents();
                require(window.mode() == WindowMode::Windowed && !glfwGetWindowMonitor(native) &&
                            placement(native) == original &&
                            glfwGetWindowAttrib(native, GLFW_DECORATED) &&
                            glfwGetWindowAttrib(native, GLFW_RESIZABLE),
                        "Windowed position, size or frame was not restored");
            };
            auto renderer = Renderer::createOpenGL(window, {.renderThread = true});
            AssetStore assets;
            Scene scene;
            const auto camera = scene.createNode(scene.root());
            scene.setComponent(camera, Camera{});
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 3}));
            const auto object = scene.createNode(scene.root());
            scene.setComponent(object, MeshRenderer{assets.createMesh(geometry::cube()),
                                                    {assets.createMaterial({})}});
            RenderExtractor extractor;
            const auto snapshot = extractor.extract(scene, assets);
            const RenderView view{.cameraNode = camera, .shadows = false, .shadowResolution = 64};
            const auto draw = [&] {
                const auto stats = renderer.render(snapshot, view);
                renderer.present();
                const auto [width, height] = window.framebufferSize();
                const auto image = renderer.readback();
                require(stats.visibleInstances == 1 && image.width == uint32_t(width) &&
                            image.height == uint32_t(height) && renderer.debugErrorCount() == 0,
                        "Fullscreen switch damaged rendering or framebuffer sizing");
            };
            int resized = 0;
            auto subscription =
                window.subscribe<FramebufferResizeEvent>([&](const auto &) { ++resized; });
            const auto before = resized;
            auto pending = renderer.submit(snapshot, view);
            window.setMode(WindowMode::BorderlessFullscreen);
            pending.get();
            require(resized == before, "Mode switch dispatched application events immediately");
            require(window.mode() == WindowMode::BorderlessFullscreen &&
                        !glfwGetWindowMonitor(native) &&
                        !glfwGetWindowAttrib(native, GLFW_DECORATED),
                    "Borderless fullscreen attached a monitor or kept decorations");
            const auto borderless = placement(native);
            require(borderless[2] == desktop.width && borderless[3] == desktop.height,
                    "Borderless mode did not cover desktop resolution");
            window.pollEvents();
            require(resized > before, "Fullscreen framebuffer events were not delivered");
            draw();
            window.setMode(WindowMode::BorderlessFullscreen); // 相同配置不重复修改窗口。
            require(placement(native) == borderless, "Repeated borderless switch changed size");
            rejects<std::logic_error>([&] { window.resize(200, 200); },
                                      "Fullscreen resize was accepted");
            rejects<std::invalid_argument>([&] { window.setMode(static_cast<WindowMode>(99)); },
                                           "Invalid runtime window mode was accepted");
            rejects<std::invalid_argument>(
                [&] { window.setMode(WindowMode::ExclusiveFullscreen, {.width = 800}); },
                "Incomplete fullscreen resolution was accepted");
            rejects<std::invalid_argument>(
                [&] { window.setMode(WindowMode::ExclusiveFullscreen, {.monitorIndex = 99999}); },
                "Invalid fullscreen monitor index was accepted");
            rejects<std::invalid_argument>(
                [&] { window.setMode(WindowMode::BorderlessFullscreen, {.refreshRate = 60}); },
                "Borderless refresh-rate override was accepted");
            require(window.mode() == WindowMode::BorderlessFullscreen,
                    "Rejected options altered the active mode");

            // 同一窗口跨两种全屏模式，不能用全屏尺寸覆盖之前保存的窗口尺寸。
            window.setMode(WindowMode::ExclusiveFullscreen, {.width = desktop.width,
                                                             .height = desktop.height,
                                                             .refreshRate = desktop.refreshRate});
            require(window.mode() == WindowMode::ExclusiveFullscreen &&
                        glfwGetWindowMonitor(native),
                    "Exclusive fullscreen did not attach the display");
            window.pollEvents();
            draw();
            window.setMode(WindowMode::BorderlessFullscreen);
            window.pollEvents();
            draw();
            window.setMode(WindowMode::Windowed);
            checkRestored();
            draw();

            auto ownerCheck = std::async(std::launch::async, [&] {
                rejects<std::logic_error>([&] { window.setMode(WindowMode::Windowed); },
                                          "Display switch accepted a non-owner thread");
                rejects<std::logic_error>([&] { window.mode(); },
                                          "Mode query accepted a non-owner thread");
            });
            ownerCheck.get();
            // 最大化前的正常矩形与最大化状态都应保留。
            glfwMaximizeWindow(native);
            window.pollEvents();
            require(glfwGetWindowAttrib(native, GLFW_MAXIMIZED), "Window did not maximize");
            window.setMode(WindowMode::BorderlessFullscreen);
            window.setMode(WindowMode::Windowed);
            window.pollEvents();
            require(glfwGetWindowAttrib(native, GLFW_MAXIMIZED),
                    "Fullscreen exit did not restore maximized state");
            glfwRestoreWindow(native);
            checkRestored();
            // 模拟平台在显示器断开时强制解除关联；pollEvents 负责恢复窗口状态。
            window.setMode(WindowMode::ExclusiveFullscreen);
            glfwSetWindowMonitor(native, nullptr, 0, 0, 400, 300, GLFW_DONT_CARE);
            checkRestored();
            draw();
            renderer.waitIdle();
        }
        {
            Window window({.width = 420,
                           .height = 260,
                           .vsync = false,
                           .mode = WindowMode::ExclusiveFullscreen});
            require(window.mode() == WindowMode::ExclusiveFullscreen &&
                        glfwGetWindowMonitor(detail::WindowAccess::state(window)->native),
                    "Initial exclusive fullscreen did not attach a display");
            Window moved = std::move(window);
            moved.setMode(WindowMode::Windowed);
            const auto rect = placement(detail::WindowAccess::state(moved)->native);
            require(moved.mode() == WindowMode::Windowed && rect[2] == 420 && rect[3] == 260,
                    "Window move lost fullscreen restoration state");
        }
        std::cout << "PASS fullscreen creation, transitions, placement, maximize, validation, "
                     "thread ownership, mode recovery and threaded rendering\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FULLSCREEN TEST FAILED: " << error.what() << '\n';
        return 1;
    }
}
