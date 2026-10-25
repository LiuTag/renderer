# 游戏基础 UI 示例

[文档导航](../../docs/README.md) · [UI 模块](../../docs/UI.md) · [综合游戏](../game/README.md)

renderer_ui_demo 演示 RML/RCSS 皮肤、文字/图片、按钮/禁用、进度条、复选框、音量滑条、滚动列表、输入框和提示信息。使用引擎统一 RHI，可选择 OpenGL/Vulkan、窗口/离屏及渲染线程。图片由程序生成，Lato 示例字体随 SDK 安装；全部使用公开头文件。

```powershell
cmake --preset windows -DRENDERER_BUILD_UI=ON -DRENDERER_BUILD_VULKAN=ON
cmake --build --preset windows --target renderer_ui_demo --parallel 8
build/windows/Debug/renderer_ui_demo.exe --backend vulkan --threaded
build/windows/Debug/renderer_ui_demo.exe --backend opengl --offscreen --frames 36 --self-test --output build/windows/ui.png
```

默认优先使用已构建的 Vulkan。`--hidden` 隐藏窗口，`--offscreen` 不创建应用窗口；`--frames` 限制帧数，默认窗口运行到关闭、离屏一帧；`--threaded/--single-thread` 选择渲染线程，`--self-test` 自动点击按钮/复选框/滑条、输入文本并滚动。`--output` 指定退出时 PNG，默认 ui.png；`--font` 额外加载回退字体，例如应用提供的中文字体。

主题在 [theme.rcss](theme.rcss)，布局在 [demo.rml](demo.rml)。修改文件后重新运行即可；文档相对资源以所在目录解析。界面空白区域继续留给游戏输入，控件区域占用输入。完整 IME 和系统剪贴板尚未接入，详见模块边界。

## 从已安装 SDK 独立构建

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/ui -B build/ui-sdk -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/ui-sdk --config Release --parallel 8
ctest --test-dir build/ui-sdk -C Release --output-on-failure
```

SDK 需要 ui 组件及至少一个后端；配置会从安装包取得字体路径，使用者无需安装 RmlUi、FreeType 或 Vulkan SDK。运行 GPU 示例仍需要相应驱动。RENDERER_BUILD_EXAMPLES/RENDERER_BUILD_TESTS 分别控制示例与运行检查，根工程还通过 RENDERER_BUILD_UI 控制模块。
