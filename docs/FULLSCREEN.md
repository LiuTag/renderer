# 窗口与全屏模式

[返回文档导航](README.md) · [事件订阅](WINDOW_EVENTS.md) · [二次开发手册](DEVELOPMENT_GUIDE.md)

`Window` 支持普通窗口、无边框全屏和独占全屏。可以在创建时设置，也可以在窗口创建线程运行时切换；切换复用原有 OpenGL 上下文，不要求重新创建 Renderer 或 Device。

## 三种模式

| WindowMode | 行为 | 分辨率与刷新率 |
| --- | --- | --- |
| Windowed | 普通带边框、可调整尺寸的窗口 | 使用窗口客户区尺寸 |
| BorderlessFullscreen | 移除边框并覆盖目标显示器的整个区域，包括任务栏区域 | 使用桌面视频模式，不切换显示器分辨率 |
| ExclusiveFullscreen | 将窗口关联到目标显示器，由 GLFW 请求全屏视频模式 | 默认桌面模式，也可指定期望分辨率和刷新率 |

独占全屏的窗口系统行为由平台和驱动决定，SDK 不承诺绕过系统合成器。当前实际测试平台为 Windows；其他平台的窗口位置和显示模式行为仍受窗口系统约束。

## 创建时指定

```cpp
#include <renderer/window.hpp>

renderer::Window window({
    .width = 1280,
    .height = 800,
    .title = "Fullscreen application",
    .mode = renderer::WindowMode::BorderlessFullscreen,
});
```

这里 width/height 是退出全屏后恢复的客户区尺寸。全屏使用自己的尺寸选择，避免把桌面宽高覆盖成窗口恢复尺寸。

指定独占视频模式：

```cpp
renderer::Window window({
    .width = 1280,
    .height = 800,
    .mode = renderer::WindowMode::ExclusiveFullscreen,
    .fullscreen = {
        .monitorIndex = 0,
        .width = 1920,
        .height = 1080,
        .refreshRate = 60,
    },
});
```

## 运行时切换和查询

```cpp
window.setMode(renderer::WindowMode::BorderlessFullscreen);
const auto current = window.mode();
window.setMode(renderer::WindowMode::ExclusiveFullscreen,
               {.width = 1920, .height = 1080, .refreshRate = 60});
window.setMode(renderer::WindowMode::Windowed);
```

首次离开普通窗口时，保存正常客户区的位置、尺寸、边框、可调整尺寸属性及最大化状态。两种全屏模式之间切换不会覆盖这份记录。回到 Windowed 时恢复记录；如果原窗口最大化，仍恢复最大化，并保留其正常窗口矩形。

`setMode()` 的 FullscreenConfig 参数含义：

| 字段 | 默认值 | 约束与语义 |
| --- | --- | --- |
| monitorIndex | -1 | 自动选择窗口重叠面积最大的显示器；已在全屏时保留当前显示器；完全离屏时回退主显示器 |
| width、height | 0、0 | 独占模式下同时为 0 使用进入全屏前的桌面分辨率；否则须同时为正 |
| refreshRate | 0 | 独占模式下 0 使用桌面刷新率，正数表示期望 Hz |

monitorIndex 的非负值是当前 GLFW 显示器列表下标，从 0 开始，0 是主显示器。编号不是持久显示器 ID，热插拔后应重新选择；越界会抛出异常。当前公共接口尚未提供显示器信息枚举。

独占请求由 GLFW 选择最接近的受支持视频模式，实际尺寸以 `framebufferSize()` 和尺寸事件为准。独占模式切换到无边框时，恢复进入独占前的桌面模式，再使用对应桌面尺寸，不把独占分辨率当成桌面分辨率。

BorderlessFullscreen 仅使用 monitorIndex，width/height/refreshRate 必须保持 0；Windowed 忽略 FullscreenConfig。重复设置同一全屏模式和相同视频参数不会重复切换。

## 事件、线程与渲染目标

所有模式设置和查询都在 Window 的创建线程执行。启用独立渲染线程后仍从应用主线程调用 `window.setMode()`，不要在主线程重新绑定上下文。模式切换不会替应用等待整个 GPU 空闲，已排队帧可能仍使用提交时的尺寸，后续提交采用新的尺寸。

窗口位置、客户区、framebuffer、焦点和最大化状态变化沿用已有事件接口。原生回调立即更新 framebuffer 缓存，用户订阅者仍只在 `pollEvents()` 中执行。可用 Keyboard Press 处理模式开关，不依赖系统 Repeat：

```cpp
auto fullscreenKey = window.subscribe<renderer::KeyEvent>([&](const auto &event) {
    if (event.key == renderer::Key::F11 && event.action == renderer::InputAction::Press) {
        window.setMode(window.mode() == renderer::WindowMode::Windowed
                           ? renderer::WindowMode::BorderlessFullscreen
                           : renderer::WindowMode::Windowed);
    }
});
```

保存返回的令牌才能保持订阅。高层 Renderer 会在后续帧按实际 framebuffer 尺寸重建内部目标；低层 RHI 应用需要自行重建相应纹理和编译图。不要仅修改 viewport。

如果全屏显示器在 `pollEvents()` 期间断开，或平台解除独占显示器关联，窗口会退回普通模式。没有专用模式变更事件，需要时可在 poll 后查询 mode。

## 参数和可见性约定

- 隐藏普通窗口可以进入无边框模式，并保持隐藏。独占全屏会占用显示器，因此要求窗口可见；创建时 `visible=false` 搭配独占模式抛出 invalid_argument，隐藏窗口运行时进入独占模式抛出 logic_error。
- `resize(width,height)` 只用于普通窗口。全屏时抛出 logic_error；调整独占分辨率请重新调用 setMode 并提供视频参数，或先退出全屏再调整窗口尺寸。
- 非法模式、负尺寸、只指定一个分辨率维度、负刷新率和越界显示器编号会被拒绝，参数校验失败不改变当前模式。
- 平台操作失败时抛异常，并尽量恢复原模式；显示器已失效时退回普通窗口。应用应在事件循环边界处理异常。
- 原生窗口和上下文保持不变，Window 移动后保留模式及恢复记录。切换不会替应用改写相机的固定 aspectRatio；需要自动适配时设为 0。

## 示例程序

```powershell
.\build\windows\Release\renderer_demo.exe --borderless --threaded
.\build\windows\Release\renderer_demo.exe --fullscreen --monitor 0 --threaded
```

示例 F11 在普通窗口和无边框全屏之间切换，Alt+Enter 在普通窗口和独占全屏之间切换；已处于任意全屏时按对应快捷键会退出。Space 和 Esc 保持原有暂停/关闭用途。`--hidden` 可搭配无边框和正数 --frames，不能搭配独占全屏。

## 验证

`renderer.fullscreen` 是需要显示环境的串行 GPU 测试，覆盖初始两种全屏、互切、位置尺寸及最大化恢复、移动 Window、非法参数、隐藏限制、线程归属、平台解除独占后的恢复、专用渲染线程与实际回读。为减少桌面干扰，独占测试使用当前桌面视频参数，不要求修改桌面分辨率或刷新率；不同视频模式和多显示器热插拔仍需在目标设备上实测。
