# SDK 安装与二次开发

[返回文档导航](README.md) · [完整二次开发手册](DEVELOPMENT_GUIDE.md)

SDK 安装公共头文件、核心静态库、可选 OpenGL/Vulkan 后端及十二个扩展静态库及完整链接依赖，并提供 `find_package(renderer CONFIG)` 配置。使用者不需要访问引擎源码、下载依赖或安装 Vulkan SDK。运行图形功能需要相应的 OpenGL 4.5 Core 或 Vulkan 1.3 驱动；窗口呈现需要显示环境，Vulkan 离屏不需要。

[离屏渲染](OFFSCREEN.md)使用同一套后端目标，无额外模块。PNG 编码随 core 静态提供，不导出 stb 头文件或额外 PNG/zlib 库；Windows OpenGL 的 WGL pbuffer 系统库由 CMake 自动传递。安装包包含 `share/renderer/examples/offscreen` 和共用图集，可独立构建并直接输出 PNG。

完整 SDK 还安装 [game 综合应用](../examples/game/README.md)，需十二个扩展和至少一个后端。可单独配置 `share/renderer/examples/game`，无需访问引擎私有头文件或下载素材；应用调度、音频缓存、流式物件、实际 PNG/WAV 输出均有源码。

## 构建并安装到指定目录

Windows 下首次独立配置且未指定安装前缀时，SDK 默认安装到 `<构建目录>/sdk`，例如 `build/windows/sdk`。该目录可由普通用户写入；作为其他工程的子目录使用时，不改变父工程的安装前缀。显式 `-DCMAKE_INSTALL_PREFIX=...`、环境变量或 `cmake --install --prefix ...` 仍优先。

在源码根目录执行，安装目录通过标准 CMake 变量 `CMAKE_INSTALL_PREFIX` 指定：

```powershell
cmake --preset windows -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer
cmake --build --preset windows-release --parallel 6
cmake --install build/windows --config Release
```

需要 Debug 时，将相同构建目录的 Debug 配置也安装到同一前缀：

```powershell
cmake --build --preset windows --parallel 6
cmake --install build/windows --config Debug
```

也可以在安装时覆盖前缀：

```powershell
cmake --install build/windows --config Release --prefix D:/SDK/renderer
```

单配置生成器使用 `-DCMAKE_BUILD_TYPE=Release`，再执行 `cmake --build` 和 `cmake --install`。`cmake --install` 不会自动编译，需先构建对应配置。

Visual Studio 中可选择 Debug/Release、x64，然后右键根解决方案的 `INSTALL` 项目执行“生成”。它使用配置阶段保存在缓存中的安装前缀，并安装当前选择的配置；命令行等价操作如下：

```powershell
cmake --build build/windows --config Debug --target INSTALL --parallel 6
cmake --build build/windows --config Release --target INSTALL --parallel 6
```

旧构建目录可能仍缓存 `C:/Program Files/renderer`，不会因默认值修改而自动迁移。若 INSTALL 报 `file INSTALL cannot make directory`、访问拒绝或最后显示 `MSB3073`，先检查输出中最早的 CMake 错误及目标路径。可从源码根目录修改旧缓存，再在 VS 中重新加载生成的解决方案：

```powershell
cmake --preset windows -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer
```

路径按实际工程位置调整。`cmake --install ... --prefix ...` 只覆盖当次命令，不会修改 VS INSTALL 使用的缓存前缀；需要固定 VS 的安装位置时使用配置阶段的 `-D` 参数。

安装路径使用 `GNUInstallDirs`，支持修改 `CMAKE_INSTALL_INCLUDEDIR`、`CMAKE_INSTALL_LIBDIR`、`CMAKE_INSTALL_BINDIR` 和 `CMAKE_INSTALL_DATADIR`。建议使用相对目录以保持安装包可迁移。默认安装布局如下：

```text
C:/SDK/renderer/
  include/renderer/                 公共头文件
  lib/Debug/                        Debug 库及静态链接依赖
  lib/Release/                      Release 库及静态链接依赖
  lib/cmake/renderer/               Config、版本和导出 targets
  share/renderer/                   中文文档与第三方许可
```

仅安装已构建并显式指定安装的配置。配置使用独立子目录，Debug 和 Release 的同名依赖库不会互相覆盖。核心/后端头文件来自 `include/renderer`，启用的扩展头文件来自 `modules/<模块>/include/renderer`，统一安装到 SDK 的 include 目录；不安装私有或第三方开发头文件。

核心库随包提供 glslang、SPIR-V 支持和 SPIRV-Cross 所需的静态库。启用任一图形后端时还安装内部平台库、共用管线库和 GLFW；OpenGL 另有 GLAD，Vulkan 的 Volk 实现对象直接合入 `renderer_vulkan`。不要只复制引擎主库，应保留整个 SDK 目录。`renderer::_...` 目标是自动传递的实现依赖，应用只使用以下公开目标：

| 目标 | 用途 |
| --- | --- |
| `renderer::core` | 场景、资源、模型导入、快照、Shader 编译、RHI 接口及图编译 |
| `renderer::opengl` | 窗口及 Windows pbuffer 离屏 Device/Renderer 工厂，自动传递 core |
| `renderer::vulkan` | 窗口和无窗口 Device/Renderer 工厂，自动传递 core；需在源码构建时启用 |
| `renderer::animation` | 可选方向序列帧、动画播放及事件，只依赖 core |
| `renderer::interaction` | 可选屏幕射线、地面求交、Alpha 拾取及筛选，只依赖 core |
| `renderer::audio` | 可选音乐/音效与混音，只依赖 core 公共类型；miniaudio 实现编入库内 |
| `renderer::tilemap` | 稀疏瓦片地图、脏块缓存、批次发布和后台流式加载 |
| `renderer::collision` | XZ 圆/AABB 查询、连续圆形移动与滑动 |
| `renderer::particles` | CPU 固定步图片粒子与火/烟/雨/雪预设 |
| `renderer::environment` | 昼夜太阳、调色与天气强度 |
| `renderer::presentation` | Scene 精灵遮挡淡出、闪白与恢复 |
| `renderer::physics2d` | 可选 Box2D 刚体、形状、固定步、事件、查询、关节和场景同步 |
| `renderer::camera` | 可选轨道/跟随/自由控制、平滑、限幅和相机节点写回，只依赖 core |
| `renderer::ui` | 可选游戏控件、文档/皮肤、文字/图片、事件和覆盖层快照；私有 RmlUi/FreeType 自动传递 |
| `renderer::text` | 世界文字、字体/排版/图集、节点绑定、Billboard、屏幕字号和深度开关；只需 core 与私有 FreeType |

这些扩展各自只依赖 core 的公共接口。相机的输入单位、更新顺序和独立 SDK 示例见[相机控制](CAMERA.md)；地图、碰撞、粒子、环境和表现的接入见[图片世界模块](WORLD_MODULES.md)。

ui 组件安装 renderer_ui、RmlUi 和 FreeType 三个静态库，以及 RmlUi/FreeType/示例字体许可证；不安装第三方开发头文件或它们的独立 CMake 包。示例字体位于 share/renderer/assets，由 renderer_UI_FONT 提供路径，文档/RCSS 皮肤随示例源码安装。应用可提供其他字体；完整接入和文字/IME 范围见[UI 文档](UI.md)。

text 组件安装 renderer_text，FreeType 与 UI 共用同一个静态依赖，不要求 RmlUi 或 UI。renderer_TEXT_FONT 提供安装后的示例字体路径，单独构建 text 时也安装该字体与许可。字体/图集和世界节点的接口见[世界文字](TEXT.md)。

## 从外部工程接入

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES CXX)
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS core opengl)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE renderer::opengl)
if(MSVC)
    target_compile_options(my_app PRIVATE /utf-8)
endif()
```

配置应用时指定安装前缀：

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build --config Release
```

C++20 要求和头文件目录由导出目标自动传递，不需要手动添加 include 路径或枚举依赖库。也可用 `-Drenderer_DIR=C:/SDK/renderer/lib/cmake/renderer` 精确定位配置；自定义库目录时对应调整此路径。

SDK 目录可以整体复制到新位置，在新的应用构建目录中将 `CMAKE_PREFIX_PATH` 指向新位置即可。Windows 应用应使用与 SDK 一致的架构、兼容的 MSVC 工具链及运行库：默认 Debug 为 `/MDd`，Release 为 `/MD`，不要交叉混用。

## Vulkan SDK 组件

构建引擎时设置 `RENDERER_BUILD_VULKAN=ON`，需要 Vulkan 开发头文件；应用接入安装包时只请求组件和链接目标：

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS vulkan)
target_link_libraries(my_app PRIVATE renderer::vulkan)
```

应用不需要 Vulkan SDK、Volk 头文件或 Vulkan import library。通过 `GraphicsApi::Vulkan` 创建窗口，再调用 `Renderer::createVulkan()` 或 `rhi::Device::createVulkan()`。关闭 `RENDERER_BUILD_OPENGL` 可生成仅 Vulkan 的图形 SDK，该包不安装 renderer_opengl 或 GLAD；保留 core 的 Shader 工具链。完整构建和运行方法见 [Vulkan 说明](VULKAN.md)。

## 仅安装核心 SDK

```powershell
cmake -S . -B build/core -DRENDERER_BUILD_OPENGL=OFF -DRENDERER_BUILD_VULKAN=OFF -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_ANIMATION=OFF -DRENDERER_BUILD_INTERACTION=OFF -DRENDERER_BUILD_AUDIO=OFF -DRENDERER_BUILD_TILEMAP=OFF -DRENDERER_BUILD_COLLISION=OFF -DRENDERER_BUILD_PARTICLES=OFF -DRENDERER_BUILD_ENVIRONMENT=OFF -DRENDERER_BUILD_PRESENTATION=OFF -DRENDERER_BUILD_PHYSICS2D=OFF -DRENDERER_BUILD_CAMERA=OFF -DRENDERER_BUILD_UI=OFF -DRENDERER_BUILD_TEXT=OFF -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer-core
cmake --build build/core --config Debug --parallel 6
cmake --install build/core --config Debug
```

应用使用 `find_package(renderer CONFIG REQUIRED COMPONENTS core)` 并链接 `renderer::core`。该包不包含窗口依赖或图形后端；若请求必需的 `opengl` / `vulkan` 组件，CMake 会报告包不可用。事件值类型和空订阅令牌属于核心；窗口与图形工厂调用需要相应的后端组件。

通过 `-DRENDERER_INSTALL_SDK=OFF` 可以关闭本工程的 SDK 安装规则。不同后端/模块组合使用独立安装前缀；同一变体的 Debug/Release 可以共存。

## 验证已安装的 SDK

仓库提供一个独立的外部使用者工程，不通过 `add_subdirectory` 接入引擎：

```powershell
cmake -S examples/sdk_consumer -B build/sdk-consumer -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build/sdk-consumer --config Release
ctest --test-dir build/sdk-consumer -C Release --output-on-failure
```

示例会创建场景、提取快照、编译并转译 Shader、使用模型和渲染图接口，随后创建隐藏窗口，渲染和回读一帧，验证低层设备工厂及事件订阅。它从已安装包解析全部库，能够检查静态传递依赖是否完整。

SDK 中也包含此示例，默认位于 `share/renderer/examples/sdk_consumer`；没有引擎源码时，可直接将该目录作为 `cmake -S` 的参数。

源码根工程开启 `RENDERER_BUILD_EXAMPLES` 后可直接构建全部示例，不需要安装 SDK。SDK 还安装聚合的 `share/renderer/examples/CMakeLists.txt` 和共用的 `share/renderer/tests/example_tests.cmake`，可单独配置整个 examples 目录；测试注册遵守 `RENDERER_BUILD_TESTS` 总开关。复制单个示例工程到其他位置时，测试脚本通过 SDK 配置提供的 `renderer_EXAMPLE_TEST_HELPER` 定位。私有测试源码和测试资产不随 SDK 安装。

需要学习场景交互、Compute、自定义 RenderGraph 或图片游戏时，继续阅读[二次开发手册](DEVELOPMENT_GUIDE.md)和 [Sprite 专章](SPRITES.md)。独立工程安装在 `share/renderer/examples/development`，完整 SDK 提供四个可执行程序及 CTest；关闭动画或交互扩展时提供三个基础示例。Sprite 渲染保留在 core，动画和拾取为独立扩展，需显式请求组件及链接目标，见[模块说明](MODULES.md)。`examples/modules_consumer` 验证动画/交互的按需接入；`examples/audio` 独立验证音频组件，后者通过 `--offline` 不依赖声卡。音频模块的线程与分发方式见[音频说明](AUDIO.md)。

音频模块默认开启，可用 `RENDERER_BUILD_AUDIO=OFF` 裁剪；关闭时不安装音频头文件或库。完整 SDK 比先前增加一个 `renderer_audio` 静态库，miniaudio 实现对象已经合入，无需另一个音频依赖库；许可文本随 SDK 安装。

验证核心包时添加 `-DSDK_CONSUMER_OPENGL=OFF`，并使构建配置与已安装配置一致；此时不创建窗口，也不需要图形显示环境。

性能示例 `share/renderer/examples/sprite_benchmark` 可单独配置，自动链接包内存在的 OpenGL/Vulkan 组件，对比精灵数量和线程模式。它与森林示例共用相邻 `examples/common` 的图集头文件；单独复制示例时保留该目录，见[性能测试说明](SPRITE_BENCHMARK.md)。

## 头文件与注释约定

所有自有头文件使用包含相对路径的唯一宏保护：`#ifndef`、`#define`、末尾 `#endif`。第三方与生成文件保留上游格式。

文件开头的 Doxygen `@file` / `@brief` 说明文件职责，类和结构体开头的 `@brief` 说明其用途。函数声明前说明行为，每个具名参数使用 `@param` 描述含义、单位、范围和必要的生命周期约束；模板参数使用 `@tparam`。`@return` 描述返回结果、空值或失败的语义，void 和构造/析构函数明确标为无返回值。对外服务类继续使用 PImpl，值类型公开其必要字段。

## 图片世界 SDK 示例

启用五个新模块时，SDK 增加对应头文件和五个静态库；它们不引入第三方依赖。`share/renderer/examples/world` 包含可独立构建的集成场景，依赖同级 `examples/common` 的素材生成头文件，单独复制时应一并保留。构建与测试步骤见[示例说明](../examples/world/README.md)。更新含新值类型字段的 SDK 后必须重新编译应用。

流式接口 `<renderer/tilemap/streaming.hpp>` 随 tilemap 组件安装，后台线程和区块文件实现已包含在同一个 `renderer_tilemap` 库内。无需额外线程库配置或第三方头文件；core 已传递 Threads/C++20 要求。关闭 tilemap 同时关闭流式实现、头文件和测试。详情见[流式加载](STREAMING.md)。

`renderer::physics2d` 随启用配置安装公共头文件和 renderer_physics2d 静态库；私有 Box2D 静态库自动传递。完整 SDK 每个配置新增两个库，共 27 个。上游独立安装被排除，不安装 Box2D 开发头文件或第二套包配置。组件、固定版本和应用契约见[Box2D 文档](PHYSICS2D.md)。

## 渲染基础增强 SDK

AssetLoader、RenderTexture、材质效果和内存统计均随 core/后端安装，没有新增外部依赖。Windows 平台库的 IMM32/comctl32 系统链接依赖由导出目标自动传递。ui/text 仍为独立组件；features 示例要求两者，其他模块可关闭。新值类型/字段需要使用同版本头文件和库重新编译应用。详见[features SDK 示例](../examples/features/README.md)。

## 压缩纹理、数组与文字增强

核心安装 renderer/texture.hpp：DDS 读取/保存、CPU BC1/3/4/5 编码、BC1/2/3/4/5/7 解码、数组与离线 mip 无需额外组件。stb_dxt/bcdec 实现编入 renderer_core，第三方头文件不安装，许可原文随包保存；完整 SDK 仍是每配置 27 个静态库。高层数组、格式降级和 RHI 接口由已有两种后端提供。

SDF、字形 LRU 与 trimCache 属于可选 text；共享 UI 字体缓存回收与数组图片选层属于 ui。更新头文件和静态库后需重新编译应用。详见[纹理说明](TEXTURES.md)和[文字说明](TEXT.md)。SDK 的 share/renderer/examples/texture_text 可独立构建打包器及对照示例；只有 core 的包也可构建 CPU 打包器。全部示例开启后为 23 个目标、40 项示例检查。

## 性能优化接口与示例

批量传输、区域/Alpha8 纹理、场景缓存和空间索引属于 core/RHI；字形 R8/局部图集更新属于 text。新头文件 renderer/texture_region.hpp 随核心安装，暂存池没有增加第三方依赖。完整 SDK 仍每配置 27 个静态库；更新后重新编译消费者。performance 示例需要 text/interaction 和任一后端，可独立配置，详见[示例](../examples/performance/README.md)与[优化说明](OPTIMIZATION.md)。
