# 测试

单元与集成测试源码、测试资产及 CMake 入口全部位于源码的 `tests/`。`CMakeLists.txt` 定义测试可执行程序，`example_tests.cmake` 统一注册示例运行检查。

生成 Visual Studio 解决方案时，测试可执行项目统一显示在 `tests` 解决方案文件夹中；用于运行检查的示例项目仍位于 `examples`。此分组不改变可执行文件的输出位置。

| 根工程开关 | 默认值 | 含义 |
| --- | --- | --- |
| `RENDERER_BUILD_TESTS` | ON | 所有测试的总开关；OFF 时不编译测试程序、不注册 CTest |
| `RENDERER_GL_TESTS` | OFF | 总开关开启时，增加窗口/OpenGL 测试及需要图形环境的示例检查 |
| `RENDERER_VULKAN_TESTS` | OFF | 总开关开启且 Vulkan 后端启用时，增加 Vulkan GPU/窗口/示例检查；两种后端都有时增加图像对比 |
| `RENDERER_BUILD_EXAMPLES` | ON | 示例编译开关；关闭时只保留单元/集成测试，不注册示例检查 |

## 编译测试

从源码根目录执行：

```powershell
# 只编译 CPU 测试，不编译示例。
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_TESTS=ON -DRENDERER_GL_TESTS=OFF -DRENDERER_VULKAN_TESTS=OFF
cmake --build --preset windows --parallel 6
ctest --preset windows

# 示例、单元测试及真实 OpenGL 检查一起构建。
cmake --preset windows-gpu-tests -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_TESTS=ON -DRENDERER_GL_TESTS=ON
cmake --build --preset windows-gpu-tests --parallel 6
ctest --preset windows-gpu-tests
```

仅构建库：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_TESTS=OFF
cmake --build --preset windows-release --parallel 6
```

同时检查两种后端：

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build --preset windows --parallel 6
ctest --preset windows --output-on-failure
```

`RENDERER_BUILD_TESTS=OFF` 优先于两个 GPU 开关，即使旧缓存中 GPU 测试为 ON，也不会编译或注册任何测试；重新配置会清空旧测试清单。关闭测试不会删除已经生成的可执行文件，它们不再属于当前构建目标。

完整模块的测试本体为七个 CPU 套件（包含无声卡音频混音）和六个 OpenGL/窗口套件。仅启用 OpenGL、全部扩展及示例时，包含性能示例在内共二十一项 CTest。两种后端都启用时，另有五个 Vulkan 套件、一个跨后端对比及 Vulkan 示例，当前共三十项。图形检查需要真实驱动与桌面环境，全屏和性能示例检查串行执行。

`graphics_fixture.hpp` 让四组底层/高级/FXAA/Sprite 测试复用同一份断言，按编译宏创建不同后端。`vulkan_window_tests.cpp` 验证交换链、回读、缩放、全屏和专用线程；`backend_parity_tests.cpp` 对同一快照的绘制统计和像素进行比较。Vulkan-only 预设及验证层行为见 [Vulkan 说明](../docs/VULKAN.md)。

音频模块关闭时不注册 `renderer.audio` 或 `renderer.audio.example`；启用后它们不依赖 `RENDERER_GL_TESTS`，运行示例时显式传入 `--offline`。覆盖与线程约定见[音频说明](../docs/AUDIO.md)。

性能示例检查为 `renderer.benchmark.opengl` / `renderer.benchmark.vulkan`，每项覆盖两个数量、两种图集规模和两种线程，核对真实图像、可见精灵数量及错误。它们串行运行且不设置 FPS 门槛，见[性能说明](../docs/SPRITE_BENCHMARK.md)。

SDK 安装示例及本目录的 README、示例注册脚本，不安装私有测试源码、测试资产或测试可执行文件。独立 SDK 示例支持同样的测试开关。详细范围见[验证与排错](../docs/VALIDATION.md)。
