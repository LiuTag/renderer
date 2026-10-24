/**
 * @file
 * @brief 定义 GLFW 运行环境、原生窗口共享状态和事件桥接，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_WINDOW_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_WINDOW_HPP_INCLUDED
#include "events.hpp"
#include "renderer/window.hpp"
#include <GLFW/glfw3.h>
#include <atomic>
#include <thread>

namespace renderer::detail {
/**
 * @brief 对 GLFW 进程运行环境进行引用计数并记录创建线程的生命周期守卫。
 */
struct GlfwLifetime {
    std::thread::id thread = std::this_thread::get_id();
    /**
     * @brief 初始化或引用 GLFW 进程级运行环境并记录应用线程。
     * @return 无（构造函数）。
     */
    GlfwLifetime();
    /**
     * @brief 释放 GLFW 引用，在最后一个实例退出时终止运行环境。
     * @return 无（析构函数）。
     */
    ~GlfwLifetime();
};
/** @brief 切换全屏前保存的普通窗口客户区位置、尺寸、边框和最大化状态。 */
struct WindowPlacement {
    int x = 0, y = 0, width = 1280, height = 800;
    bool maximized = false, decorated = true, resizable = true;
};
/**
 * @brief 共享原生窗口、上下文占用、事件弱引用及跨线程尺寸缓存的内部状态。
 */
struct WindowState {
    std::shared_ptr<GlfwLifetime> lifetime;
    std::weak_ptr<EventDispatcher> events;
    GLFWwindow *native = nullptr;
    bool rendererAttached = false;
    GraphicsApi graphicsApi = GraphicsApi::OpenGL;
    bool vsync = true;
    std::function<void()>
        presentCallback; ///< Vulkan 设备设置弱引用回调，保持低层 Window::present 用法一致。
    std::atomic<bool> rendererThreaded{false};
    // GLFW 回调由主线程刷新尺寸，渲染线程读取缓存而不调用窗口事件 API。
    std::atomic<int> framebufferWidth{0}, framebufferHeight{0};
    // 以下显示状态仅在窗口创建线程访问，不随图形上下文转交给渲染线程。
    WindowMode mode = WindowMode::Windowed;
    FullscreenConfig fullscreen;
    WindowPlacement windowed;
    GLFWmonitor *fullscreenMonitor = nullptr;
    GLFWvidmode desktopMode{};
    bool changingMode = false;
    /**
     * @brief 在创建线程销毁原生窗口。
     * @return 无（析构函数）。
     */
    ~WindowState();
    /**
     * @brief 验证当前线程为窗口创建线程，不符合时抛出异常。
     * @return 无。
     */
    void checkThread() const;
    /**
     * @brief 使当前线程持有窗口的图形上下文。
     * @return 无。
     */
    void makeCurrent() const;
    /**
     * @brief 向尚存活的事件分派器转交事件。
     * @param event 需要排队的窗口事件。
     * @return 无。
     */
    void postEvent(WindowEvent event) noexcept {
        if (auto dispatcher = events.lock())
            dispatcher->post(std::move(event));
    }
    /**
     * @brief 保存原生回调错误，避免异常跨越 C 回调边界。
     * @param error 原生回调内捕获的异常。
     * @return 无。
     */
    void eventError(std::exception_ptr error) noexcept {
        if (auto dispatcher = events.lock())
            dispatcher->recordError(std::move(error));
    }
};
/**
 * @brief 供图形后端读取公开窗口内部状态的友元桥。
 */
struct WindowAccess {
    /**
     * @brief 取得窗口内部状态供具体后端使用。
     * @param window 有效窗口。
     * @return 共享的原生窗口状态。
     */
    static std::shared_ptr<WindowState> state(Window &window);
};
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_WINDOW_HPP_INCLUDED
