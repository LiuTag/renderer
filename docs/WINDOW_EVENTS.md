# 窗口与输入事件订阅

[返回文档导航](README.md)

Window 还支持创建时和运行时的无边框/独占全屏配置；切换通过现有尺寸、焦点和窗口事件通知，详见[窗口与全屏模式](FULLSCREEN.md)。

`Window` 提供按类型订阅及统一订阅。公共事件类型、键名和订阅令牌不暴露 GLFW 类型，`Window` 和 `EventSubscription` 均采用 PImpl。

## 按事件类型订阅

```cpp
#include <renderer/window.hpp>
#include <iostream>

void runWindow() {
    using namespace renderer;
    Window window;
    bool paused = false;

    // 保存返回的令牌，生命周期内保持订阅；Repeat 不触发再次切换。
    auto keyboard = window.subscribe<KeyEvent>([&](const KeyEvent &event) {
        if (event.key == Key::Space && event.action == InputAction::Press)
            paused = !paused;
        if (event.key == Key::S && event.action == InputAction::Press &&
            hasModifier(event.modifiers, Modifiers::Control))
            std::cout << "Save requested\n";
    });
    auto pointer = window.subscribe<MouseMoveEvent>([](const MouseMoveEvent &event) {
        std::cout << "Cursor: " << event.x << ", " << event.y << '\n';
    });
    auto scroll = window.subscribe<MouseScrollEvent>([](const MouseScrollEvent &event) {
        std::cout << "Scroll: " << event.yOffset << '\n';
    });
    auto resized = window.subscribe<FramebufferResizeEvent>([](const auto &event) {
        std::cout << "Framebuffer: " << event.width << " x " << event.height << '\n';
    });

    for (;;) {
        window.pollEvents(); // 所有用户回调在创建窗口的主线程执行。
        if (window.shouldClose())
            break;
        // 根据 paused / 输入状态更新场景，然后 extract、render 和 present。
    }
    keyboard.disconnect(); // 主动取消；其他令牌会在退出作用域时自动取消。
}
```

示例只演示事件分派，不包含渲染循环内容。项目的 `examples/demo.cpp` 已使用 KeyEvent 订阅处理空格暂停，原有 `spacePressed()` 查询仍保留兼容。

不要直接写 `window.subscribe<KeyEvent>(handler);` 并丢弃结果：临时令牌在语句结束时析构，订阅立即取消。令牌不能复制，可以移动；移动赋值会取消被替换的旧订阅。`connected()` 查询连接状态，`disconnect()` 可重复调用。

## 统一订阅

```cpp
auto allEvents = window.subscribe([](const renderer::WindowEvent &event) {
    if (const auto *key = std::get_if<renderer::KeyEvent>(&event)) {
        // 统一记录、输入映射或交给应用事件分发层。
    }
});
```

统一订阅和按类型订阅可以同时使用，回调按注册顺序执行。事件通过 const 引用传递，仅在当前回调期间有效；需要长期保存时复制事件或其数据。

## 事件类型和单位

| 类型 | 数据与用途 |
| --- | --- |
| KeyEvent | Key、扫描码、Press/Release/Repeat、修饰键 |
| TextInputEvent | 已提交的 Unicode 码点，char32_t，不包含输入法组合编辑状态 |
| MouseMoveEvent | 客户区左上原点的 x/y，屏幕坐标单位 |
| MouseButtonEvent | 按键、Press/Release、修饰键，以及按键发生时的客户区 x/y |
| MouseScrollEvent | xOffset/yOffset，保留触控板小数滚动量 |
| CursorEnterEvent | 光标进入/离开客户区 |
| WindowMoveEvent | 客户区在屏幕上的 x/y |
| WindowResizeEvent | 客户区宽高，屏幕坐标单位 |
| FramebufferResizeEvent | 渲染缓冲宽高，物理像素；最小化时可能为零 |
| WindowFocusEvent | 焦点获得/失去 |
| WindowMinimizeEvent | 最小化/恢复 |
| WindowMaximizeEvent | 最大化/恢复 |
| WindowContentScaleEvent | xScale/yScale，内容缩放比例变化 |
| WindowCloseEvent | 原生关闭、requestClose 或默认 Esc 逻辑产生的关闭请求 |
| WindowRefreshEvent | 窗口内容需要重绘 |
| FileDropEvent | 已复制的 filesystem::path 列表，原生 UTF-8 路径转换为平台路径 |

Key 使用引擎枚举，例如 `Key::W`、`Key::D0`、`Key::LeftShift`、`Key::F1`。扫描码是平台相关值，文本输入应使用 TextInputEvent。Modifiers 支持 Shift、Control、Alt、Super、CapsLock、NumLock，使用 `hasModifier()` 检查组合。

高 DPI 环境下，鼠标/客户区尺寸与 framebuffer 像素尺寸可能不同；渲染目标使用 FramebufferResizeEvent 或 `framebufferSize()`。内部尺寸缓存仍在原生回调中立即更新，订阅回调延迟分派不会影响渲染线程的尺寸读取。

## 分派与生命周期约定

- 订阅、pollEvents、窗口操作和窗口析构均在创建窗口的线程执行。原生事件泵属于应用主线程，独立渲染线程不执行用户事件回调。
- 原生回调只入队，用户回调统一在 pollEvents 中执行。resize 等窗口操作即使同步触发了原生回调，用户处理函数仍等到后续 pollEvents。
- 每次分派固定事件批次及订阅者快照。在回调中新增的订阅、产生的新事件，从下一次 pollEvents 开始处理。
- 取消订阅对尚未分派的调用立即生效，允许自取消或取消其他订阅者。令牌可在其他线程取消，但不等待已经分派的回调结束；捕获对象的生命周期由应用协调。
- 某个用户回调抛出异常时继续处理本批其余事件及订阅者，结束后由 pollEvents 重抛第一个异常。异常不跨越原生 C 回调边界，下一次 pollEvents 仍可正常使用。
- 不允许在同一个窗口的回调中递归调用 pollEvents；它会抛出 logic_error。
- 移动 Window 不使原有令牌失效；但捕获旧 Window 对象引用的应用回调需要自行调整引用。
- Window 销毁会取消其全部订阅，即使 Renderer 仍保留原生窗口/上下文。令牌不会延长窗口生命周期。允许回调中销毁 Window，后续订阅者将不再被调用。

订阅不会重放当前窗口状态，也不会主动通知初始尺寸。需要初始值时调用 framebufferSize()。多个窗口共享原生事件泵，但分别保存队列；各自的用户回调由各自 pollEvents 分派。

## 关闭与取消

```cpp
auto closing = window.subscribe<renderer::WindowCloseEvent>([&](const auto &) {
    if (hasUnsavedChanges)
        window.cancelClose();
});
```

为兼容现有行为，Esc 按下时默认请求关闭；该请求同样可以被取消，按键 Repeat 不会反复产生关闭请求。`requestClose()` 立即设置 shouldClose 标记并排队通知，重复请求在标记已设置时不重复入队；`cancelClose()` 清除标记。若需要处理程序化关闭的取消逻辑，循环应先 pollEvents 再检查 shouldClose，如上方示例。

本接口是有序广播，不提供事件冒泡或 handled 拦截。多个关闭监听者都可以修改关闭标记，应用应集中管理最终关闭决定。

## 验证与源码

- `include/renderer/events.hpp`：公共数据、键名与 PImpl 令牌。
- `include/renderer/window.hpp`：统一/按类型订阅和关闭取消。
- `src/events.cpp`、`src/internal/events.hpp`：令牌所有权、事件队列、订阅快照及异常策略。
- `src/platform/window.cpp`：16 种事件的 GLFW 适配、路径复制和主线程分派。
- `tests/event_tests.cpp`：8 组无显示环境测试，覆盖顺序、数据所有权、取消、移动、异常、重入和跨线程取消。
- `tests/window_tests.cpp`：隐藏真实窗口，覆盖原生回调适配、真实 resize、关闭取消、错误线程、移动、回调中销毁窗口及渲染线程保留上下文。

输入测试通过调用已安装的原生回调确定性注入参数，不向桌面发送键鼠输入；resize 使用真实窗口操作。公开事件系统已提供 TextCompositionEvent；Windows 的原生组合生命周期只排队，在 pollEvents 分派。setTextInputArea 接收 UI 编辑区域并定位候选窗口；clipboardText/setClipboardText 提供系统剪贴板。相机控制器由独立 camera 模块接收应用映射的输入；鼠标捕获模式尚未封装。
