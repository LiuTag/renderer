/**
 * @file
 * @brief 声明跨平台键鼠与窗口事件、修饰键操作和 RAII 事件订阅令牌。
 */
#ifndef RENDERER_INCLUDE_RENDERER_EVENTS_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_EVENTS_HPP_INCLUDED
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace renderer {
namespace detail {
struct EventAccess;
}

// 引擎自己的可移植键名，不在公共接口中暴露原生窗口类型。
/** @brief 引擎自有的跨平台键盘键名，与原生窗口类型解耦。 */
enum class Key : int {
    Unknown = -1,
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus,
    Period,
    Slash,
    D0 = 48,
    D1,
    D2,
    D3,
    D4,
    D5,
    D6,
    D7,
    D8,
    D9,
    Semicolon = 59,
    Equal = 61,
    A = 65,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    LeftBracket = 91,
    Backslash,
    RightBracket,
    GraveAccent = 96,
    World1 = 161,
    World2,
    Escape = 256,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock = 280,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    F1 = 290,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    F25,
    Keypad0 = 320,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    KeypadEqual,
    LeftShift = 340,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,
    Menu
};
/** @brief 按键释放、首次按下或系统重复的动作类型。 */
enum class InputAction { Release, Press, Repeat };
/** @brief 跨平台鼠标按键编号，包含左右、中键和扩展键。 */
enum class MouseButton { Left, Right, Middle, Button4, Button5, Button6, Button7, Button8 };
/** @brief 可按位组合的修饰键及锁定状态标记。 */
enum class Modifiers : uint8_t {
    None = 0,
    Shift = 1,
    Control = 2,
    Alt = 4,
    Super = 8,
    CapsLock = 16,
    NumLock = 32
};
/**
 * @brief 合并两组修饰键标记。
 * @param a 第一组标记。
 * @param b 第二组标记。
 * @return 两组标记的按位或。
 */
constexpr Modifiers operator|(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
/**
 * @brief 取两组修饰键标记的交集。
 * @param a 第一组标记。
 * @param b 第二组标记。
 * @return 两组标记的按位与。
 */
constexpr Modifiers operator&(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
/**
 * @brief 判断给定标记是否全部包含在当前修饰键状态中。
 * @param value 当前修饰键标记集合。
 * @param flag 需要检查的单个或组合标记。
 * @return value 包含 flag 的全部位时为 true；flag 为 None 时也返回 true。
 */
constexpr bool hasModifier(Modifiers value, Modifiers flag) {
    return (value & flag) == flag;
}

/**
 * @brief 键盘物理按键、扫描码、动作和修饰键状态事件。
 */
struct KeyEvent {
    Key key = Key::Unknown;
    int scanCode = -1; // 平台扫描码；字符输入应使用 TextInputEvent。
    InputAction action = InputAction::Press;
    Modifiers modifiers = Modifiers::None;
};
/**
 * @brief 已提交 Unicode 码点事件，不包含输入法组合编辑状态。
 */
struct TextInputEvent {
    char32_t codepoint = 0; // 已提交的 Unicode 码点，不包含输入法组合中的编辑状态。
};
/** @brief 输入法组合生命周期；光标使用 Unicode 字符偏移，Commit 内容不再重复发为 TextInputEvent。
 */
enum class CompositionPhase { Begin, Update, Commit, End };
/** @brief 输入法预编辑或提交的 UTF-8 文本；End 取消尚未提交的预编辑。 */
struct TextCompositionEvent {
    CompositionPhase phase = CompositionPhase::Update;
    std::string text;
    uint32_t cursor = 0;
};
/** @brief 当前可编辑区域，使用客户区左上原点屏幕坐标；供候选窗定位和输入法启停。 */
struct TextInputArea {
    bool active = false;
    float x = 0, y = 0, width = 0, height = 0;
};
/**
 * @brief 以客户区左上为原点、屏幕坐标单位的鼠标位置事件。
 */
struct MouseMoveEvent {
    double x = 0, y = 0; // 客户区左上角原点，屏幕坐标单位，不是 framebuffer 像素。
};
/**
 * @brief 鼠标按下或释放事件，附带修饰键和当时客户区位置。
 */
struct MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    InputAction action = InputAction::Press; // 鼠标按键只产生 Press / Release。
    Modifiers modifiers = Modifiers::None;
    double x = 0, y = 0; // 按键发生时的客户区坐标，与 MouseMoveEvent 使用同一单位。
};
/**
 * @brief 保留触控板小数滚动量的水平与垂直滚轮事件。
 */
struct MouseScrollEvent {
    double xOffset = 0, yOffset = 0; // 保留触控板的小数滚动量。
};
/**
 * @brief 鼠标指针进入或离开客户区的事件。
 */
struct CursorEnterEvent {
    bool entered = false;
};
/**
 * @brief 窗口在屏幕坐标系中位置改变的事件。
 */
struct WindowMoveEvent {
    int x = 0, y = 0;
};
/**
 * @brief 窗口客户区屏幕坐标尺寸改变的事件。
 */
struct WindowResizeEvent {
    int width = 0, height = 0;
}; // 客户区屏幕坐标单位。
/**
 * @brief 实际渲染像素尺寸改变的事件，尺寸允许为零。
 */
struct FramebufferResizeEvent {
    int width = 0, height = 0;
}; // 实际像素，可为 0。
/**
 * @brief 窗口获取或失去键盘焦点的事件。
 */
struct WindowFocusEvent {
    bool focused = false;
};
/**
 * @brief 窗口最小化状态改变的事件。
 */
struct WindowMinimizeEvent {
    bool minimized = false;
};
/**
 * @brief 窗口最大化状态改变的事件。
 */
struct WindowMaximizeEvent {
    bool maximized = false;
};
/**
 * @brief 窗口水平与垂直内容缩放比例改变的事件。
 */
struct WindowContentScaleEvent {
    float xScale = 1, yScale = 1;
};
/**
 * @brief 可通过 Window::cancelClose 撤销的关闭请求事件。
 */
struct WindowCloseEvent {}; // 关闭请求；处理时可通过 Window::cancelClose() 撤销。
/**
 * @brief 系统请求应用重绘窗口的事件。
 */
struct WindowRefreshEvent {};
/**
 * @brief 拥有文件路径副本的文件拖入事件。
 */
struct FileDropEvent {
    std::vector<std::filesystem::path> paths; // 拥有路径副本，原生回调结束后仍有效。
};

using WindowEvent =
    std::variant<KeyEvent, TextInputEvent, MouseMoveEvent, MouseButtonEvent, MouseScrollEvent,
                 CursorEnterEvent, WindowMoveEvent, WindowResizeEvent, FramebufferResizeEvent,
                 WindowFocusEvent, WindowMinimizeEvent, WindowMaximizeEvent,
                 WindowContentScaleEvent, WindowCloseEvent, WindowRefreshEvent, FileDropEvent,
                 TextCompositionEvent>;
/**
 * @brief 统一事件回调；event 是仅在本次调用期间有效的事件引用，回调无返回值。
 */
using WindowEventHandler = std::function<void(const WindowEvent &event)>;

// 移动独占的 RAII 订阅令牌，必须保存；析构或 disconnect 会取消后续分派。
// 不延长窗口生命周期；窗口销毁后 connected 返回 false。
/**
 * @brief 使用独占 PImpl 的 RAII 订阅令牌，析构即取消订阅且不延长窗口生命周期。
 */
class EventSubscription {
  public:
    /**
     * @brief 创建未连接的订阅令牌。
     * @return 无（构造函数）。
     */
    EventSubscription();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~EventSubscription();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    EventSubscription(EventSubscription &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    EventSubscription &operator=(EventSubscription &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    EventSubscription(const EventSubscription &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    EventSubscription &operator=(const EventSubscription &other) = delete;
    /**
     * @brief 取消订阅，可重复调用；不再分派后续事件。
     * @return 无。
     */
    void disconnect() noexcept;
    /**
     * @brief 检查订阅是否仍连接到存活窗口。
     * @return 窗口与订阅均有效时为 true，否则为 false。
     */
    bool connected() const noexcept;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    /**
     * @brief 由内部事件系统创建已连接的订阅令牌。
     * @param impl 独占的订阅实现状态。
     * @return 无（构造函数）。
     */
    explicit EventSubscription(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend struct detail::EventAccess;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_EVENTS_HPP_INCLUDED
