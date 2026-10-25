#include "renderer/window.hpp"
#include "../internal/window.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace renderer {
namespace {
std::weak_ptr<detail::GlfwLifetime> glfwLifetime;
std::string glfwError(const char *operation) {
    const char *description = nullptr;
    glfwGetError(&description);
    return std::string(operation) + ": " + (description ? description : "unknown GLFW error");
}
detail::WindowState &windowState(GLFWwindow *native) noexcept {
    return *static_cast<detail::WindowState *>(glfwGetWindowUserPointer(native));
}
void requestWindowClose(detail::WindowState &state) noexcept {
    if (!glfwWindowShouldClose(state.native)) {
        glfwSetWindowShouldClose(state.native, GLFW_TRUE);
        state.postEvent(WindowCloseEvent{});
    }
}
void checkGlfw(const char *operation) {
    const char *description = nullptr;
    if (glfwGetError(&description) != GLFW_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": " +
                                 (description ? description : "unknown GLFW error"));
}
void validateMode(WindowMode mode, const FullscreenConfig &config) {
    if (mode != WindowMode::Windowed && mode != WindowMode::BorderlessFullscreen &&
        mode != WindowMode::ExclusiveFullscreen)
        throw std::invalid_argument("Invalid window mode");
    if (mode == WindowMode::Windowed)
        return;
    if (config.monitorIndex < -1 || config.width < 0 || config.height < 0 ||
        (config.width == 0) != (config.height == 0) || config.refreshRate < 0)
        throw std::invalid_argument("Invalid fullscreen configuration");
    if (mode == WindowMode::BorderlessFullscreen &&
        (config.width || config.height || config.refreshRate))
        throw std::invalid_argument("Borderless fullscreen uses the desktop video mode");
}
bool monitorAvailable(GLFWmonitor *monitor) {
    int count = 0;
    auto **monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i)
        if (monitors[i] == monitor)
            return true;
    return false;
}
GLFWmonitor *selectMonitor(const detail::WindowState &state, int index) {
    int count = 0;
    auto **monitors = glfwGetMonitors(&count);
    if (!count || !monitors)
        throw std::runtime_error("No display is available for fullscreen");
    if (index >= count)
        throw std::invalid_argument("Fullscreen monitor index is out of range");
    if (index >= 0)
        return monitors[index];
    if (monitorAvailable(state.fullscreenMonitor))
        return state.fullscreenMonitor;
    // 选择与客户区重叠面积最大的显示器，完全离屏时回退主显示器。
    int x = 0, y = 0, width = 0, height = 0;
    glfwGetWindowPos(state.native, &x, &y);
    glfwGetWindowSize(state.native, &width, &height);
    auto *selected = glfwGetPrimaryMonitor();
    int64_t largest = 0;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const auto *video = glfwGetVideoMode(monitors[i]);
        if (!video)
            continue;
        const auto overlapX =
            std::max<int64_t>(0, std::min<int64_t>(int64_t{x} + width, int64_t{mx} + video->width) -
                                     std::max<int64_t>(x, mx));
        const auto overlapY = std::max<int64_t>(
            0, std::min<int64_t>(int64_t{y} + height, int64_t{my} + video->height) -
                   std::max<int64_t>(y, my));
        if (overlapX * overlapY > largest) {
            largest = overlapX * overlapY;
            selected = monitors[i];
        }
    }
    return selected ? selected : monitors[0];
}
void rememberWindowedPlacement(detail::WindowState &state) {
    if (state.mode != WindowMode::Windowed || state.changingMode)
        return;
    const bool maximized = glfwGetWindowAttrib(state.native, GLFW_MAXIMIZED) == GLFW_TRUE;
    state.windowed.maximized = maximized;
    if (maximized || glfwGetWindowAttrib(state.native, GLFW_ICONIFIED))
        return;
    glfwGetWindowPos(state.native, &state.windowed.x, &state.windowed.y);
    glfwGetWindowSize(state.native, &state.windowed.width, &state.windowed.height);
}
void updateFramebufferSize(detail::WindowState &state) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(state.native, &width, &height);
    state.framebufferWidth = width;
    state.framebufferHeight = height;
}
void applyMode(detail::WindowState &state, WindowMode target, GLFWmonitor *monitor,
               const GLFWvidmode &desktop, const FullscreenConfig &config) {
    // 不绑定/解绑上下文，Renderer 的专用线程仍然拥有同一个上下文。
    if (glfwGetWindowAttrib(state.native, GLFW_ICONIFIED) ||
        glfwGetWindowAttrib(state.native, GLFW_MAXIMIZED))
        glfwRestoreWindow(state.native);
    checkGlfw("Restore window before display mode change");
    if (target != WindowMode::ExclusiveFullscreen && glfwGetWindowMonitor(state.native)) {
        glfwSetWindowMonitor(state.native, nullptr, state.windowed.x, state.windowed.y,
                             state.windowed.width, state.windowed.height, GLFW_DONT_CARE);
        checkGlfw("Leave exclusive fullscreen");
    }
    glfwSetWindowAttrib(state.native, GLFW_DECORATED,
                        target == WindowMode::BorderlessFullscreen ? GLFW_FALSE
                                                                   : state.windowed.decorated);
    glfwSetWindowAttrib(state.native, GLFW_RESIZABLE,
                        target == WindowMode::BorderlessFullscreen ? GLFW_FALSE
                                                                   : state.windowed.resizable);
    checkGlfw("Configure window frame");
    if (target == WindowMode::Windowed) {
        glfwSetWindowMonitor(state.native, nullptr, state.windowed.x, state.windowed.y,
                             state.windowed.width, state.windowed.height, GLFW_DONT_CARE);
        checkGlfw("Restore windowed placement");
        if (state.windowed.maximized)
            glfwMaximizeWindow(state.native);
    } else if (target == WindowMode::BorderlessFullscreen) {
        int x = 0, y = 0;
        glfwGetMonitorPos(monitor, &x, &y);
        // 使用整个显示器范围而不是工作区；不占用显示器、不改变桌面视频模式。
        glfwSetWindowMonitor(state.native, nullptr, x, y, desktop.width, desktop.height,
                             GLFW_DONT_CARE);
    } else {
        glfwSetWindowMonitor(state.native, monitor, 0, 0,
                             config.width ? config.width : desktop.width,
                             config.height ? config.height : desktop.height,
                             config.refreshRate ? config.refreshRate : desktop.refreshRate);
    }
    checkGlfw("Change window display mode");
}
void changeMode(detail::WindowState &state, WindowMode target, const FullscreenConfig &config) {
    state.checkThread();
    validateMode(target, config);
    if (target == WindowMode::ExclusiveFullscreen &&
        !glfwGetWindowAttrib(state.native, GLFW_VISIBLE))
        throw std::logic_error("A hidden window cannot enter exclusive fullscreen");
    glfwGetError(nullptr); // 仅把本次窗口操作的 GLFW 错误作为切换失败。
    GLFWmonitor *monitor = nullptr;
    GLFWvidmode desktop{};
    if (target != WindowMode::Windowed) {
        monitor = selectMonitor(state, config.monitorIndex);
        if (monitor == state.fullscreenMonitor && monitorAvailable(monitor))
            desktop = state.desktopMode;
        else {
            const auto *video = glfwGetVideoMode(monitor);
            if (!video)
                throw std::runtime_error(glfwError("Query fullscreen desktop video mode"));
            desktop = *video;
        }
    }
    checkGlfw("Select fullscreen monitor");
    if (target == state.mode &&
        (target == WindowMode::Windowed ||
         (monitor == state.fullscreenMonitor && config.width == state.fullscreen.width &&
          config.height == state.fullscreen.height &&
          config.refreshRate == state.fullscreen.refreshRate)))
        return;
    rememberWindowedPlacement(state);
    if (state.mode == WindowMode::Windowed) {
        state.windowed.decorated = glfwGetWindowAttrib(state.native, GLFW_DECORATED) != 0;
        state.windowed.resizable = glfwGetWindowAttrib(state.native, GLFW_RESIZABLE) != 0;
    }
    const auto previousMode = state.mode;
    const auto previousConfig = state.fullscreen;
    auto *previousMonitor = state.fullscreenMonitor;
    const auto previousDesktop = state.desktopMode;
    state.changingMode = true;
    try {
        applyMode(state, target, monitor, desktop, config);
        state.mode = target;
        state.fullscreen = config;
        state.fullscreenMonitor = monitor;
        state.desktopMode = desktop;
    } catch (...) {
        const auto failure = std::current_exception();
        // 平台操作可能部分成功；尽量恢复原模式，显示器已断开时恢复普通窗口。
        const bool restoreFullscreen =
            previousMode != WindowMode::Windowed && monitorAvailable(previousMonitor);
        const auto fallback = restoreFullscreen ? previousMode : WindowMode::Windowed;
        try {
            glfwGetError(nullptr);
            applyMode(state, fallback, previousMonitor, previousDesktop, previousConfig);
            state.mode = fallback;
            if (!restoreFullscreen)
                state.fullscreenMonitor = nullptr;
        } catch (...) {
            state.mode = glfwGetWindowMonitor(state.native)
                             ? WindowMode::ExclusiveFullscreen
                             : (glfwGetWindowAttrib(state.native, GLFW_DECORATED)
                                    ? WindowMode::Windowed
                                    : WindowMode::BorderlessFullscreen);
        }
        state.changingMode = false;
        updateFramebufferSize(state);
        std::rethrow_exception(failure);
    }
    state.changingMode = false;
    updateFramebufferSize(state);
}
void installCallbacks(GLFWwindow *window) {
    // 数值只在此平台适配层转换，公共头文件不依赖 GLFW。
    static_assert(static_cast<int>(Key::Escape) == GLFW_KEY_ESCAPE &&
                  static_cast<int>(Key::Menu) == GLFW_KEY_MENU &&
                  static_cast<int>(Key::A) == GLFW_KEY_A &&
                  static_cast<int>(Key::F25) == GLFW_KEY_F25 &&
                  static_cast<int>(MouseButton::Left) == GLFW_MOUSE_BUTTON_LEFT &&
                  static_cast<int>(InputAction::Repeat) == GLFW_REPEAT);
    glfwSetKeyCallback(window,
                       [](GLFWwindow *native, int key, int scan, int action, int mods) noexcept {
                           windowState(native).postEvent(KeyEvent{static_cast<Key>(key), scan,
                                                                  static_cast<InputAction>(action),
                                                                  static_cast<Modifiers>(mods)});
                           // 兼容 Esc 默认关闭，取消后不因按键仍被按住而每帧重试。
                           if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                               requestWindowClose(windowState(native));
                       });
    glfwSetCharCallback(window, [](GLFWwindow *native, unsigned int codepoint) noexcept {
        windowState(native).postEvent(TextInputEvent{static_cast<char32_t>(codepoint)});
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow *native, double x, double y) noexcept {
        windowState(native).postEvent(MouseMoveEvent{x, y});
    });
    glfwSetMouseButtonCallback(
        window, [](GLFWwindow *native, int button, int action, int mods) noexcept {
            double x = 0, y = 0;
            glfwGetCursorPos(native, &x, &y);
            windowState(native).postEvent(MouseButtonEvent{static_cast<MouseButton>(button),
                                                           static_cast<InputAction>(action),
                                                           static_cast<Modifiers>(mods), x, y});
        });
    glfwSetScrollCallback(window, [](GLFWwindow *native, double x, double y) noexcept {
        windowState(native).postEvent(MouseScrollEvent{x, y});
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow *native, int entered) noexcept {
        windowState(native).postEvent(CursorEnterEvent{entered == GLFW_TRUE});
    });
    glfwSetWindowPosCallback(window, [](GLFWwindow *native, int x, int y) noexcept {
        auto &state = windowState(native);
        rememberWindowedPlacement(state);
        state.postEvent(WindowMoveEvent{x, y});
    });
    glfwSetWindowSizeCallback(window, [](GLFWwindow *native, int width, int height) noexcept {
        auto &state = windowState(native);
        rememberWindowedPlacement(state);
        state.postEvent(WindowResizeEvent{width, height});
    });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *native, int width, int height) noexcept {
        auto &state = windowState(native);
        // 内部尺寸缓存立即更新；公开通知延迟到主线程 pollEvents 分派。
        state.framebufferWidth = width;
        state.framebufferHeight = height;
        state.postEvent(FramebufferResizeEvent{width, height});
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow *native, int focused) noexcept {
        windowState(native).postEvent(WindowFocusEvent{focused == GLFW_TRUE});
    });
    glfwSetWindowIconifyCallback(window, [](GLFWwindow *native, int minimized) noexcept {
        windowState(native).postEvent(WindowMinimizeEvent{minimized == GLFW_TRUE});
    });
    glfwSetWindowMaximizeCallback(window, [](GLFWwindow *native, int maximized) noexcept {
        auto &state = windowState(native);
        rememberWindowedPlacement(state);
        state.postEvent(WindowMaximizeEvent{maximized == GLFW_TRUE});
    });
    glfwSetWindowContentScaleCallback(window, [](GLFWwindow *native, float x, float y) noexcept {
        windowState(native).postEvent(WindowContentScaleEvent{x, y});
    });
    glfwSetWindowCloseCallback(window, [](GLFWwindow *native) noexcept {
        windowState(native).postEvent(WindowCloseEvent{});
    });
    glfwSetWindowRefreshCallback(window, [](GLFWwindow *native) noexcept {
        windowState(native).postEvent(WindowRefreshEvent{});
    });
    glfwSetDropCallback(window, [](GLFWwindow *native, int count, const char **paths) noexcept {
        // 原生路径只在回调期间有效；复制并完成 UTF-8 到平台路径的转换。
        try {
            FileDropEvent event;
            event.paths.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const std::string_view utf8(paths[i]);
                event.paths.emplace_back(std::u8string(utf8.begin(), utf8.end()));
            }
            windowState(native).postEvent(std::move(event));
        } catch (...) {
            windowState(native).eventError(std::current_exception());
        }
    });
}
} // namespace
detail::GlfwLifetime::GlfwLifetime() {
    if (!glfwInit())
        throw std::runtime_error(glfwError("GLFW initialization failed"));
}
detail::GlfwLifetime::~GlfwLifetime() {
    glfwTerminate();
}
detail::WindowState::~WindowState() {
    if (native) {
        removeTextInputHook(*this);
        glfwDestroyWindow(native);
    }
}
void detail::WindowState::checkThread() const {
    if (std::this_thread::get_id() != lifetime->thread)
        throw std::logic_error("Window calls must use the creating thread");
}
void detail::WindowState::makeCurrent() const {
    checkThread();
    if (graphicsApi != GraphicsApi::OpenGL)
        throw std::logic_error("Vulkan windows have no OpenGL context");
    glfwMakeContextCurrent(native);
}
struct Window::Impl {
    std::shared_ptr<detail::WindowState> state;
    std::shared_ptr<detail::EventDispatcher> events = std::make_shared<detail::EventDispatcher>();
    ~Impl() {
        events->close();
    } // Renderer 可保留原生窗口，但不能保留应用回调。
};
Window::Window(const WindowConfig &c) : impl_(std::make_unique<Impl>()) {
    if (c.width <= 0 || c.height <= 0)
        throw std::invalid_argument("Window dimensions must be positive");
    if (c.graphicsApi != GraphicsApi::OpenGL && c.graphicsApi != GraphicsApi::Vulkan)
        throw std::invalid_argument("Invalid window graphics API");
    validateMode(c.mode, c.fullscreen);
    if (c.mode == WindowMode::ExclusiveFullscreen && !c.visible)
        throw std::invalid_argument("Exclusive fullscreen requires a visible window");
    auto lifetime = glfwLifetime.lock();
    if (!lifetime) {
        lifetime = std::make_shared<detail::GlfwLifetime>();
        glfwLifetime = lifetime;
    }
    if (lifetime->thread != std::this_thread::get_id())
        throw std::logic_error("All windows must use the application main thread");
    impl_->state = std::make_shared<detail::WindowState>();
    impl_->state->lifetime = std::move(lifetime);
    impl_->state->events = impl_->events;
    impl_->state->graphicsApi = c.graphicsApi;
    impl_->state->vsync = c.vsync;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, c.debugContext ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, c.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API,
                   c.graphicsApi == GraphicsApi::OpenGL ? GLFW_OPENGL_API : GLFW_NO_API);
    impl_->state->native = glfwCreateWindow(c.width, c.height, c.title.c_str(), nullptr, nullptr);
    if (!impl_->state->native)
        throw std::runtime_error(glfwError("Graphics window creation failed"));
    glfwSetWindowUserPointer(impl_->state->native, impl_->state.get());
    installCallbacks(impl_->state->native);
    detail::installTextInputHook(*impl_->state);
    glfwSetInputMode(impl_->state->native, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    rememberWindowedPlacement(*impl_->state);
    if (c.mode != WindowMode::Windowed)
        setMode(c.mode, c.fullscreen);
    framebufferSize();
    if (c.graphicsApi == GraphicsApi::OpenGL) {
        impl_->state->makeCurrent();
        glfwSwapInterval(c.vsync ? 1 : 0);
    }
}
void Window::setTextInputArea(const TextInputArea &area) {
    auto &s = *impl_->state;
    s.checkThread();
    for (float v : {area.x, area.y, area.width, area.height})
        if (!std::isfinite(v) || std::abs(v) > 10000000)
            throw std::invalid_argument("Invalid text input area");
    if (area.width < 0 || area.height < 0)
        throw std::invalid_argument("Negative text input area");
    const auto &old = s.textInputArea;
    if (old.active == area.active && old.x == area.x && old.y == area.y &&
        old.width == area.width && old.height == area.height)
        return;
    s.textInputArea = area;
    detail::updateTextInputArea(s);
}
std::string Window::clipboardText() const {
    impl_->state->checkThread();
    const char *value = glfwGetClipboardString(impl_->state->native);
    if (!value) {
        glfwGetError(nullptr);
        return {};
    }
    return value;
}
void Window::setClipboardText(const std::string &text) {
    impl_->state->checkThread();
    if (text.find('\0') != std::string::npos)
        throw std::invalid_argument("Clipboard text contains NUL");
    glfwGetError(nullptr);
    glfwSetClipboardString(impl_->state->native, text.c_str());
    checkGlfw("Set clipboard text");
}
Window::~Window() = default;
Window::Window(Window &&) noexcept = default;
Window &Window::operator=(Window &&) noexcept = default;
void Window::pollEvents() {
    // 分派期间允许应用销毁 Window 包装对象，局部共享引用保持原生状态有效。
    const auto state = impl_->state;
    const auto events = impl_->events;
    state->checkThread();
    if (events->dispatching)
        throw std::logic_error("Window::pollEvents cannot be called recursively");
    glfwPollEvents();
    if (state->mode != WindowMode::Windowed &&
        (!monitorAvailable(state->fullscreenMonitor) ||
         (state->mode == WindowMode::ExclusiveFullscreen && !glfwGetWindowMonitor(state->native))))
        setMode(WindowMode::Windowed);
    events->dispatch();
}
EventSubscription Window::subscribe(WindowEventHandler handler) {
    impl_->state->checkThread();
    return detail::EventAccess::subscribe(*impl_->events, std::move(handler));
}
bool Window::shouldClose() const {
    impl_->state->checkThread();
    return glfwWindowShouldClose(impl_->state->native) != 0;
}
void Window::requestClose() {
    impl_->state->checkThread();
    requestWindowClose(*impl_->state);
}
void Window::cancelClose() {
    impl_->state->checkThread();
    glfwSetWindowShouldClose(impl_->state->native, GLFW_FALSE);
}
void Window::present() {
    if (impl_->state->rendererThreaded)
        throw std::logic_error("Use Renderer::present() with a dedicated render thread");
    if (impl_->state->graphicsApi == GraphicsApi::Vulkan) {
        impl_->state->checkThread();
        if (!impl_->state->presentCallback)
            throw std::logic_error("Create a Vulkan device before presenting");
        impl_->state->presentCallback();
        return;
    }
    impl_->state->makeCurrent();
    glfwSwapBuffers(impl_->state->native);
}
WindowMode Window::mode() const {
    impl_->state->checkThread();
    return impl_->state->mode;
}
void Window::setMode(WindowMode mode, const FullscreenConfig &fullscreen) {
    changeMode(*impl_->state, mode, fullscreen);
    framebufferSize();
}
void Window::setTitle(const std::string &title) {
    impl_->state->checkThread();
    glfwSetWindowTitle(impl_->state->native, title.c_str());
}
std::pair<int, int> Window::framebufferSize() const {
    impl_->state->checkThread();
    int width = 0, height = 0;
    glfwGetFramebufferSize(impl_->state->native, &width, &height);
    impl_->state->framebufferWidth = width;
    impl_->state->framebufferHeight = height;
    return {width, height};
}
std::pair<int, int> Window::clientSize() const {
    impl_->state->checkThread();
    int width = 0, height = 0;
    glfwGetWindowSize(impl_->state->native, &width, &height);
    return {width, height};
}
bool Window::spacePressed() const {
    impl_->state->checkThread();
    return glfwGetKey(impl_->state->native, GLFW_KEY_SPACE) == GLFW_PRESS;
}
void Window::resize(int width, int height) {
    impl_->state->checkThread();
    if (impl_->state->mode != WindowMode::Windowed)
        throw std::logic_error("Use Window::setMode to change fullscreen resolution");
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Window dimensions must be positive");
    glfwSetWindowSize(impl_->state->native, width, height);
    framebufferSize();
}
std::shared_ptr<detail::WindowState> detail::WindowAccess::state(Window &window) {
    return window.impl_->state;
}
} // namespace renderer
