# SDK 安装与二次开发

[返回文档导航](README.md) · [完整二次开发手册](DEVELOPMENT_GUIDE.md)

SDK 安装公共头文件、核心静态库、可选 OpenGL/Vulkan/animation/interaction/audio 静态库及完整链接依赖，并提供 `find_package(renderer CONFIG)` 配置。使用者不需要访问引擎源码、下载依赖或安装 Vulkan SDK。运行图形功能需要相应的 OpenGL 4.5 Core 或 Vulkan 1.3 驱动与显示环境。

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
cmake --preset windows -DCMAKE_INSTALL_PREFIX=C:/projects/chatgpt/renderer/build/windows/sdk
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
| `renderer::opengl` | 窗口、OpenGL Device 工厂和高层 Renderer，自动传递 core |
| `renderer::vulkan` | Vulkan Device/Renderer 工厂及交换链，自动传递 core；需在源码构建时启用 |
| `renderer::animation` | 可选方向序列帧、动画播放及事件，只依赖 core |
| `renderer::interaction` | 可选屏幕射线、地面求交、Alpha 拾取及筛选，只依赖 core |
| `renderer::audio` | 可选音乐/音效与混音，只依赖 core 公共类型；miniaudio 实现编入库内 |

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
cmake -S . -B build/core -DRENDERER_BUILD_OPENGL=OFF -DRENDERER_BUILD_VULKAN=OFF -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_ANIMATION=OFF -DRENDERER_BUILD_INTERACTION=OFF -DRENDERER_BUILD_AUDIO=OFF -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer-core
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
