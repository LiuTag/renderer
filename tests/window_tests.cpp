#include "internal/window.hpp"
#include "renderer/renderer.hpp"
#include <future>
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Setter> auto installed(GLFWwindow *native, Setter setter) {
    auto callback = setter(native, nullptr);
    setter(native, callback);
    require(callback != nullptr, "Native window callback is missing");
    return callback;
}
template <class T, class F> bool contains(const std::vector<WindowEvent> &events, F predicate) {
    for (const auto &event : events)
        if (const auto *typed = std::get_if<T>(&event); typed && predicate(*typed))
            return true;
    return false;
}
} // namespace
int main() {
    try {
        {
            Window window({.width = 320,
                           .height = 240,
                           .title = "Window events",
                           .visible = false,
                           .vsync = false});
            window.pollEvents();
            auto state = detail::WindowAccess::state(window);
            auto *native = state->native;
            std::vector<WindowEvent> events;
            std::vector<KeyEvent> keys;
            int closeRequests = 0;
            auto all = window.subscribe([&](const WindowEvent &event) { events.push_back(event); });
            auto keyboard =
                window.subscribe<KeyEvent>([&](const KeyEvent &event) { keys.push_back(event); });
            auto closing = window.subscribe<WindowCloseEvent>([&](const auto &) {
                ++closeRequests;
                window.cancelClose();
            });
            auto key = installed(native, glfwSetKeyCallback);
            key(native, GLFW_KEY_W, 17, GLFW_PRESS, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
            key(native, GLFW_KEY_W, 17, GLFW_REPEAT, 0);
            key(native, GLFW_KEY_W, 17, GLFW_RELEASE, 0);
            installed(native, glfwSetCharCallback)(native, 0x4E2D);
            installed(native, glfwSetCursorPosCallback)(native, 123.5, 45.25);
            installed(native, glfwSetMouseButtonCallback)(native, GLFW_MOUSE_BUTTON_RIGHT,
                                                          GLFW_PRESS, GLFW_MOD_ALT);
            auto scroll = installed(native, glfwSetScrollCallback);
            scroll(native, 0.25, -1.5);
            installed(native, glfwSetCursorEnterCallback)(native, GLFW_TRUE);
            installed(native, glfwSetWindowPosCallback)(native, 50, 60);
            installed(native, glfwSetWindowSizeCallback)(native, 333, 222);
            installed(native, glfwSetFramebufferSizeCallback)(native, 0, 0);
            require(state->framebufferWidth == 0 && state->framebufferHeight == 0,
                    "Framebuffer cache was not updated before event dispatch");
            installed(native, glfwSetWindowFocusCallback)(native, GLFW_FALSE);
            installed(native, glfwSetWindowIconifyCallback)(native, GLFW_TRUE);
            installed(native, glfwSetWindowMaximizeCallback)(native, GLFW_TRUE);
            installed(native, glfwSetWindowContentScaleCallback)(native, 1.25f, 1.5f);
            installed(native, glfwSetWindowRefreshCallback)(native);
            std::string path = "assets/中文.obj";
            const char *paths[] = {path.c_str()};
            installed(native, glfwSetDropCallback)(native, 1, paths);
            path = "changed.obj";
            glfwSetWindowShouldClose(native, GLFW_TRUE); // 模拟原生关闭请求已设置的标记。
            installed(native, glfwSetWindowCloseCallback)(native);
            require(events.empty() && keys.empty() && closeRequests == 0,
                    "Application callbacks ran inside a native callback");
            window.pollEvents();
            require(keys.size() == 3 && keys[0].key == Key::W && keys[0].scanCode == 17 &&
                        hasModifier(keys[0].modifiers, Modifiers::Control | Modifiers::Shift) &&
                        keys[1].action == InputAction::Repeat &&
                        keys[2].action == InputAction::Release,
                    "Typed keyboard subscription or action/modifier mapping failed");
            require(contains<TextInputEvent>(events,
                                             [](const auto &e) { return e.codepoint == U'中'; }) &&
                        contains<MouseMoveEvent>(
                            events, [](const auto &e) { return e.x == 123.5 && e.y == 45.25; }) &&
                        contains<MouseButtonEvent>(events,
                                                   [](const auto &e) {
                                                       return e.button == MouseButton::Right &&
                                                              e.action == InputAction::Press &&
                                                              hasModifier(e.modifiers,
                                                                          Modifiers::Alt);
                                                   }) &&
                        contains<MouseScrollEvent>(
                            events,
                            [](const auto &e) { return e.xOffset == 0.25 && e.yOffset == -1.5; }) &&
                        contains<CursorEnterEvent>(events, [](const auto &e) { return e.entered; }),
                    "Text or mouse event mapping failed");
            require(
                contains<WindowMoveEvent>(events,
                                          [](const auto &e) { return e.x == 50 && e.y == 60; }) &&
                    contains<WindowResizeEvent>(
                        events, [](const auto &e) { return e.width == 333 && e.height == 222; }) &&
                    contains<FramebufferResizeEvent>(
                        events, [](const auto &e) { return e.width == 0 && e.height == 0; }) &&
                    contains<WindowFocusEvent>(events, [](const auto &e) { return !e.focused; }) &&
                    contains<WindowMinimizeEvent>(events,
                                                  [](const auto &e) { return e.minimized; }) &&
                    contains<WindowMaximizeEvent>(events,
                                                  [](const auto &e) { return e.maximized; }) &&
                    contains<WindowContentScaleEvent>(
                        events,
                        [](const auto &e) { return e.xScale == 1.25f && e.yScale == 1.5f; }) &&
                    contains<WindowRefreshEvent>(events, [](const auto &) { return true; }) &&
                    contains<FileDropEvent>(events,
                                            [](const auto &e) {
                                                return e.paths.size() == 1 &&
                                                       e.paths[0] == std::filesystem::path(
                                                                         u8"assets/中文.obj");
                                            }),
                "Window state/file-drop event mapping failed");
            require(closeRequests == 1 && !window.shouldClose(),
                    "Native close could not be cancelled");
            window.requestClose();
            window.requestClose();
            require(closeRequests == 1 && window.shouldClose(),
                    "Programmatic close was not deferred/idempotent");
            window.pollEvents();
            require(closeRequests == 2 && !window.shouldClose(),
                    "Programmatic close notification missing");
            key(native, GLFW_KEY_ESCAPE, 1, GLFW_PRESS, 0);
            key(native, GLFW_KEY_ESCAPE, 1, GLFW_REPEAT, 0);
            window.pollEvents();
            require(closeRequests == 3 && !window.shouldClose(),
                    "Escape close cancellation failed");
            key(native, GLFW_KEY_ESCAPE, 1, GLFW_REPEAT, 0);
            window.pollEvents();
            require(closeRequests == 3, "Held Escape repeatedly requested a cancelled close");

            events.clear();
            window.resize(420, 280);
            for (int i = 0; i < 4; ++i)
                window.pollEvents();
            const auto size = window.framebufferSize();
            require(contains<FramebufferResizeEvent>(events,
                                                     [&](const auto &e) {
                                                         return e.width == size.first &&
                                                                e.height == size.second;
                                                     }),
                    "Actual GLFW resize did not reach public subscribers");

            int observed = 0;
            auto throwing = window.subscribe<MouseScrollEvent>(
                [](const auto &) { throw std::runtime_error("listener"); });
            auto observer = window.subscribe<MouseScrollEvent>([&](const auto &) { ++observed; });
            scroll(native, 0, 1); // 这里不得抛异常跨越原生回调边界。
            bool caught = false;
            try {
                window.pollEvents();
            } catch (const std::runtime_error &) {
                caught = true;
            }
            require(caught && observed == 1,
                    "Callback exception escaped natively or stopped other subscribers");
            throwing.disconnect();
            auto wrongThread = std::async(std::launch::async, [&] {
                try {
                    auto invalid = window.subscribe([](const WindowEvent &) {});
                } catch (const std::logic_error &) {
                    return true;
                }
                return false;
            });
            require(wrongThread.get(), "Window subscription accepted a non-owner thread");

            closing.disconnect();
            all.disconnect();
            const auto before = keys.size();
            Window moved = std::move(window);
            key(native, GLFW_KEY_A, 30, GLFW_PRESS, 0);
            moved.pollEvents();
            require(keyboard.connected() && keys.size() == before + 1 && keys.back().key == Key::A,
                    "Window move invalidated existing subscriptions");
            keyboard.disconnect();
            key(native, GLFW_KEY_A, 30, GLFW_RELEASE, 0);
            moved.pollEvents();
            require(keys.size() == before + 1, "Disconnected keyboard listener was invoked");
        }
        {
            auto window = std::make_unique<Window>(
                WindowConfig{.width = 320, .height = 240, .visible = false, .vsync = false});
            auto renderer = Renderer::createOpenGL(*window, {.renderThread = true});
            auto state = detail::WindowAccess::state(*window);
            auto key = installed(state->native, glfwSetKeyCallback);
            int delivered = 0;
            auto subscription = window->subscribe<KeyEvent>([&](const auto &) { ++delivered; });
            window.reset();
            require(!subscription.connected(),
                    "Renderer kept application event subscriptions alive");
            key(state->native, GLFW_KEY_A, 0, GLFW_PRESS, 0);
            require(delivered == 0, "Destroyed Window still received events");
            renderer.waitIdle();
            require(renderer.debugErrorCount() == 0, "Event teardown damaged the render context");
        }
        {
            auto window = std::make_unique<Window>(
                WindowConfig{.width = 320, .height = 240, .visible = false, .vsync = false});
            window->pollEvents();
            int remaining = 0;
            auto close = window->subscribe<MouseScrollEvent>([&](const auto &) { window.reset(); });
            auto after = window->subscribe<MouseScrollEvent>([&](const auto &) { ++remaining; });
            auto state = detail::WindowAccess::state(*window);
            installed(state->native, glfwSetScrollCallback)(state->native, 0, 1);
            state.reset();
            window->pollEvents();
            require(!window && !close.connected() && !after.connected() && remaining == 0,
                    "Window destruction inside a callback kept later listeners active");
        }
        std::cout << "PASS native event adapters, typed subscriptions, resize, cancellation, "
                     "exceptions, moves, thread ownership and context lifetime\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "WINDOW EVENT TEST FAILED: " << error.what() << '\n';
        return 1;
    }
}
