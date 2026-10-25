# 渲染基础能力综合示例

[示例导航](../README.md) · [异步资源](../../docs/ASSET_LOADING.md) · [材质与实时纹理](../../docs/RENDERING_EXTENSIONS.md) · [调试统计](../../docs/DIAGNOSTICS.md)

`renderer_features_demo` 同时展示这轮五项升级：

- 程序生成树木图片并后台解码，主线程按预算发布资源；加载前显示占位网格。
- 左上角性能面板显示内存、CPU/GPU 阶段、上传与复用、等待资源。
- 树木矩阵保持静态，变化只在 Shader uniform 的时间中；移动角色验证增量实例上传。
- 自定义风摆 GLSL 材质、加色光点，与 Weighted OIT 和 FXAA 共存。
- 两张 GPU 相机输出分别用于小地图和角色预览；中文名字牌、编辑框、组合输入、系统剪贴板和字形缓存重建。

```powershell
cmake --build build/windows --config Release --target renderer_features_demo --parallel 6
.\build\windows\Release\renderer_features_demo.exe --backend vulkan --threaded
.\build\windows\Release\renderer_features_demo.exe --backend opengl --offscreen --frames 120 --self-test --output build/windows/features.png --cache build/windows/features-assets
```

支持 --backend、--threaded/--single-thread、--offscreen、--hidden、--frames、--font、--output、--cache、--self-test。窗口模式默认持续运行，点击输入框测试中文，Esc 在 UI 不占用键盘时退出；关闭窗口始终可退出。每轮结束保存最后画面。

Windows 自动尝试本机 C:/Windows/Fonts/msyh.ttc，系统字体不会复制进 SDK。其他平台或字体配置通过 --font 传入包含中文的字体；未提供中文字体时，世界名字牌使用英文，UI 中的中文可能显示缺字。

自检运行至少 41 帧：第 20 帧模拟中文预编辑/提交，第 40 帧回收并预热字形缓存，结束检查资源发布、实例复用、输入值、面板位置与图形诊断。四项 CTest 检查分别覆盖两个后端的窗口/离屏，采用 90 帧。完整核心 GPU 检查另做实际像素断言；Windows 输入法测试用原生消息检查延迟事件，未向桌面发送键盘输入。

安装后的独立构建：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/features -B build/features-sdk -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/features-sdk --config Release --parallel 6
ctest --test-dir build/features-sdk -C Release --output-on-failure
```

需要 ui、text 与至少一个后端，不依赖 animation、physics2d 或 audio。代码只包含公共头文件，窗口/输入策略仍由应用层组织。
