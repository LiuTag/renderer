# 游戏 UI 模块

[文档导航](README.md) · [模块划分](MODULES.md) · [SDK 安装](SDK.md) · [独立示例](../examples/ui/README.md) · [综合游戏](../examples/game/README.md)

`renderer::ui` 使用固定版本 RmlUi 6.3 + FreeType 2.14.3，提供游戏界面的文档、布局、文字、图片、基础控件和事件。公共服务采用 PImpl，不暴露 RmlUi/FreeType 类型；模块只链接 core 的公共接口，不创建窗口、不调用 OpenGL/Vulkan、不包含引擎私有头文件。

## 范围

| 功能 | 当前能力 |
| --- | --- |
| 文档与皮肤 | UTF-8 RML/RCSS 文件或内存文档、布局、属性修改、样式状态、过渡/动画和矩阵变换 |
| 基础控件 | 面板、图片、文本、按钮、复选框、范围滑条、进度条、滚动区域/列表、文本输入框；提示框由布局/事件组合 |
| 文字 | FreeType 字体、多个字族、回退字体、字体集合索引、自动生成字形纹理 |
| 输入 | 引擎键鼠事件、已提交 Unicode 文本、鼠标捕获、焦点、尺寸/DPI、输入坐标到像素映射 |
| 事件 | RAII 订阅、click/change/submit 等冒泡事件、事件状态副本、轮询、异常隔离 |
| 绘制 | 有序透明合成、相邻兼容命令合并、矩形裁剪、线性预乘 Alpha、双后端和渲染线程 |
| 输出 | 窗口呈现、独立离屏纹理、renderToImage/readback/PNG 均包含 UI |

RML/RCSS 是 RmlUi 的文档语言，并非完整浏览器 HTML/CSS。应用应显式提供基础样式，例如 `div { display:block; }` 和控件尺寸；示例的 [theme.rcss](../examples/ui/theme.rcss) 是可修改的起点。

适配层为 `<button>` 补上表单禁用/焦点语义，以及 Enter/Space 键盘激活。失去窗口焦点会取消按住的按钮，不会把取消操作转换成一次点击。

当前适配器支持基础绘制与变换，支持矩形滚动裁剪。非矩形/圆角溢出遮罩、独立合成层、模糊/滤镜、渐变和自定义 Shader 装饰器尚未实现，使用时记录诊断，不能据此认为所有 RCSS 效果均有完整支持。UI 不使用 OIT：必须保持文档的透明层叠顺序。

## 构建与 SDK

默认 `RENDERER_BUILD_UI=ON`。设为 OFF 后不下载/构建 RmlUi，不构建 UI 库/测试/示例、不安装专用头文件和库；FreeType 若仍被 text 启用会继续构建。综合 game 示例需要全部模块，因此也会跳过。core 的通用覆盖层接口仍然可用，渲染核心和后端不反向链接 UI。

需要场景中的名字牌、路牌和伤害数字时，使用独立的[世界文字模块](TEXT.md)。它生成世界精灵批次并支持深度测试，与屏幕 UI 的覆盖层绘制分开。

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS ui opengl)
target_link_libraries(my_game PRIVATE renderer::ui renderer::opengl)
```

CPU 工具可只请求 ui；图形输出需要显式链接后端。SDK 自动传递私有 RmlUi 和 FreeType 静态库，无需使用者下载依赖或安装它们的开发包。示例字体路径为 `renderer_UI_FONT`，只在 UI 组件启用时定义；这不是强制的应用字体。

源码离线配置可使用：

```powershell
cmake --preset windows -DFETCHCONTENT_SOURCE_DIR_RMLUI=C:/deps/RmlUi-6.3 -DFETCHCONTENT_SOURCE_DIR_FREETYPE=C:/deps/freetype-VER-2-14-3
```

两个依赖均通过固定 URL/SHA256 的 FetchContent 管理，不将依赖源码和构建产物提交仓库。FreeType 的 zlib/bzip2/png/HarfBuzz/Brotli 外部依赖关闭，字体轮廓栅格化和基本文字可用；复杂文字塑形/双向排版不在首版范围。SDK 按配置安装库、许可证和 Lato 示例字体。

## 最小接入

```cpp
#include <renderer/ui/ui.hpp>
#include <renderer/renderer.hpp>
using namespace renderer;

UiContext ui({1280, 720});
ui.loadFont(fontPath); // 应用提供字体路径，示例可用 CMake 的 renderer_UI_FONT。
auto document = ui.loadDocument("assets/hud.rml");
auto button = ui.subscribe(document, "pause", "click", [&](const UiEvent&) {
    paused = !paused;
}); // 保留令牌，否则离开表达式/作用域就取消。
ui.update(0); // 第一次输入前先完成布局。

// 每帧，在角色/环境更新之后、提交渲染之前执行。
ui.setText(document, "coins", std::to_string(coins));
ui.update(dt);
view.overlay = ui.snapshot();
renderer.render(extractor.extract(scene, assets), view);
```

UI 文档中的 ID 在文档内标识元素，多个文档可以使用相同 ID。文档身份是进程唯一的值，跨上下文、卸载后或无效的身份会被拒绝。节点树由 UI 文档管理，与世界 Scene 独立；游戏不需要为每个按钮创建场景节点。

## 输入路由与 DPI

UI 不自动订阅 Window。应用用一个统一事件入口，先交给 UI，再决定是否驱动玩法与相机：

```cpp
auto input = window.subscribe([&](const WindowEvent& event) {
    const bool captured = ui.processEvent(event);
    if (!captured)
        gameInput(event);
    // 游戏中已有按键的 Release 即使被捕获也要清理，避免角色持续移动。
});
```

`processEvent` 返回此事件是否被 UI 捕获，不等于 RmlUi 的事件传播返回值。指针命中实际元素或在 UI 内按下后拖出时捕获鼠标；有控件焦点时捕获键盘。失去窗口焦点会清理按键、拖动和控件焦点。背景 HUD 应使用 `body { pointer-events:none; }`，可交互面板再设置 `pointer-events:auto`，让空白区域继续接收世界交互。

鼠标事件是客户区坐标，覆盖层使用 framebuffer 像素；两者不能混用：

```cpp
const auto [cw, ch] = window.clientSize();
const auto [pw, ph] = window.framebufferSize();
ui.setViewport({unsigned(pw), unsigned(ph), double(cw), double(ch), float(pw)/float(cw)});
```

仅在尺寸为正时调用。width/height 是画布像素，inputWidth/inputHeight 是鼠标坐标尺寸，density 控制 RCSS 的 dp。processEvent 也处理对应的窗口尺寸、framebuffer 尺寸和内容缩放事件。framebuffer 为零时暂停绘制，恢复正尺寸后继续。

## 控件与资源

`setText` 自动转义 RML，适用于用户/游戏文本；`setMarkup` 用于应用提供的可信子元素标记。`setProperty` 修改 RCSS 属性，`setValue` 操作表单/进度值，`setChecked` 操作复选状态，`setEnabled` 用于表单禁用。element 返回独立状态副本和最近布局的矩形，不暴露内部指针。修改后调用 update 或 snapshot 刷新布局。

程序设置值时，RmlUi 控件可能产生 change 事件。回调应明确设置业务状态，例如 `music.setEnabled(event.checked)`；避免只做反向切换而形成状态反馈。setChecked 对相同值不重复设置。

```cpp
ui.registerImage("wood", icon); // TextureDesc：左下原点、未预乘 RGBA8，srgb 标明颜色空间。
ui.setValue(document, "health", "0.75");
ui.setValue(document, "volume", "60");
ui.setChecked(document, "music", true);
ui.setEnabled(document, "craft", canCraft);
```

RML 使用 `<img src="ui:wood"/>`。注册图片名非空且唯一；同名重复注册拒绝。文档也可使用相对路径加载普通图片，沿用引擎图片导入。适配层统一处理文件图片与字形纹理的 UV 差异、sRGB 和 Alpha；应用不需要预乘或手工翻转。字体/文档/相对资源支持 Unicode 文件路径。

registerImage(name, description, layer=0) 可选取压缩/数组图片的基准 mip，CPU 解码后保存普通二维线性预乘纹理；文件路径读取 DDS 时取第 0 层，此 UI 路径不保留 BC GPU 存储。详见[纹理说明](TEXTURES.md)。releaseFontCache() 显式回收并重建共享 RmlUi 字体资源，影响同线程全部 UiContext；不能在事件回调分派期间调用，旧 OverlaySnapshot 继续保留所需纹理。UI 小字使用位图，世界文字可选择[独立 SDF 模式](TEXT.md)。

Lato 示例字体只覆盖相应拉丁字符。中文显示需要应用加载中文字体，例如 `ui.loadFont(chineseFont, true)` 注册为回退；字体由共享运行时保留到最后一个 UI 上下文销毁。TextInputEvent 输入已提交码点，TextCompositionEvent 处理预编辑、选区替换、提交、取消和失焦恢复。Windows 平台已接 IMM32 组合消息；应用每帧将 textInputArea() 传给 Window::setTextInputArea() 定位候选窗口。setPlatformServices 可注入 Window 的系统剪贴板读写，未注入时保留内部剪贴板；其他平台原生预编辑适配尚未实现。接入代码见[渲染扩展](RENDERING_EXTENSIONS.md)。

文档/样式/字体通过适配层的 Unicode 文件接口读取，单个文件上限 64 MiB。图片导入沿用引擎已有的解码与尺寸限制。

## 线程、事件与生命周期

进程中的所有 UiContext 在同一 UI 线程创建、调用和析构；方法检查线程，错误线程调用抛出异常，销毁也必须遵守此约定。模块没有自己的工作线程。update 的 dt 是 [0,4] 内有限秒数，用于样式动画时间；布局和 RmlUi::Render 都在 UI 线程执行。

RmlUi 的渲染回调只收集 CPU 几何。snapshot 返回 core 的共享不可变 OverlaySnapshot，独立保留几何和纹理，可以跨线程读取、放入 RenderView，或在文档/UiContext 已销毁后继续提交。应用不应让渲染线程访问 UiContext 或保留 RmlUi 指针。

事件在处理输入时保存值副本，用户回调在 update 完成 RmlUi 更新后调用。回调可设置控件或业务数据，但不可重入 processEvent/update/snapshot，也不要在回调中销毁当前 UiContext。取消订阅和卸载文档会阻止后续分派；已经开始的回调不会被中断。回调异常写入 takeMessages，并继续其他订阅。

takeEvents 提供轮询副本，订阅与轮询可以并用。每次待分派/轮询最多 4096 项，溢出有诊断；轮询积累满后不会阻止订阅继续工作。长期运行应用应定期取走事件和诊断。

## 合成流程与性能

```text
输入 → UI 文档/布局/动画 → CPU 几何与纹理 → OverlaySnapshot
世界渲染 → ToneMap → 可选 FXAA → Screen Overlay → 呈现 / 回读 / PNG
```

核心只提供 OverlayVertex/OverlayDraw/OverlaySnapshot 与合成 Pass，不知道按钮、布局、字体或 RmlUi。顶点是画布左上原点像素坐标，颜色和采样纹理是线性预乘 Alpha。输出到 sRGB 显示附件时正确编码；UI 不经过场景曝光、昼夜调色或 FXAA。

覆盖层严格按绘制顺序透明合成，只合并相邻且纹理/裁剪相同的命令，不能为减少 draw call 全局按纹理排序。帧槽复用前等待 GPU 完成，再上传覆盖层顶点和索引；纹理缓存在设备中并按既有延迟资源机制回收。当前每次 snapshot 会整理并复制 CPU 几何，适合常规 HUD/菜单，尚未提供大型列表虚拟化或静态子树的独立 GPU 缓存。

RenderStats.overlayDrawCalls/overlayTriangles 单独记录 UI；drawCalls 包含 UI，triangles 继续仅统计场景。overlay 为空时不生成 Screen Overlay Pass。快照画布尺寸和目标不同时横纵分别缩放，通常应同步更新 UiViewport 以保持 dp 和布局。

## 示例与验证

- [UI 示例](../examples/ui/README.md)：基础控件、图片背包、滚动合成列表、提示框、输入、皮肤及 PNG。
- [game 示例](../examples/game/README.md)：HUD、采集计数、暂停/攻击/天气、音乐和音量、FXAA/OIT 与真实游戏状态同步。
- renderer.overlay：尺寸、索引、颜色、纹理资源引用和仓库销毁后的快照。
- renderer.ui：真实布局、控件值、回调/取消/异常、文本、DPI、拖动捕获、线程和文档身份。
- renderer.ui.opengl / renderer.ui.vulkan：真实 GPU 回读检查顺序、预乘 Alpha/sRGB、裁剪、非对称纹理方向、字体、线程和快照保留。
- 四项 renderer.ui.{opengl,vulkan}.{window,offscreen}.example：操作控件并保存真实 PNG，SDK 使用相同示例源码。

上游资料：[RmlUi](https://github.com/mikke89/RmlUi/tree/6.3)、[渲染接口](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html)、[FreeType](https://freetype.org/)。在线 RmlUi 文档可能对应更新的主分支，实际接口以本工程固定的 6.3 源码为准。

## 实时预览和调试面板

registerImage(name, RenderTexture) 注册同一 Renderer 的相机输出，不下载像素；适合小地图和角色预览。setDebugPanel 接收 RenderStats、RendererMemoryStats、AssetMemoryStats，创建只读统计文档，不抢夺输入焦点。详见[调试统计](DIAGNOSTICS.md)与[综合示例](../examples/features/README.md)。
