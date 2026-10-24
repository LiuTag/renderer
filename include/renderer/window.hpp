/**
 * @file
 * @brief 声明窗口配置、生命周期、事件订阅和显示控制接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_WINDOW_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_WINDOW_HPP_INCLUDED
#include "renderer/events.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace renderer {
namespace detail {
struct WindowAccess;
}
/** @brief 普通窗口、覆盖桌面的无边框全屏，或由显示器视频模式驱动的独占全屏。 */
enum class WindowMode { Windowed, BorderlessFullscreen, ExclusiveFullscreen };
/** @brief 窗口使用的图形 API；Vulkan 创建不含 OpenGL 上下文的原生窗口。 */
enum class GraphicsApi { OpenGL, Vulkan };
/**
 * @brief 全屏目标显示器与可选独占视频模式；不暴露原生显示器类型。
 */
struct FullscreenConfig {
    int monitorIndex = -1;     ///< -1 自动选择窗口主要所在显示器，否则为当前显示器列表下标。
    int width = 0, height = 0; ///< 独占分辨率，两项同时为 0 使用桌面模式，否则须均为正。
    int refreshRate = 0;       ///< 独占刷新率（Hz），0 使用桌面刷新率；无边框模式只接受 0。
};
/**
 * @brief 窗口初始客户区尺寸、标题、可见性、上下文与全屏配置。
 */
struct WindowConfig {
    int width = 1280, height = 800;
    std::string title = "Renderer";
    bool visible = true;
    bool vsync = true;
    bool debugContext = true;
    WindowMode mode = WindowMode::Windowed;
    FullscreenConfig fullscreen; ///< 全屏使用独立尺寸；width/height 保留为退出全屏后的窗口尺寸。
    GraphicsApi graphicsApi = GraphicsApi::OpenGL; ///< 创建后不可切换，须与设备工厂一致。
};

// 窗口创建、事件、尺寸查询与修改均在创建窗口的应用主线程执行。
// Renderer 开启独立线程时，由内部逻辑转移设备操作权，窗口事件仍由主线程负责。
/**
 * @brief 以 PImpl 管理主线程窗口生命周期与事件，独立渲染时由内部移交设备操作权。
 */
class Window {
  public:
    /**
     * @brief 在应用主线程创建窗口；根据 graphicsApi 创建 OpenGL 上下文或 Vulkan 窗口。
     * @param config 初始窗口、上下文与全屏配置；独占全屏要求 visible=true。
     * @return 无（构造函数）。
     */
    explicit Window(const WindowConfig &config = {});
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~Window();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    Window(Window &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    Window &operator=(Window &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Window(const Window &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Window &operator=(const Window &other) = delete;
    /**
     * @brief 在创建线程收集并按订阅顺序分派事件；本批结束后重抛首个回调异常。
     * @return 无。
     */
    void pollEvents();
    // 按注册顺序在 pollEvents 中分派；回调异常完成本批分派后由 pollEvents 抛出。
    // 保存返回的 RAII 令牌，否则语句结束即取消；允许在回调中取消订阅。
    /**
     * @brief 订阅全部窗口事件，必须保存返回令牌以保持订阅。
     * @param handler 非空回调；在 pollEvents 中执行，参数仅在回调期间有效。
     * @return 独占订阅令牌，析构或 disconnect 会取消后续分派。
     */
    [[nodiscard]] EventSubscription subscribe(WindowEventHandler handler);
    /**
     * @brief 仅订阅某一种窗口事件；必须保存返回令牌。
     * @tparam T WindowEvent 变体中包含的某个事件类型。
     * @param handler 接收事件 T 的非空回调，在 pollEvents 中执行。
     * @return 控制该回调生命周期的订阅令牌。
     */
    template <class T>
    [[nodiscard]] EventSubscription subscribe(std::function<void(const T &)> handler) {
        if (!handler)
            throw std::invalid_argument("Window event handler must not be empty");
        return subscribe([handler = std::move(handler)](const WindowEvent &event) {
            if (const auto *value = std::get_if<T>(&event))
                handler(*value);
        });
    }
    /**
     * @brief 读取窗口关闭请求标记。
     * @return 已请求关闭时为 true。
     */
    bool shouldClose() const;
    /**
     * @brief 设置关闭标记并排队关闭事件，重复请求合并。
     * @return 无。
     */
    void requestClose();
    /**
     * @brief 清除关闭标记，可在关闭事件回调内撤销退出。
     * @return 无。
     */
    void cancelClose(); // 可在 WindowCloseEvent 回调中撤销关闭请求。
    /**
     * @brief 在创建线程呈现；Vulkan 需已绑定设备，独立渲染线程模式调用 Renderer::present。
     * @return 无。
     */
    void present(); // 独立渲染线程模式请调用 Renderer::present()。
    /**
     * @brief 查询当前窗口模式；在创建线程调用，显示器断开后的状态由 pollEvents 同步。
     * @return 当前普通窗口、无边框全屏或独占全屏模式。
     */
    WindowMode mode() const;
    /**
     * @brief 在创建线程切换模式，保留上下文；回到窗口模式恢复先前位置、尺寸及最大化状态。
     * @param mode 目标窗口模式；非法枚举抛出 invalid_argument。
     * @param fullscreen 全屏目标与独占视频参数；窗口模式忽略，无边框仅接受默认视频参数。
     * @return 无；显示器或平台操作失败抛出异常，隐藏窗口不能进入独占全屏。
     */
    void setMode(WindowMode mode, const FullscreenConfig &fullscreen = {});
    /**
     * @brief 修改普通窗口客户区尺寸；全屏状态下抛出 logic_error，改分辨率请使用 setMode。
     * @param width 正的客户区宽度，屏幕坐标单位。
     * @param height 正的客户区高度，屏幕坐标单位。
     * @return 无。
     */
    void resize(int width, int height);
    /**
     * @brief 修改窗口标题。
     * @param title UTF-8 编码的标题文本。
     * @return 无。
     */
    void setTitle(const std::string &title);
    /**
     * @brief 查询窗口实际可渲染区域的像素尺寸。
     * @return 宽度与高度组成的二元组；最小化时可能为零。
     */
    std::pair<int, int> framebufferSize() const;
    /**
     * @brief 查询客户区尺寸，用于将鼠标事件坐标换算为帧缓冲像素。
     * @return 客户区宽高，使用与鼠标事件一致的屏幕坐标单位；在创建线程调用。
     */
    std::pair<int, int> clientSize() const;
    /**
     * @brief 查询空格键当前是否按下，用于简单轮询。
     * @return 空格键按下时为 true。
     */
    bool spacePressed() const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend struct detail::WindowAccess;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_WINDOW_HPP_INCLUDED
