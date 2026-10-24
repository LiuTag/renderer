# 示例

本目录保存全部示例源码，由根目录的 `RENDERER_BUILD_EXAMPLES` 总开关控制。源码工程直接链接本次构建的引擎目标，无需先安装 SDK。

生成 Visual Studio 解决方案时，示例项目统一显示在 `examples` 解决方案文件夹中，独立配置示例目录也适用。此分组不改变可执行文件的输出位置。

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_TESTS=OFF
cmake --build --preset windows-release --parallel 6
.\build\windows\Release\renderer_sprite_demo.exe --threaded
```

从源码根目录执行上述命令。Visual Studio 生成器的可执行文件保存在构建目录的 `Debug/`、`Release/`，与原 demo 位置一致。

| 目标 | 源码 | 所需模块 |
| --- | --- | --- |
| `renderer_demo` | `demo.cpp` | opengl |
| `renderer_sprite_demo` | `development/sprite_app.cpp` | opengl、animation、interaction |
| `renderer_vulkan_demo` | `demo.cpp`，选择 Vulkan 工厂 | vulkan |
| `renderer_vulkan_sprite_demo` | `development/sprite_app.cpp`，选择 Vulkan 工厂 | vulkan、animation、interaction |
| `renderer_sprite_benchmark` | `sprite_benchmark/main.cpp` | 自动链接可用的 opengl / vulkan，不需要扩展模块 |
| `renderer_offscreen_demo` | `offscreen/main.cpp` | core 与可用的 opengl / vulkan；无应用窗口，直接保存 PNG |
| `development_scene` | `development/scene_app.cpp` | opengl |
| `development_compute` | `development/compute_app.cpp` | opengl |
| `development_graph` | `development/graph_app.cpp` | opengl |
| `development_sprite` | `development/sprite_app.cpp` | opengl、animation、interaction |
| `renderer_sdk_consumer` | `sdk_consumer/main.cpp` | core，自动使用可用的 opengl |
| `renderer_modules_consumer` | `modules_consumer/main.cpp` | core，自动使用启用的动画/交互模块 |
| `renderer_audio_demo` | `audio/main.cpp` | audio |
| `renderer_physics2d_demo` | `physics2d/main.cpp` | physics2d，无窗口物理演示 |
| `renderer_world_demo` | `world/main.cpp` | tilemap、particles、environment、presentation、physics2d 或 collision，以及一个后端 |

依赖模块未构建时，跳过对应示例。两个后端及全部九个扩展开启时有 15 个示例目标。关闭 OpenGL 仍可编译 CPU 使用者、音频/物理示例及启用的 Vulkan 示例。`renderer_sprite_demo` 和 `development_sprite` 复用同一份源码，保留原有可执行程序名称。

源码构建加 `-DRENDERER_BUILD_VULKAN=ON` 后运行 `renderer_vulkan_demo --threaded --oit --fxaa` 或 `renderer_vulkan_sprite_demo --threaded --oit`。窗口/设备创建以外的应用逻辑共用原示例，详见 [Vulkan 说明](../docs/VULKAN.md)。

音频示例默认播放两秒演示声，`--offline` 不打开声卡，`--music <文件>` 流式播放自己的音乐；详见[音频说明](../docs/AUDIO.md)。

性能测试运行 `renderer_sprite_benchmark --backend all --threads all --counts 1000,5000,10000 --atlas-images 25,100,400`，自动比较后端与线程并输出 CSV；场景口径、预热和计时范围见[精灵性能测试](../docs/SPRITE_BENCHMARK.md)。

## 安装后作为独立工程

SDK 安装本目录及测试注册脚本；`examples/` 整体或 development、sdk_consumer、modules_consumer、audio、sprite_benchmark、world、physics2d、offscreen 子目录都可单独配置。独立配置时通过 find_package 使用 SDK，例如：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples -B build/sdk-examples -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON
cmake --build build/sdk-examples --config Release --parallel 6
ctest --test-dir build/sdk-examples -C Release --output-on-failure
```

独立示例同样支持 `RENDERER_BUILD_EXAMPLES=OFF` 关闭编译、`RENDERER_BUILD_TESTS=OFF` 关闭运行检查。聚合 examples 工程的 GL/Vulkan 检查默认关闭，可分别设置 `RENDERER_GL_TESTS=ON` / `RENDERER_VULKAN_TESTS=ON`。development 和 sdk_consumer 独立工程保留原来的 GL 检查默认开启行为，可显式关闭。测试注册集中在源码 `tests/example_tests.cmake`，需要图形环境的检查只在允许相应后端测试时注册。

详细操作见[二次开发手册](../docs/DEVELOPMENT_GUIDE.md)、[图片游戏示例](../docs/SPRITES.md)和[测试说明](../tests/README.md)。

## 模块化图片世界

`renderer_world_demo` 位于 [world](world/README.md)，依赖 tilemap、particles、environment、presentation、physics2d 或 collision，以及至少一个图形后端；根工程按依赖目标是否存在决定是否构建。它展示流式地图、碰撞移动、四类粒子、昼夜天气、遮挡淡出、闪白、内描边及接触阴影，默认启用渲染线程。运行 `renderer_world_demo --backend opengl` 或 `--backend vulkan`，安装后的同名目录也可独立构建。

[physics2d](physics2d/README.md) 是无窗口物理示例。world 示例在 physics2d 存在时自动采用 Box2D，否则使用 collision。

## 离屏图片输出

[offscreen](offscreen/README.md) 使用相同森林图集，直接输出 PNG，不创建应用 Window。运行 `renderer_offscreen_demo --backend vulkan --threaded --output build/windows/forest.png`。Vulkan 路径完全跳过窗口系统；Windows OpenGL 使用 pbuffer，驱动可能保留内部隐藏 HWND。接口、颜色和平台约束见[离屏文档](../docs/OFFSCREEN.md)。
