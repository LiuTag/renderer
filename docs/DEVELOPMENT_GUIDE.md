# Renderer SDK 二次开发指南

本文面向使用**已安装 SDK**开发三维应用、图片风格游戏、模型查看器及专用 GPU 工具的 C++ 开发者，以 **0.2.0 / 2026-10-26 / C++20 / OpenGL 4.5 Core / Vulkan 1.3** 的公共接口为准。可以只拿安装目录完成接入，无需访问引擎私有头文件或下载第三方开发库。

SDK 默认将本指南安装到 **`<SDK>/share/renderer/docs/DEVELOPMENT_GUIDE.md`**，文档首页为 `<SDK>/share/renderer/README.md`；全部专章、示例源码和许可一起安装，本文的相对链接在源码树和完整 SDK 中均可使用。若供应方修改 `CMAKE_INSTALL_DATADIR`，以其实际目录为准。`find_package` 还提供 `renderer_DOCUMENTATION_DIR` 和 `renderer_DEVELOPMENT_GUIDE` 供工具定位文档。

[文档导航](README.md) · [SDK 安装与目录详解](SDK.md) · [接口速查](API_GUIDE.md) · [功能边界](../IMPLEMENTATION.md) · [验证记录](VALIDATION.md)

完成指南后，应能配置独立 CMake 工程、绘制并保存第一帧、管理场景/资源/模块、组织应用更新与渲染提交，以及排查线程、链接、颜色、字体和性能问题。基础使用路线不要求掌握原生 OpenGL/Vulkan；底层调用可另读[双后端源码导读](RENDERING_WALKTHROUGH.md)。

## 阅读路线

| 开发目标 | 阅读位置与完整参考 |
| --- | --- |
| 安装后第一次接入，窗口/离屏取得图片 | 第 1–3 节；第 2.2 节完整 CMake + 第 3.4 节完整 main.cpp |
| 场景、变换、资源和模型查看器 | 第 4–8 节，以及 [development 示例](../examples/development/CMakeLists.txt) |
| 生命周期、独立渲染线程和提交结果 | 第 9 节；不可变快照与 GPU 完成语义 |
| 自定义 Shader、Compute 和完整渲染图 | 第 10–12 节；development_compute / development_graph |
| 性能、显存、Vulkan 提交复用与错误排查 | 第 13–14 节及 [诊断](DIAGNOSTICS.md)、[优化](OPTIMIZATION.md) |
| SDK 业务扩展与引擎源码修改边界 | 第 15 节；源码修改只属于可选进阶路线 |
| 后台加载、包卸载、DDS 与纹理数组 | 第 16 节及 [features](../examples/features/README.md)、[texture_text](../examples/texture_text/README.md) |
| Sprite、Billboard、动画、交互和批次 | 第 17 节及 [Sprite](SPRITES.md) |
| 相机、中文世界文字和 UI | 第 18–19 节及 [camera](CAMERA.md)、[text](TEXT.md)、[ui](UI.md) |
| 地图、流式加载、物理、粒子、环境和音频 | 第 20–21 节及 [game 综合应用](../examples/game/README.md) |
| 小地图/预览、应用架构和交付打包 | 第 22–24 节 |

除第 2.2 节和第 3.4 节标为完整工程的代码外，后续短代码块是局部用法，默认已有 `scene`、`assets`、`engine`、`view` 等对象并使用 `using namespace renderer;`；变量前提和所需组件在各节说明。业务保存、导航、AI 和玩法由应用定义。

## 1. 开发方式与能力边界

### 1.1 选择接入层级

| 方式 | 应用负责什么 | 引擎负责什么 | 适用情形 |
| --- | --- | --- | --- |
| 高层 `Renderer` | 创建资源、修改 Scene、选择 RenderView、提交快照 | 裁剪、排序、合批、光照、阴影、透明、ToneMap、GPU 缓存和同步 | 大多数三维应用和模型查看器 |
| `rhi::Device` + `CommandEncoder` | 定义 Shader、管线、资源、命令及访问屏障 | 原生 API 映射、命令验证、完成标记及延迟销毁 | Compute、专用绘制、已有渲染框架接入 |
| RHI + `RenderGraph` | 声明 Pass、完整访问关系和命令回调 | 依赖分析、无效 Pass 剔除、屏障和临时资源复用 | 多阶段绘制、后处理、自定义渲染流水线 |

同一窗口在同一时间只能绑定一个高层 Renderer 或一个低层 Device。高层 Renderer 不公开其内部 Device 和渲染图；公开的 RenderTexture 可作为独立实时颜色输出，但不暴露原生 GPU 句柄，SDK 当前也没有向高层流水线注册任意 Pass 的接口。需要完全控制 Shader 或增加绘制阶段时，可以独立使用 RHI/RenderGraph，或者按第 15 节修改引擎源码。

### 1.2 公共模块与头文件

| 头文件 | 主要类型与用途 |
| --- | --- |
| `renderer/math.hpp`、`handle.hpp` | 向量、矩阵、包围盒、带代际的类型化句柄 |
| `renderer/assets.hpp` | CPU 网格、纹理、材质与 AssetStore |
| `renderer/scene.hpp` | Scene、节点及网格、精灵、排序组、相机与灯光组件 |
| `renderer/sprite.hpp`、`sprite_snapshot.hpp` | 核心图片资源/图集及只读视图渲染数据 |
| `renderer/animation/sprite_animation.hpp` | 可选 animation 模块的方向序列帧及事件 |
| `renderer/interaction/picking.hpp` | 可选 interaction 模块的射线、地面与 Alpha 拾取 |
| `renderer/audio/audio.hpp` | 可选 audio 模块的音频资源、播放句柄、混音分组及监听器 |
| `renderer/model.hpp` | 模型模板、实例化结果，调用 loadModel 后使用完整 Model 类型时包含 |
| `renderer/render_snapshot.hpp` | RenderExtractor、不可变 RenderSnapshot |
| `renderer/renderer.hpp` | 高层 Renderer、视图、统计、截图 |
| `renderer/events.hpp`、`window.hpp` | 事件数据、订阅令牌和窗口 |
| `renderer/shader.hpp` | GLSL 编译、SPIR-V、反射和绑定映射 |
| `renderer/rhi.hpp` | GPU 资源、Pipeline、Device 与命令录制 |
| `renderer/render_graph.hpp` | 图资源、Pass 声明、编译与执行 |
| `renderer/asset_loader.hpp`、`texture.hpp` | 异步资源、发布预算、DDS/BC 压缩、mip 与数组工具 |
| `renderer/render_texture.hpp`、`overlay.hpp` | 同渲染器实时输出纹理及不可变屏幕覆盖层 |
| `renderer/camera/camera_controller.hpp` | 可选 camera：Orbit/Follow/Free 相机 |
| `renderer/text/text.hpp` | 可选 text：中文/回退、位图/SDF、世界文字 |
| `renderer/ui/ui.hpp` | 可选 ui：RML/RCSS、控件、输入路由、覆盖层 |
| `renderer/tilemap/tilemap.hpp`、`tilemap/streaming.hpp` | 可选 tilemap：瓦片、分块、后台读取与保存 |
| `renderer/collision/collision.hpp`、`physics2d/physics2d.hpp` | 可选轻量查询/移动，以及独立 Box2D 刚体 |
| `renderer/particles/particles.hpp` | 可选固定步图片粒子及天气/火烟预设 |
| `renderer/environment/environment.hpp`、`presentation/presentation.hpp` | 可选昼夜天气与遮挡淡出/闪白 |

对外服务类使用 PImpl，数学类型、句柄和描述结构是普通值类型。PImpl 隐藏实现依赖，但公共接口仍使用 `std::string`、容器、future 等 C++ 类型；应使用与 SDK 兼容的编译器、架构和运行库，不把它当成跨语言 C ABI。

### 1.3 组件选择和能力边界

核心提供场景、资源、静态模型、快照、Sprite、材质/光照、OIT/FXAA、异步资源、纹理工具、RHI 与 RenderGraph。OpenGL/Vulkan 是单独后端；animation、interaction、audio、tilemap、collision、particles、environment、presentation、physics2d、camera、ui、text 为十二个可选组件。

组件存在取决于 SDK 制作时的构建开关。`find_package(renderer CONFIG REQUIRED COMPONENTS ...)` 对缺失组件立即失败；`OPTIONAL_COMPONENTS` 可用于明确的应用降级。扩展库之间没有强制链接依赖，应用负责模块更新与事件联动；core 和图形后端不会反向链接全部扩展。

现有相机控制、UI、世界文字、流式世界和二维物理都可以直接通过公共 SDK 使用。三维模型骨骼/蒙皮、通用 ECS、导航/AI、通用场景文件和业务存档格式、完整三维物理、精确逐像素透明，以及原生窗口句柄嵌入尚不属于当前能力。高层支持材质函数和 RenderTexture，但没有向内部 RenderGraph 注册任意 Pass 的稳定接口。

## 2. 从安装目录接入并运行

### 2.1 先确认 SDK 与工具链

假定完整 SDK 在 `C:/SDK/renderer`，应用源码在另一个目录 `my_app`。使用 CMake 3.25+、兼容的 C++20 工具链；本机交付验证为 Windows x64 / Visual Studio 2022 / MSVC。SDK 自带静态传递依赖；消费者不需要下载 GLFW/FreeType/Box2D、安装 Vulkan SDK 或包含 VMA 头文件。运行时仍需要相应图形驱动；窗口模式需要桌面环境，Windows OpenGL 离屏依赖 WGL pbuffer。

```text
C:/SDK/renderer/
  include/renderer/                       公共 API 与启用的扩展头文件
  lib/Release/                           Release 库及静态传递依赖
  lib/Debug/                             安装 Debug 后才存在
  lib/cmake/renderer/rendererConfig.cmake  find_package 入口
  share/renderer/README.md                文档首页
  share/renderer/docs/DEVELOPMENT_GUIDE.md 本指南
  share/renderer/examples/                可独立构建的示例源码
  share/renderer/assets/                  已启用文字/UI 的示例字体
  share/renderer/third_party/licenses/    许可证
```

SDK 只有安装过的配置与组件。应用需匹配 x64/x86、Debug/Release、编译器与运行库；Windows 默认 Debug 为 `/MDd`，Release 为 `/MD`。公共类型使用标准库容器和 future，不承诺跨编译器 ABI，也不要混用升级前后的头文件、库和对象文件。只提供 Release 的 SDK，应先用 Release 构建示例；需要 Debug 时请供应方补装对应库。

### 2.2 创建自己的 CMake 工程（完整文件）

目录只需要：

```text
my_app/
  CMakeLists.txt
  main.cpp
```

将下列完整内容保存为 `CMakeLists.txt`，`main.cpp` 使用第 3.4 节的完整程序：

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES CXX)
set(APP_BACKEND "opengl" CACHE STRING "opengl or vulkan")
set_property(CACHE APP_BACKEND PROPERTY STRINGS opengl vulkan)
if(NOT APP_BACKEND STREQUAL "opengl" AND NOT APP_BACKEND STREQUAL "vulkan")
    message(FATAL_ERROR "APP_BACKEND must be opengl or vulkan")
endif()
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS core ${APP_BACKEND})
add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_compile_definitions(my_app PRIVATE APP_USE_VULKAN=$<STREQUAL:${APP_BACKEND},vulkan>)
target_link_libraries(my_app PRIVATE renderer::${APP_BACKEND})
if(MSVC)
    target_compile_options(my_app PRIVATE /utf-8)
endif()
message(STATUS "Renderer guide: ${renderer_DEVELOPMENT_GUIDE}")
```

从 `my_app` 目录配置 OpenGL 应用：

```powershell
cmake -S . -B build/opengl -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DAPP_BACKEND=opengl
cmake --build build/opengl --config Release --parallel 6
.\build\opengl\Release\my_app.exe --frames 60 --capture output/opengl.png
```

若 SDK 已含 Vulkan，用同一份应用源码创建另一构建目录：

```powershell
cmake -S . -B build/vulkan -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DAPP_BACKEND=vulkan
cmake --build build/vulkan --config Release --parallel 6
.\build\vulkan\Release\my_app.exe --offscreen --frames 4 --capture output/vulkan.png
```

带空格的安装路径使用 `-DCMAKE_PREFIX_PATH="D:/SDK/Renderer SDK"`。也可将 `renderer_DIR` 指向 `<SDK>/lib/cmake/renderer`；自定义安装 lib 目录时按实际位置调整。单配置生成器需设置 `-DCMAKE_BUILD_TYPE=Release`，可执行文件通常直接在构建目录而不在 Release 子目录。

只使用 CPU 场景/资源/模型/Shader/图编译时，请求 core 并链接 renderer::core。需要可选能力时显式申请与链接，例如：

```cmake
# 局部片段：在自己的应用 CMake 中增加所需组件。
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS opengl camera text
    OPTIONAL_COMPONENTS audio)
target_link_libraries(my_app PRIVATE renderer::opengl renderer::camera renderer::text)
if(TARGET renderer::audio)
    target_link_libraries(my_app PRIVATE renderer::audio)
    target_compile_definitions(my_app PRIVATE APP_HAS_AUDIO=1)
endif()
```

关闭组件意味着对应 API 不可调用；需要用条件编译分隔可选代码，不能只判断 TARGET 后仍无条件编译调用。使用 REQUIRED 时 SDK 缺失组件即配置失败，这是最早的部署检查。

不要手工枚举静态依赖，不要直接链接 `renderer::_...` 内部目标或把 SDK 的 lib 目录全加进链接器。CMake 目标已经传递头文件、C++20、线程与平台库。`RENDERER_BUILD_*` 是引擎制作/示例构建开关，消费者设置它们不会向一个裁剪过的 SDK 增加缺失库。

### 2.3 运行安装包内的完整示例

安装包的示例是可编辑的 SDK 消费源码。单独构建到应用工作目录，避免把构建产物写回 SDK：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples -B build/sdk-examples -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/sdk-examples --config Release --parallel 6
ctest --test-dir build/sdk-examples -C Release --output-on-failure
```

只开启本机支持且 SDK 已提供的后端检查。聚合示例的 GPU 检查默认关闭；`RENDERER_BUILD_EXAMPLES=OFF` 关闭示例，`RENDERER_BUILD_TESTS=OFF` 不注册运行检查。安装包不携带引擎私有单元测试源码，现有 SDK 聚合示例完整配置为 40 项运行检查；以本次 `ctest -N` 为准。

| 子目录 | 用途和所需组件 |
| --- | --- |
| [development](../examples/development/CMakeLists.txt) | OpenGL 场景/输入、Compute、自定义图及 Sprite 四个教学程序；Sprite 另需 animation/interaction |
| [offscreen](../examples/offscreen/README.md) | 一个后端即可，森林 PNG；Vulkan 路径无窗口 |
| [camera](../examples/camera/README.md)、[physics2d](../examples/physics2d/README.md) | 只需对应模块的 CPU 示例 |
| [audio](../examples/audio/main.cpp) | 对应模块；`--offline` 不打开声卡 |
| [text](../examples/text/README.md)、[ui](../examples/ui/README.md) | 对应模块与后端，包含字体、窗口/离屏和输入接入 |
| [texture_text](../examples/texture_text/README.md) | core 的 DDS 打包器；可视对照另需 text 与后端 |
| [features](../examples/features/README.md) | ui/text 与后端；异步资源、调试面板、材质效果及小地图 |
| [world](../examples/world/README.md) | 图片地图、流式/碰撞、粒子、环境、表现与一个后端 |
| [game](../examples/game/README.md) | 十二模块与一个后端，完整应用调度、HUD、音画和存档联动 |
| [performance](../examples/performance/README.md) | text/interaction 与后端，六负载性能 CSV 与参考开关 |

这些目录也可各自作为 CMake 的 `-S` 输入，参照各目录 README。只有 Vulkan 的 SDK 不提供依赖 OpenGL 的 development 四程序，应从 offscreen/features/game 等双后端示例入手。完整示例清单见[示例导航](../examples/README.md)。

### 2.4 仅供 SDK 制作者：从源码安装

已经拿到 SDK 的读者可跳过本节。制作双后端 SDK 时，供应方需在源码根目录准备 Vulkan 开发头文件及构建依赖：

```powershell
cmake --preset windows -DRENDERER_BUILD_OPENGL=ON -DRENDERER_BUILD_VULKAN=ON -DRENDERER_INSTALL_SDK=ON -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer
cmake --build build/windows --config Release --parallel 6
cmake --install build/windows --config Release
cmake --build build/windows --config Debug --parallel 6
cmake --install build/windows --config Debug
```

安装完整目录后，可通过 `share/renderer/README.md` 导航到本指南。VS INSTALL 的前缀来自配置缓存，`cmake --install --prefix` 仅覆盖当次安装；构建开关、离线依赖、GNUInstallDirs、自定义 lib/share 目录及升级规则见 [SDK 专章](SDK.md)。

## 3. 创建第一帧与应用主循环

### 3.1 对象创建顺序

```cpp
Window window({.width = 960, .height = 640, .title = "My application"});
AssetStore assets;
Scene scene;
RenderExtractor extractor;
auto engine = Renderer::createOpenGL(window, {.renderThread = true});
```

Window 在应用主线程创建，事件收集、窗口操作和高层 Renderer 调用也放在该线程。`renderThread=true` 将底层上下文交给内部工作线程，应用不需要自行调用 OpenGL。

建议让 Renderer 先于 Window 析构。资源仓库和场景由业务逻辑维护，快照持有已经解析的不可变资源数据，具体所有权见第 9 节。

### 3.2 一帧的应用侧顺序

```cpp
for (;;) {
    window.pollEvents();
    if (window.shouldClose())
        break;
    const auto [width, height] = window.framebufferSize();
    if (width <= 0 || height <= 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
    }
    // 1. 根据输入和时间步更新业务状态。
    // 2. 使用 Scene 接口写入节点变换与组件。
    const auto snapshot = extractor.extract(scene, assets);
    const auto stats = engine.render(snapshot, view);
    engine.present();
}
engine.waitIdle();
```

上例的等待需要 `<thread>`、`<chrono>`。在 `pollEvents()` 后检查关闭标记，允许关闭订阅者先调用 `cancelClose()`。最小化时尺寸可能为零，应用可以跳过绘制并适当等待，避免空转。

`extract()` 解析当前场景和资源；它不会执行 GPU 命令。`render()` 完成 CPU 帧准备和 GPU 命令提交；它不等于 GPU 执行结束。`present()` 才交换窗口缓冲。窗口路径不要漏掉相应步骤；离屏不调用 present，renderToImage 会在内部连续执行绘制和回读。

### 3.3 数学与坐标约定

| 项目 | 约定 |
| --- | --- |
| 坐标系 | 右手系，应用通常使用 Y 向上 |
| 矩阵 | 列主序、列向量，`A * B` 表示先 B 后 A |
| 角度 | 弧度；用 `radians(45)` 从度转换 |
| 节点变换 | 有限仿射矩阵，世界矩阵 = 父世界矩阵 × 局部矩阵 |
| 相机、方向光、聚光灯方向 | 节点局部 -Z |
| 投影 NDC 深度 | [0,1]，通常 LESS，清除深度为 1 |
| 手工上传纹理 | 左下原点、紧密排列 RGBA8 |
| 输出回读图像 | 左上原点 RGBA8 |
| 场景长度 | 应用自行选择一致单位；引擎不做通用自动单位换算 |

`Mat4::transformPoint()` 直接取矩阵乘积的 XYZ，不进行齐次除法，适合仿射变换。需要屏幕投影时，使用 `Vec4 clip = projection * view * Vec4{...}`，自行检查 w、裁剪范围并进行透视除法。

### 3.4 可直接编译的窗口/离屏程序（完整 main.cpp）

此程序只链接第 2.2 节选择的一个后端，不依赖扩展、外部模型或字体。创建立方体、棋盘地面和相机，支持窗口、隐藏窗口、离屏、固定帧数与 PNG；省略 `--frames` 时窗口持续运行，离屏/隐藏模式默认运行一帧。

```cpp
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <renderer/renderer.hpp>
#include <renderer/window.hpp>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char **argv) {
    using namespace renderer;
    try {
        bool offscreen = false, hidden = false;
        int frameLimit = 0;
        std::filesystem::path capture;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() {
                if (++i >= argc)
                    throw std::invalid_argument("Missing option value");
                return std::string(argv[i]);
            };
            if (arg == "--offscreen")
                offscreen = true;
            else if (arg == "--hidden")
                hidden = true;
            else if (arg == "--frames") {
                frameLimit = std::stoi(value());
                if (frameLimit <= 0)
                    throw std::invalid_argument("--frames must be positive");
            } else if (arg == "--capture")
                capture = value();
            else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        if ((offscreen || hidden) && frameLimit == 0)
            frameLimit = 1;
#if APP_USE_VULKAN
        constexpr auto api = GraphicsApi::Vulkan;
#else
        constexpr auto api = GraphicsApi::OpenGL;
#endif
        // Window 必须比 Renderer 活得更久；离屏模式不创建应用 Window。
        std::unique_ptr<Window> window;
        if (!offscreen)
            window = std::make_unique<Window>(WindowConfig{.width = 960,
                                                           .height = 640,
                                                           .title = "SDK application",
                                                           .visible = !hidden,
                                                           .vsync = !hidden,
                                                           .graphicsApi = api});
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        RendererConfig config;
        config.renderThread = true;
        config.assetUploadBudgetBytes = 0; // 单帧教学截图；实际游戏可保留默认软预算。
        auto engine = [&]() {
#if APP_USE_VULKAN
            if (window)
                return Renderer::createVulkan(*window, config);
            return Renderer::createVulkanOffscreen({960, 640}, config);
#else
            if (window)
                return Renderer::createOpenGL(*window, config);
            return Renderer::createOpenGLOffscreen({960, 640}, config);
#endif
        }();
        const auto checker = assets.createTexture(geometry::checkerboard());
        const auto material = assets.createMaterial(
            {.baseColor = {.85f, .35f, .12f, 1}, .metallic = .2f, .roughness = .4f});
        const auto groundMaterial =
            assets.createMaterial({.baseColorTexture = checker, .roughness = .9f});
        const auto cube = scene.createNode(scene.root(), "Cube");
        scene.setComponent(cube, MeshRenderer{assets.createMesh(geometry::cube()), {material}});
        const auto ground = scene.createNode(scene.root(), "Ground");
        scene.setComponent(ground,
                           MeshRenderer{assets.createMesh(geometry::plane(8)), {groundMaterial}});
        scene.setLocalTransform(ground, Mat4::translation({0, -.55f, 0}));
        const auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{}); // aspectRatio=0 自动使用目标像素比例。
        scene.setLocalTransform(camera, *Mat4::lookAt({3, 2, 5}, {0, 0, 0}).inverse());
        RenderView view;
        view.cameraNode = camera;
        view.toneMapping = ToneMapping::ACES;
        view.antiAliasing = AntiAliasing::FXAA;
        int frame = 0;
        while (frameLimit == 0 || frame < frameLimit) {
            if (window) {
                window->pollEvents();
                if (window->shouldClose())
                    break;
                const auto [w, h] = window->framebufferSize();
                if (w <= 0 || h <= 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }
            scene.setLocalTransform(cube, Mat4::rotationY(float(frame) / 60.f));
            const auto snapshot = extractor.extract(scene, assets);
            const auto stats = engine.render(snapshot, view);
            (void)stats;
            if (window)
                engine.present();
            ++frame;
        }
        if (frame > 0 && !capture.empty())
            engine.readback().savePng(capture);
        if (engine.debugErrorCount())
            throw std::runtime_error("Graphics debug errors");
        std::cout << engine.deviceInfo().backend << ": " << frame << " frames\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

本例用固定的每帧角度便于截图复现；实际交互应用按单调时钟计算 dt，使用它推进动画/控制器，物理则由固定步系统消费。关闭阴影或透明/FXAA 只需修改 RenderView；切换图形 API 需要重新创建匹配的窗口与 Renderer，不能对现有窗口原地改 API。

```powershell
# OpenGL 隐藏窗口、OpenGL 离屏，以及 Vulkan 离屏示例。
.\build\opengl\Release\my_app.exe --hidden --frames 4 --capture output/gl-window.png
.\build\opengl\Release\my_app.exe --offscreen --frames 4 --capture output/gl-offscreen.png
.\build\vulkan\Release\my_app.exe --offscreen --frames 4 --capture output/vk-offscreen.png
```

PNG 使用显示编码、左上行序；不应手工再上下翻转或做一次 Gamma 编码。`readback()` 同步等待所需 GPU 工作，宜按截图请求触发。Windows OpenGL pbuffer 初始化会使用引导窗口，Vulkan 离屏完全跳过窗口/Surface/Swapchain；详细约束见[离屏文档](OFFSCREEN.md)。

## 4. 构建场景与组织节点

### 4.1 创建层级和组件

```cpp
const auto vehicle = scene.createNode(scene.root(), "Vehicle");
const auto body = scene.createNode(vehicle, "Body");
const auto mesh = assets.createMesh(geometry::cube());
const auto material = assets.createMaterial({.baseColor = {0.2f, 0.5f, 0.9f, 1}});
scene.setComponent(body, MeshRenderer{mesh, {material}});
scene.setLocalTransform(vehicle, Mat4::translation({3, 0, 0}));
scene.setLocalTransform(body, Mat4::translation({0, 1, 0}) * Mat4::scaling({2, 1, 1}));
```

业务分组节点不需要 MeshRenderer。移动 vehicle 会带动其全部后代；同一个节点可以同时有网格、相机和灯光组件。根节点固定存在，禁止删除或换父节点。

节点名称可重复，适合显示和诊断，不是全局唯一键。SDK 当前不提供按名称搜索、枚举子节点或序列化场景树的公共接口；应用应维护自己的业务 ID 到 NodeHandle 的映射，以及需要的层级索引。

### 4.2 修改组件必须写回

```cpp
if (auto renderer = scene.component<MeshRenderer>(body)) {
    renderer->castShadow = false;
    renderer->layerMask = 0x02;
    scene.setComponent(body, *renderer);
}
scene.removeComponent<Light>(body);
```

`component<T>()` 返回 `optional<T>` 副本。只修改副本不会影响 Scene。`setComponent<T>()` 支持 MeshRenderer、SpriteRenderer、SortingGroup、Camera 和 Light；任意自定义 T 不是运行时扩展点，当前会在编译时报错。

### 4.3 局部与世界变换

组合平移、旋转、缩放时按右侧先执行：

```cpp
scene.setLocalTransform(body,
    Mat4::translation({0, 1, 0}) * Mat4::rotationY(radians(30)) * Mat4::scaling({2, 1, 1}));
const auto bodyWorld = scene.worldTransform(body);
scene.reparent(body, scene.root(), ReparentMode::KeepWorld);
```

`KeepLocal` 保留局部矩阵，世界姿态随新父节点改变。`KeepWorld` 计算 `inverse(newParentWorld) * oldWorld`，保留世界姿态及可能的剪切；新父节点世界矩阵不可逆时抛异常，应用应处理失败。

将一个目标世界姿态写入任意父节点下的对象时，也使用这个换算，而不是直接把世界矩阵传给 `setLocalTransform()`。

### 4.4 可见性、图层与删除

`setVisible(node, false)` 隐藏节点及全部后代，包括相机和灯光。`effectiveVisible(node)` 返回合并祖先后的结果。相机 `cullingMask` 与实例 `layerMask` 按位相与非零才允许进入该视图；例如主视图使用 0x01，辅助对象使用 0x02。

`destroySubtree(node)` 删除整棵子树，随后旧句柄失效。句柄的 `if (handle)` 只检查非空，不能检测删除后的失效；节点用 `scene.valid(handle)` 检查，资源由所属仓库/设备的接口验证。句柄包含进程内 owner 和代际，不适合作为跨运行保存的资产 ID。

## 5. 网格、纹理和材质

### 5.1 手工创建网格

```cpp
MeshDesc triangle;
triangle.vertices = {
    {{-1, -1, 0}, {0, 0, 1}, {0, 0}},
    {{ 1, -1, 0}, {0, 0, 1}, {1, 0}},
    {{ 0,  1, 0}, {0, 0, 1}, {0.5f, 1}},
};
triangle.indices = {0, 1, 2};
triangle.submeshes = {{.firstIndex = 0, .indexCount = 3, .materialSlot = 0}};
const auto triangleMesh = assets.createMesh(std::move(triangle));
```

网格使用三角形列表和 uint32 索引；每三个索引组成三角形。顶点位置、法线、UV 须有限，索引须落在顶点范围内。未指定 submesh 时自动补一个覆盖全部索引、材质槽为 0 的 submesh。

`firstIndex` 是索引下标，不是字节偏移；`indexCount` 须为正且是 3 的倍数。一个 submesh 对应一个材质槽，当前槽编号小于 64。MeshRenderer 的 materials 必须覆盖网格用到的槽编号，缺失时提取快照会失败。

内置 `cube()` 是边长 1 的立方体，`sphere()` 是直径 1 的球体，`plane(size)` 位于 XZ 平面且法线朝 +Y。需要面向相机的 XY 面板时，应创建对应顶点或变换网格。

### 5.2 图片与颜色空间

```cpp
const auto colorTexture = assets.loadTexture("assets/albedo.png", true);
const auto normalTexture = assets.loadTexture("assets/normal.png", false);
MaterialDesc surface;
surface.baseColorTexture = colorTexture;
surface.normalTexture = normalTexture;
surface.roughness = 0.6f;
const auto surfaceMaterial = assets.createMaterial(surface);
```

| 数据 | 应采用的空间/通道 |
| --- | --- |
| baseColor、emissive、灯光颜色、clearColor | 线性 RGB；不要先手工编码成 sRGB |
| 基色和自发光图片 | 通常 `srgb=true`，由采样器自动解码 |
| 法线图片 | `srgb=false`，切线空间法线数据 |
| metallicRoughnessTexture | `srgb=false`，G=粗糙度，B=金属度 |
| occlusionTexture | `srgb=false`，R=环境遮蔽 |
| Alpha | 线性覆盖/混合因子，不按 sRGB 转换 |

默认未压缩、单层 RGBA8 TextureDesc 的 pixels 字节数为 `width * height * 4`；Alpha8、数组、显式 mip 和 BC 使用各自载荷规则，可用 texture::validate 检查。尺寸为正且每边不超过 16384。`loadTexture()` 会把常见图片解码并转换为左下原点；模型导入遵循模型 UV 与图片的配套约定，不要再给 glTF 的纹理统一额外翻转。

滤波和寻址在 `TextureDesc.sampling` 中设置。手工创建不需要 mip 的纹理时设置 `mipmapped=false`；颜色与数据纹理的 srgb 选择比过滤方式更先影响结果正确性。

### 5.3 材质参数与不可变修改

| MaterialDesc 字段 | 含义 |
| --- | --- |
| baseColor | 线性基础 RGBA，Alpha 范围 [0,1] |
| metallic、roughness | 金属度和粗糙度系数，范围 [0,1] |
| normalScale | 法线纹理扰动强度，非负 |
| occlusionStrength | AO 强度，范围 [0,1] |
| emissive | 非负线性自发光 RGB |
| unlit | 跳过常规光照的无光照材质 |
| alphaMode、alphaCutoff | 不透明、裁剪或混合；裁剪阈值范围 [0,1] |
| doubleSided | 是否关闭背面剔除 |

资源创建后不可变，SDK 没有 `setMaterialColor()` 之类的就地修改接口。应用保存自己的描述副本，创建替代资源，再更新实例组件：

```cpp
auto materialDescription = surface;
materialDescription.baseColor = {0.1f, 0.8f, 0.3f, 1};
const auto replacement = assets.createMaterial(materialDescription);
if (auto component = scene.component<MeshRenderer>(body)) {
    component->materials.at(0) = replacement;
    scene.setComponent(body, *component);
}
```

确认其他节点、模型模板或未来提取不再需要旧句柄后，才能销毁旧材质。已提交快照继续使用旧资源；不要每一帧无条件创建同样的网格、纹理或材质，否则会不断产生新 CPU 身份和 GPU 缓存项。

## 6. 导入模型与应用业务对象

```cpp
#include <renderer/model.hpp>
auto model = assets.loadModel("assets/scene.glb");
for (const auto &warning : model.warnings())
    std::cout << warning << '\n';
const auto first = model.instantiate(scene, scene.root(), "First model");
const auto second = model.instantiate(scene, scene.root(), "Second model");
scene.setLocalTransform(second.root, Mat4::translation({4, 0, 0}));
if (!first.cameras.empty())
    view.cameraNode = first.cameras.front();
```

图片和模型路径默认相对于程序当前工作目录，不是相对于可执行文件或 SDK 安装目录。应用应通过命令行、配置或自己的资产根目录组织路径；模型引用的外部文件由导入器相对于模型文件目录解析。

Model 是可复制的静态模板，实例化时创建独立节点并共享网格/材质句柄。`ModelInstance` 保存 root、nodes、cameras 和 lights，适合业务层建立模型实例到场景节点的映射。删除实例使用 `scene.destroySubtree(instance.root)`，不会连带销毁共享资产。

模板本身不拥有 AssetStore。销毁模板不会删除资源；销毁仓库资源会使之后依赖这些句柄的提取失败。导入器失败时回滚本次创建的资源，实例化失败时清理新节点，应用仍需捕获异常并向用户显示错误。

支持静态 glTF/GLB 与 OBJ/MTL，支持范围和已知限制见[架构文档](ARCHITECTURE.md)。模型过大、过小或朝向不符时，通过实例根节点统一调整平移、旋转和缩放；不要假定引擎会自动居中并适配相机。导入模型中的相机也可能不适合当前视口，可直接使用应用创建的相机。

## 7. 相机、灯光与事件驱动交互

### 7.1 放置相机和切换投影

```cpp
const auto cameraNode = scene.createNode(scene.root(), "Main camera");
Camera camera;
camera.verticalFov = radians(60);
camera.nearPlane = 0.1f;
camera.farPlane = 200;
camera.aspectRatio = 0; // 自动取本次视口的宽高比。
scene.setComponent(cameraNode, camera);
const auto pose = Mat4::lookAt({3, 2, 5}, {0, 0, 0}).inverse();
if (!pose)
    throw std::runtime_error("Invalid camera pose");
scene.setLocalTransform(cameraNode, *pose);
view.cameraNode = cameraNode;
```

`lookAt()` 返回世界到相机的视图矩阵；相机节点需要它的逆矩阵作为世界姿态。上例根节点是单位矩阵。如果相机有非单位父变换，应再左乘父世界矩阵的逆，得到相机局部矩阵。观察位置与目标重合、up 与视线平行会导致非法视图。

透视相机要求 `0 < verticalFov < radians(179)`、`nearPlane > 0` 且 `farPlane > nearPlane`。近裁剪面过近而远裁剪面过远会降低深度精度，优先让范围覆盖实际场景。

正交相机使用 `camera.projection = Camera::Projection::Orthographic` 和正的 `orthographicHeight`，宽度由宽高比决定。固定 aspectRatio 会保持指定比例，窗口改变时不会自动替换为新比例；需要跟随视口时保持为 0。

需要可复用的操作器时，链接 `renderer::camera` 并使用 `CameraController`。应用将键鼠事件转换为旋转/平移/移动/缩放输入，每帧在角色移动之后调用 `setTarget`、`update`、`apply`，再提取快照。支持 Orbit、Follow、Free、指数平滑、缩放/俯仰限幅及父节点变换；完整接口和输入单位见[相机控制](CAMERA.md)。

### 7.2 输入回调更新状态，主循环更新场景

```cpp
bool movingForward = false;
auto keyboard = window.subscribe<KeyEvent>([&](const KeyEvent &event) {
    if (event.key == Key::W)
        movingForward = event.action != InputAction::Release;
});
auto focus = window.subscribe<WindowFocusEvent>([&](const auto &event) {
    if (!event.focused)
        movingForward = false;
});
```

连续移动应保存按下状态并乘以 dt，不把系统 Repeat 的频率当作运动速度。切换开关只处理 Press，避免长按反复切换；失焦后清除按键状态，避免收不到 Release 时持续移动。

输入绑定、目标选择和玩法限制属于应用层，可复用控制器由 renderer::camera 提供。配套 `scene_app.cpp` 还保留手工轨道相机作为对照：右键拖动修改 yaw/pitch、滚轮修改距离、键盘平移目标，再用 `lookAt()` 生成姿态。当前 SDK 没有鼠标捕获或无限相对移动接口，指针离开客户区后的交互需求需要单独扩展。

### 7.3 订阅、关闭与窗口大小

必须保存 `EventSubscription`；临时返回值在语句结束时析构，订阅随即取消。令牌可移动且不可复制，析构或 `disconnect()` 取消后续分派，不延长 Window 生命周期。

`pollEvents()` 固定本批事件与订阅者快照，按注册顺序回调。回调中新产生的事件和新订阅从下次 poll 开始处理；取消可以阻止尚未调用的监听器。回调抛异常不会阻止本批其他监听器，结束后重抛第一个异常。不允许递归调用同一窗口的 `pollEvents()`。

关闭请求会立即设置标记；需要保护未保存状态时，集中注册一个关闭处理器并按业务状态调用 `cancelClose()`。SDK 是广播模型，没有 handled 标记或事件冒泡。Esc 默认请求关闭，也可以取消。

鼠标位置及 WindowResizeEvent 使用屏幕坐标单位，FramebufferResizeEvent 使用实际像素。高 DPI 下两者可能不同。高层 Renderer 自动按窗口 framebuffer 尺寸重建内部目标；低层 RHI 应用须在尺寸变化时自己管理目标和编译图，不要仅修改 viewport 而保留错误尺寸的附件。

全屏使用 `window.setMode(WindowMode::BorderlessFullscreen)` 或 ExclusiveFullscreen，退出使用 Windowed；也可以通过 WindowConfig.mode 在创建时指定。切换保留上下文，恢复原窗口位置、尺寸和最大化状态。独占分辨率、目标显示器、隐藏限制和快捷键示例见[全屏说明](FULLSCREEN.md)。

16 种事件的字段、取消和跨线程边界见[窗口事件说明](WINDOW_EVENTS.md)。

### 7.4 配置灯光

```cpp
const auto sunNode = scene.createNode(scene.root(), "Sun");
Light sun;
sun.type = LightType::Directional;
sun.color = {1, 0.95f, 0.85f};
sun.intensity = 3;
scene.setComponent(sunNode, sun);
scene.setLocalTransform(sunNode, Mat4::rotationX(radians(-45)));
```

方向光与聚光灯使用节点局部 -Z 的世界方向，点光源使用节点世界位置。强度、颜色非负，range 须为正；聚光角为弧度并满足 `0 <= innerCone < outerCone < radians(90)`。

每个视图最多使用 64 个显式灯光，首个允许投影的方向光生成阴影。点光源和聚光灯当前只参与照明，不生成阴影。没有显式灯光时使用 RenderView 的默认方向光；ambientLight 控制环境项。光照计算的所有颜色均为线性值。

## 8. 一帧的内部流程与画质设置

### 8.1 数据流

```text
pollEvents → 应用更新 Scene → extract → 不可变快照
                                      ↓
                       相机选择 / 层过滤 / 视锥裁剪
                                      ↓
                       不透明和透明排序 / 实例合批
                                      ↓
                  阴影 → 不透明 HDR → 透明 → ToneMap → 可选 FXAA → Present 拷贝
                                                                       ↓
                                                         engine.present 交换缓冲
```

快照提取会刷新世界矩阵、继承可见性、世界包围盒和法线矩阵缓存。可见网格使用不可逆世界矩阵时会被跳过，常见原因是把某个缩放轴设为 0。快照中的可见相机必须与 `RenderView.cameraNode` 对应，否则帧准备失败。

RenderView 的常用配置如下，修改后在下一次提交生效：

| 字段 | 默认值/约束 | 用途 |
| --- | --- | --- |
| cameraNode | 必须指定快照中的可见相机 | 选择本次观察视图 |
| clearColor | 线性 RGBA，各分量 [0,1] | 背景清除色 |
| lightDirection | 默认 `{-0.4,-1,-0.3}`，须有限且非零 | 无显式灯光时的光线传播方向；建议即使有显式灯光也保留合法值 |
| ambientLight | 0.25，范围 [0,1] | 环境光强度 |
| frustumCulling | true | 是否进行视锥裁剪 |
| shadows / shadowResolution | true / 1024，分辨率 [64,8192] | 阴影启用与目标分辨率；关闭阴影时分辨率参数仍需合法 |
| exposure | 1，须为正有限数 | HDR 曝光系数 |
| toneMapping | None | None、ACES 或 Reinhard |
| transparency | SortedAlpha | 排序透明或 WeightedBlendedOIT |
| antiAliasing | None | FXAA 后处理抗锯齿，可以逐帧开启或关闭 |

### 8.2 不透明、裁剪和透明

| 材质/模式 | 深度测试 | 深度写入 | 顺序与处理 |
| --- | --- | --- | --- |
| Opaque | 开启 | 开启 | 按状态组织并实例合批，写入线性 RGBA16F HDR |
| Mask | 开启 | 开启 | 低于 alphaCutoff 的片元丢弃，阴影使用一致裁剪 |
| Blend + SortedAlpha | 开启 | 关闭 | 按实例包围盒中心深度从远到近，合并相邻兼容项 |
| Blend + WeightedBlendedOIT | 开启 | 关闭 | 按兼容状态合批，写累积色/透过率，再合成到 HDR |

仅把 baseColor.w 改为 0.5 不会自动启用透明，还必须设置 `alphaMode = AlphaMode::Blend`。Mask 适合树叶、围栏等二值覆盖，不是半透明玻璃。

SortedAlpha 能表达常规层叠，但交叉表面和相互穿透对象可能出现次序错误。加权 OIT 避免按物体深度排序，颜色合成仍是近似，可能弱化层次；它不提供折射或彩色透射。可直接按视图切换：

```cpp
view.transparency = TransparencyMode::WeightedBlendedOIT;
view.toneMapping = ToneMapping::ACES;
view.exposure = 1.0f;
view.shadows = true;
view.shadowResolution = 1024;
```

OIT 新增 RGBA32F 和 R32F 两个目标及一次全屏 Resolve，增加带宽和每像素 20 字节的逻辑颜色存储。详细公式、画质取舍和性能边界见 [OIT 说明](OIT.md)。

### 8.3 阴影、ToneMap 和回读

Blend 材质不投射阴影，可接收阴影；Mask 可投射裁剪阴影。`MeshRenderer.castShadow`、`receiveShadow` 分别控制投影和接收；视锥外但层匹配的投影者仍可能进入阴影批次。关闭 view.shadows 时仍保留图阶段，但不绘制阴影批次。

曝光和 ToneMap 在线性 HDR 合成之后执行，再写入 sRGB 显示纹理。`ToneMapping::None` 仍经过曝光和最终显示编码，并不意味着输入输出都跳过颜色空间处理。

截图使用 `engine.readback()` 和 `Image::savePpm(path)`。回读会同步等待并产生 CPU 内存副本，不宜每帧常开；PPM 忽略 Alpha，`savePpm()` 不创建父目录，配套应用自行创建目录。

高层 render 会覆盖主输出，连续调用不会自动构成分屏。需要小地图、角色预览或编辑器视口时，使用 createRenderTexture/renderToTexture 保存独立输出，再通过 UI 图片或自己的 RHI 流程合成；参见第 22 节。

### 8.4 可选 FXAA

`view.antiAliasing = AntiAliasing::FXAA` 开启抗锯齿，None 关闭。FXAA 在 ToneMap 之后处理显示编码图像，覆盖不透明、Mask 和透明几何的最终边缘；不增加几何采样数。可以与 OIT、曝光、不同 ToneMap 和专用渲染线程组合。

开启时增加一次全屏绘制和一张普通 RGBA8 临时纹理，antiAliasingDrawCalls 为 1，总 drawCalls 也增加 1；几何三角形数不变。可能让细线和小纹理变柔，不能消除所有闪烁。完整颜色空间约定和验证见[抗锯齿说明](ANTI_ALIASING.md)。

## 9. 所有权、线程和异步提交

### 9.1 生命周期速查

| 对象 | 保存的内容 | 关键约束 |
| --- | --- | --- |
| AssetStore | 不可变 CPU 资产 | 销毁句柄立即失效，数据可由材质或快照继续持有 |
| Scene | 节点、组件、资产句柄 | 不拥有仓库；未来提取需要引用的网格/材质句柄有效 |
| Model | 模板与资产句柄 | 复制模板不替代资源仓库，多个实例共享资产 |
| RenderSnapshot | 已解析资源引用与世界数据 | 可复制、跨线程读取，不依赖原 Scene/AssetStore 存活 |
| CommandList | 不可变 CPU 命令与 GPU 资源句柄 | 不替未提交命令保活资源，提交前不能销毁引用句柄 |
| Device | 原生资源、提交序号与延迟回收 | 资源操作和提交在持有上下文的设备线程进行 |
| CompiledRenderGraph | 编译计划、回调、临时资源及共享设备后端 | 首次执行绑定设备，执行与析构在设备线程进行 |
| EventSubscription | 独占订阅状态 | 捕获引用要自行保活，窗口销毁即断开 |

典型安全删除顺序是先从场景删除/替换引用，再销毁不再使用的仓库句柄。已提取快照不会突然丢失数据；但是下一次 extract 仍引用已销毁网格或材质时会报错。

材质创建时解析并共享纹理数据，因此销毁原纹理句柄不会破坏已有材质；之后用同一个失效纹理句柄创建新材质仍会失败。CPU 资源和 RHI 资源属于两个系统，不能拿 AssetStore 的 TextureHandle 当成 `rhi::TextureHandle`。

### 9.2 线程分工

| 工作 | 线程建议 |
| --- | --- |
| Window 创建、订阅、pollEvents、resize、销毁 | 应用主线程/窗口创建线程 |
| Scene、AssetStore、RenderExtractor | 单写线程，提取时不与输入修改并发 |
| 高层 Renderer 接口和析构 | 应用主线程，由内部路由到可选渲染线程 |
| 独立 CommandEncoder 录制 | 可放工作线程，不并发修改同一个编码器 |
| 低层 Device 资源操作、提交、查询、销毁 | 持有上下文的设备线程 |

快照可跨线程传递，不等于 Scene 可以并发读写。跨线程取消事件令牌也不等待已经开始的回调结束；释放捕获对象前仍需由应用协调。

### 9.3 submit、帧槽和背压

```cpp
auto result = engine.submit(extractor.extract(scene, assets), view);
// 只执行不依赖本帧结果的业务工作；可以继续修改 Scene。
scene.setLocalTransform(body, Mat4::rotationY(radians(15)));
const auto stats = result.get(); // 取回统计或任务异常。
engine.present();
```

`render()` 等价于 submit 后 get。开启 renderThread 时，submit 把任务放入有界队列；未开启时就在调用线程执行。排队满时仍会阻塞提交者，因此它不是保证立即返回的非阻塞接口。

`RendererConfig.framesInFlight` 默认 3，控制 GPU 帧槽；`maxQueuedFrames` 默认 2，控制 CPU 待执行任务。增大队列可能增加输入延迟和快照内存，不会直接降低 GPU 绘制成本。

创建时 framesInFlight 和 maxQueuedFrames 均须在 [1,8]，initialInstanceCapacity 默认为 4096、合法范围 [1,1000000]；初始容量不足时内部按需扩容。debug 默认 true，renderThread 默认 false。RendererConfig 在创建时生效，SDK 当前没有运行中重新设置配置的接口。

future 就绪只表示 CPU 处理及命令提交完成。需要复用 GPU 正在使用的内存时使用设备完成标记或 wait；应用退出、截图或检查结果时可用 `waitIdle()`。不要在每个正常动画帧都强制 waitIdle，这会阻断 CPU/GPU 重叠。应用应取回 future，避免漏掉任务异常。

## 10. 自定义 Shader、资源反射和热更新

### 10.1 GLSL 输入约定

ShaderCompiler 输入采用 Vulkan 语义 GLSL 450，编译到 SPIR-V 1.5；OpenGL 后端再转译为桌面 GLSL。使用 `layout(set=N,binding=M)`，不要在应用 Shader 中硬编码内部 OpenGL 物理绑定点。

```cpp
ShaderCompiler compiler;
const auto shader = compiler.compile(ShaderStage::Compute, R"(
#version 450
layout(local_size_x=1) in;
layout(std430,set=2,binding=7) buffer Values { uint values[]; } data;
void main() { data.values[gl_GlobalInvocationID.x] = 42u; }
)", "fill.comp");
for (const auto &binding : shader.bindings) {
    std::cout << binding.group << ':' << binding.binding << ' '
              << binding.byteSize << '\n';
}
```

`group=2` 对应逻辑 set，`binding=7` 对应组内绑定号。创建 Pipeline 时后端会分配物理绑定点；同一个逻辑绑定在不同 Shader 阶段出现时，其资源种类和块布局必须兼容。

### 10.2 文件、宏与 include

```cpp
ShaderCompileOptions options;
options.defines = {"USE_FOG=1", "MAX_LIGHTS=16"};
options.includeDirectories = {"assets/shaders/include"};
const auto artifact = compiler.compileFile(ShaderStage::Fragment,
                                            "assets/shaders/surface.frag", options);
```

GLSL 文件需要 include 时，按 glslang 规则启用 `#extension GL_GOOGLE_include_directive : require`。搜索先考虑引用文件所在目录，再考虑配置目录；嵌套深度存在上限。compile 的 sourceName 也参与相对路径定位与错误诊断，建议传入可定位的路径名。

缓存键包含预处理结果和阶段，因此宏或 include 内容改变后会得到相应新产物。文件监控器、重试时机和错误 UI 由应用实现；SDK 不提供自动文件监控。

### 10.3 CPU/GPU 布局

UBO/SSBO 的 std140/std430 不是任意 C++ 结构体的自然布局。根据 ShaderBinding 的 byteSize、ShaderMember 的 offset/size 检查 C++ 数据结构，并查询 `capabilities().uniformAlignment`、`storageAlignment` 验证绑定偏移。

例如 std140 中三分量向量、数组和矩阵可能引入额外对齐。不要只比较 `sizeof()` 总大小，还要检查成员偏移；应用可以用 `alignas`、`offsetof` 和反射进行启动时校验。运行时数组还要自行保证已分配元素数足以覆盖调度范围。

手工调用 `toOpenGL()` 时，非零 group 必须传入 `BindingRemap`；正常通过 Device 创建 Pipeline 时不需要应用自己完成这步映射。

### 10.4 热更新的替换顺序

应用先读取并编译新的 Shader，再尝试创建新 Pipeline 和匹配的绑定组；全部成功后才替换当前使用的句柄。编译失败时保留旧 Pipeline 并显示诊断，避免把场景切成黑屏。替换后确认没有尚未提交的命令引用旧句柄，再销毁旧句柄；已提交的原生资源由设备延迟回收。

这条热更新路径用于应用自建 RHI Pipeline。高层 MaterialDesc 不接受任意 ShaderArtifact，但可通过 effect.vertex/surface 定制函数体与参数；完整自定义入口/绑定布局使用 RHI 或修改高层实现，见第 22 节。

## 11. 直接使用 RHI

### 11.1 设备与资源

```cpp
Window window({.width = 128, .height = 128, .visible = false});
auto device = rhi::Device::createOpenGL(window);
const auto caps = device.capabilities();
const auto supported = device.formatFeatures(rhi::Format::Rgba16Float);
```

查询能力后再选择附件格式、绑定数量和内存范围。当前支持二维/二维数组与层视图、可选 BC 格式、float 顶点属性、uint32 索引、UBO/SSBO、组合采样纹理和 Storage Image。BC 不可作附件/Storage Image；附件单采样，MSAA、各种顶点整型布局和 Bindless 描述符数组不属于当前能力。

| Memory | 用途与同步 |
| --- | --- |
| Device | GPU 使用；可上传或通过 GPU 命令写入 |
| Upload | CPU 持久映射写入；复写前等待此前使用该范围的 GPU 完成 |
| Readback | 诊断/结果回读；使用 readBuffer 取得拥有数据的 CPU 数组 |

`map()` 仅支持 Upload。coherent 映射解决可见性，不解决 CPU 覆写 GPU 尚在读取的竞争；应用应使用帧槽、范围分配和完成标记。

### 11.2 Compute 绑定组与命令

完整实现见 [compute_app.cpp](../examples/development/compute_app.cpp)。其中 Shader 的 `set=2,binding=7` 对应：

```cpp
const auto group = device.createBindingGroup({
    .group = 2,
    .entries = {{.binding = 7, .kind = BindingKind::StorageBuffer, .buffer = storage}}
});
rhi::CommandEncoder commands;
commands.bindPipeline(pipeline);
commands.bindGroup(group);
commands.dispatch(16);
```

示例 local_size_x=1，dispatch(16) 总共产生 16 个调用。一般情况下调用总数是工作组数乘 local size；对非整除长度需在 Shader 内做索引边界判断。

GPU 存储写入之后的用途决定屏障：

| 前一次访问 | 后一次用途 | after |
| --- | --- | --- |
| Compute 写 SSBO | 拷贝源 | TransferRead |
| Compute 写间接参数 | drawIndexedIndirect / dispatchIndirect | IndirectRead |
| Storage Image 写入 | 后续纹理采样 | SampledRead |
| StorageBuffer 写入 | 后续另一段计算读取 | StorageRead 或 StorageReadWrite |

```cpp
const rhi::ResourceBarrier barrier{
    .before = rhi::Access::StorageWrite,
    .after = rhi::Access::TransferRead,
    .buffer = storage,
    .sourceStages = rhi::Stage::Compute,
    .destinationStages = rhi::Stage::Transfer,
};
commands.barrier(std::span{&barrier, 1});
commands.copyBuffer(storage, readback, byteCount);
const auto completion = device.submit(commands.finish());
device.wait(completion);
const auto data = device.readBuffer(readback);
```

屏障表达 GPU 命令之间的访问关系；完成标记表达 CPU 何时可以安全使用结果或复用内存，两者不能相互替代。CommandEncoder 的 barrier、计算和拷贝在渲染作用域外录制。

### 11.3 图形 Pipeline 与附件

图形管线描述包含顶点/片元 Shader、顶点格式、颜色/深度附件格式，以及固定的深度、剔除和混合状态。没有深度附件时应关闭 depthTest 和 depthWrite。实际附件格式必须与 Pipeline 匹配；colorBlends 非空时必须与 colorFormats 等长。

典型绘制顺序是 `beginRendering → bindPipeline/bindGroup/bindVertexBuffer → viewport/scissor → draw/drawIndexed → endRendering`。绘制只能在 begin/end 之间，拷贝和计算在作用域之外。索引缓冲为 uint32，间接索引绘制使用五字段、至少 20 字节的条目，stride 须为 4 的倍数。

颜色附件为 Rgba8Srgb 时，传给 Shader 的颜色仍应为线性值，由附件写入处理显示编码。`CommandEncoder::present(texture)` 将图像拷贝到窗口缓冲，不执行交换；低层应用随后调用 `window.present()`。

### 11.4 提交、销毁和回读

`finish()` 后编码器不能再次录制，CommandList 可复制。录制只保存句柄，没有提前保活资源；不能录制后销毁句柄再提交。`device.submit()` 成功后才由设备跟踪原生对象的提交使用，销毁句柄使其立即失效，实际对象在安全时间回收。

`complete(token)` 可非阻塞检查，`wait(token)` 等待指定提交，`waitIdle()` 等待全部工作。token 是设备专属值，不能交给另一设备。

`readBuffer()`、`readTexture()` 是同步诊断路径。纹理回读返回左上原点 RGBA8，浮点颜色会转换为字节值，它不是保留全部 HDR 精度的导出接口。为了清晰，示例等待后回读并用 memcpy 复制整数，避免依赖字节数组的对齐或别名规则。

## 12. 使用 RenderGraph 编写多阶段流程

### 12.1 逻辑图与原生资源的关系

RenderGraph 的 `GraphResource` 是图内逻辑身份，不是 RHI 句柄。`createTexture/createBuffer` 声明由编译图分配的临时资源；`importTexture/importBuffer` 声明应用已有资源，图不接管所有权。

每个 Pass 在 accesses 中声明全部资源用途，回调通过 GraphResources 解析本 Pass 允许访问的实际句柄。不能从未声明的资源取句柄，也不能把解析器引用保存到回调之后。

### 12.2 完整示例的流程

[graph_app.cpp](../examples/development/graph_app.cpp) 构造以下图：

```text
外部顶点缓冲 → Triangle → 临时 sRGB 纹理 → Copy output → 外部显示纹理 → Present

                                    Unused clear → 无使用者的临时纹理（被剔除）
```

Triangle 使用自定义 Shader 和 Pipeline。Copy output 把结果复制到应用拥有的同格式纹理，供图外回读。Present 标记 sideEffect=true，保证窗口拷贝不会因没有后续资源写入而被移除。最终导出显示资源，保留其生产链。

四个声明 Pass 中实际执行三个，Unused clear 不执行。示例反复执行同一份编译图两次，查询每个保留 Pass 的 GPU 时间，验证最终图像中心确实出现三角形。

### 12.3 访问声明与初始化

```cpp
const auto input = graph.importBuffer(vertexBuffer, vertexDescription,
                                      rhi::Access::VertexRead);
const auto target = graph.createTexture(imageDescription);
graph.addPass("Draw", {
    {input, rhi::Access::VertexRead, rhi::Stage::Vertex},
    {target, rhi::Access::ColorWrite, rhi::Stage::Attachment},
}, [input, target, pipeline](rhi::CommandEncoder &commands, const GraphResources &resources) {
    rhi::RenderingDesc pass;
    pass.colors.push_back({.texture = resources.texture(target)});
    commands.beginRendering(pass);
    commands.bindPipeline(pipeline);
    commands.bindVertexBuffer(0, resources.buffer(input));
    commands.draw(3);
    commands.endRendering();
});
graph.exportResource(target);
auto compiled = graph.compile();
device.wait(compiled.execute(device));
```

上例是局部展示，完整 Pipeline、缓冲描述和窗口交换见配套源码。Pass 按资源生产顺序声明；后续读取区间必须已经被初始化。import 的 initial 不为 None 表示外部已初始化并给出已有状态，不能用它掩盖真实的未初始化内容。

没有输出、也没有 sideEffect 的分支可以被剔除。只声明一个会写外部对象的回调但未导出资源/设置副作用，不足以保证执行。编译器先校验访问，再进行剔除；不要指望未使用分支中的非法访问一定被忽略。

单 Pass 对同一区间进行存储读改写应声明一条 StorageReadWrite，不要分成相互重叠的 StorageRead 和 StorageWrite。附件不能在同一个 Pass 中同时作为相同范围的采样输入形成反馈。

### 12.4 范围、显式依赖和回调约束

`GraphRange.firstMip/mipCount` 用于纹理 mip 范围，offset/size 用于缓冲字节范围；数量为 0 表示到末尾。重叠读写访问建立 RAW/WAR/WAW 依赖，不重叠的子范围可独立建模。`dependsOn(pass, dependency)` 增加显式先后顺序，形成依赖环会导致编译失败。

图只分析声明，不会反向分析任意回调命令。回调使用的外部可变 Buffer/Texture 都应 import 并声明访问；Pipeline、Sampler、BindingGroup 等其他对象仍需由应用保活。外部多个纹理视图如果共享底层存储，应用须统一建模其别名关系，不能把它们假定为互不相关的存储。

捕获逻辑句柄通常按值；捕获 `this` 或外部对象引用时，确保其在每次 execute 期间存活。回调由执行设备线程调用，在其中录制命令，不调用 encoder.finish()。不要捕获即将离开作用域的局部描述引用。

### 12.5 编译图缓存、重建与清理

compile 在 CPU 上生成依赖、裁剪、屏障和物理复用方案，不分配 GPU 对象。首次 execute 绑定设备并分配资源，之后复用；Device 包装对象移动不改变后端身份，但同一编译图不能换到另一台 Device。

目标尺寸、格式、mip 或 Pass 结构改变时，应用应重建逻辑图和编译图。原图中的 import 句柄不会因为应用变量重新赋值而自动更新；外部纹理重建后须相应重建导入关系。

编译图应先于其导入资源销毁，且析构发生在设备线程。临时资源由编译图回收，import 资源由应用回收。`statistics()` 查看裁剪和物理资源数，`passes()` 查看执行顺序与屏障数。

给 execute 传入时间戳池时，至少分配 `2 * statistics().executedPasses` 个条目。等待 GPU 结束后，可用 `timestampResult(pool, 2*i)` 和 `timestampResult(pool, 2*i+1)` 计算 Pass 纳秒差值；不要把绝对时间戳直接当作耗时。

## 13. 性能、内存和诊断

### 13.1 先区分 CPU、GPU 和等待

| 指标 | 解读方式 |
| --- | --- |
| sceneInstances / visibleInstances / culledInstances | 场景快照、视图可见与剔除数量，用于检查层和视锥 |
| sceneDrawCalls、shadowDrawCalls、transparentDrawCalls | 对应阶段的几何批次数 |
| drawCalls | 包含场景、阴影、OIT Resolve、ToneMap、可选 FXAA 和屏幕覆盖层，不能直接等于对象数 |
| antiAliasingDrawCalls | 开启 FXAA 时为 1，关闭为 0，不计入几何三角形数 |
| triangles | 仅场景几何，不包含阴影和全屏三角形 |
| uploadedBytes | 本帧资源/帧数据上传量，持续很大时检查资源是否反复创建 |
| gpuMeshes / gpuTextures | 当前 GPU 缓存数量，结合资源生命周期判断是否异常增长 |
| prepareMilliseconds | 前端准备耗时 |
| submitMilliseconds | 后端处理及可能的帧槽等待，不等于纯驱动提交时间 |
| gpuMilliseconds / passes | 来自已完成旧帧，仅在 gpuTimingValid 时使用 |

GPU 时间可能滞后，透明模式切换后短时间内可能仍看到旧模式 Pass 名称。垂直同步等待和呈现耗时也不能简单归为 Shader 执行。优先在相同场景、分辨率、视角和配置下比较。

### 13.2 常用优化顺序

1. 共享同一 Mesh/Material 句柄表示大量相同实例，避免每个节点重复创建资源。
2. 开启视锥裁剪并正确划分 layerMask/cullingMask，避免提交不需要的几何。
3. 检查 draw call 分布，减少材质和 submesh 切换；排序透明的批次还受深度顺序限制。
4. 检查阴影分辨率、投影者数量以及透明过度绘制。OIT 改善排序和合批，不消除片元成本。
5. 让持久上传缓冲按帧槽或不重叠范围复用，避免同步覆写。
6. 将截图、readBuffer、readTexture 和 waitIdle 保留在真正需要结果的位置。
7. 稳定的图结构缓存 CompiledRenderGraph，不必每帧重新创建临时资源计划。

场景变换缓存避免重复矩阵求逆与连乘，但仍需遍历检查版本。不要把“增量更新”理解为对任意规模静态树完全零开销。当前 PBR 采用每视图灯光列表，未实现分簇或延迟光照架构。

### 13.3 分配器和提交复用统计

高层 `engine.memoryStatistics()` 返回资源载荷、图目标、实例镜像与暂存保留估算；Vulkan 另有 nativeMemoryBlocks/Allocations、nativeMemoryBlockBytes/AllocationBytes，先检查 nativeMemoryDetailsAvailable。分配字节含对齐，块字节含空闲范围；不要和 estimatedGpuBytes 相加，也不要当作完整驱动驻留显存。

低层 `device.statistics()` 查询 submissionContextsCreated/Reused、descriptorPoolsCreated/Reused、缓存与待回收提交数，以及 VMA 分配器统计。Vulkan 已完成上下文复用与 VMA 子分配；查询不隐式等 GPU，OpenGL available=false 表示此诊断不可用。具体缓存上限、预算和统计口径见[诊断](DIAGNOSTICS.md)与[优化说明](OPTIMIZATION.md)。

### 13.4 错误处理

CPU 参数校验、导入、Shader 编译、图编译和设备操作可能抛异常。应用边界应捕获 std::exception，将错误显示或记录；异步任务通过 future.get 重新抛出。窗口用户回调的异常在 pollEvents 完成本批后抛出，独立渲染线程不会替应用吞掉业务错误。

调试构建保持 RendererConfig.debug 或 Device 的 debug 开启，结合 `debugErrorCount()` 和驱动诊断定位底层问题。不要仅看到计数为零就认为逻辑画面正确；相机、层、颜色空间和材质槽错误仍需业务检查。

## 14. 常见问题定位

| 现象 | 优先检查 |
| --- | --- |
| find_package 找不到包 | CMAKE_PREFIX_PATH 指向 SDK 前缀，或 renderer_DIR 指向配置所在目录；自定义 lib 目录时调整路径 |
| Windows 链接失败或运行库冲突 | x64/x86、Debug/Release、/MDd 与 /MD 是否匹配；SDK 依赖库是否完整 |
| 窗口存在但没有物体 | 是否 poll/extract/render/present；cameraNode 是否在快照；相机 -Z 方向、裁剪面和缩放是否正确 |
| 对象意外消失 | 可见祖先、层掩码、包围盒、背面绕序，以及世界变换是否不可逆 |
| 改组件没有生效 | 是否把 component<T>() 副本通过 setComponent 写回；是否重新提取快照 |
| 删资源后下一帧抛异常 | 场景或模型实例是否仍引用已销毁网格/材质；句柄 bool 不检测存活 |
| 半透明仍不透明 | alphaMode 是否设置为 Blend；颜色 Alpha 与纹理 Alpha 是否符合预期 |
| 透明层次跳变 | 检查物体级深度排序，必要时选 OIT 并接受近似合成边界 |
| 图片上下翻转 | 手工 RGBA8 的左下原点、loadTexture 翻转和模型 UV 约定是否被重复处理 |
| 颜色偏灰、过暗或过亮 | sRGB 标记、线性颜色输入、重复显示编码、曝光和 ToneMap |
| 相机移动速度随机器变化 | 用 dt 更新状态，避免依赖按键 Repeat 或每帧固定增量 |
| 松开键后仍在移动 | 处理 Release，并在窗口失焦时清除输入状态 |
| 事件回调没有触发 | 保存订阅令牌、调用 pollEvents；订阅不会重放初始状态 |
| resize 后尺寸错误 | 区分屏幕坐标与像素；低层目标和编译图是否同步重建 |
| Compute 数据错误 | group/binding、std430 布局、缓冲范围、工作组数量、屏障和完成等待 |
| RenderGraph Pass 不执行 | 输出是否 export，或外部副作用是否标记 sideEffect=true |
| 图编译提示未初始化 | 导入状态是否真实、首次使用是否是写入、读范围是否完整覆盖 |
| 偶发 GPU 内存增长 | 是否仍有快照/future、材质纹理引用、编译图或未完成提交持有数据 |

确认复现条件后，用最小模型、固定帧数和隐藏窗口缩小范围。GPU 像素检查使用回读的实际宽高计算坐标，不假定系统允许请求的最小窗口尺寸，也不要忽略 DPI。

## 15. 扩展业务与修改引擎源码

### 15.1 SDK 使用者可以直接做的扩展

业务对象可以在应用中保存 NodeHandle、资产业务 ID、相机输入状态和保存数据。把属性修改转换成 Scene 操作即可，不必继承 Scene 或 MeshRenderer。轨道相机、撤销重做、资产浏览、选择状态、应用事件路由和文件监控都可以先在业务层实现。

应用保存格式使用自己的稳定 ID 和描述数据，重新加载后再创建句柄。不要序列化 owner/index/generation 或 PImpl 内存布局。

自定义 Shader、Compute 和完整绘制流程使用 RHI；多阶段流程加入 RenderGraph。SDK 不暴露内部的 Backend/DeviceBackend 作为稳定插件 ABI，安装包中也不包含这些内部头文件。

### 15.2 修改引擎的文件地图

| 修改目标 | 需要同时检查的位置 |
| --- | --- |
| 新场景组件 | `include/renderer/scene.hpp` 模板派发、`src/internal/scene.hpp` 存储、`src/scene.cpp` 校验与版本、`src/renderer.cpp` 快照提取 |
| 新内置材质参数 | `assets.hpp`、`src/assets.cpp` 资源解析、`src/internal/frame.hpp` CPU/GPU 布局、`src/frame_builder.cpp`、内置 Shader 与绑定组 |
| 新内置 Pass/后处理 | `src/backends/common/renderer_backend.cpp` 图声明、Shader、Pipeline、目标重建和统计 |
| 新 RHI 命令或能力 | 公共 rhi.hpp、`src/rhi.cpp`、rhi_commands 数据/验证、DeviceBackend、OpenGL/Vulkan 回放与同步 |
| 新模型格式或扩展 | `src/model.cpp` 的导入和事务、相关描述与静态模板、CPU 测试资产 |
| 新平台/后端 | 设备工厂、平台窗口/表面、DeviceBackend、同步与资源映射、高层后端入口 |

新增对外服务类继续使用 PImpl：头文件只前置声明 Impl，析构与移动在实现文件定义；值类型和内部类按实际需要组织。自有头文件统一 include guard，并在文件、类、接口前提供中文说明、参数和返回值语义。公共头文件不能引入第三方内部类型。

Shader 布局变化需同时修改 CPU 数据、std140/std430 偏移、顶点属性、绑定组和反射检查；不能只改 GLSL 一处。增加 Pass 时声明所有资源访问、初始化关系和输出/副作用，并检查编译图缓存何时必须重建。

### 15.3 验证扩展结果

在源码根目录执行现有检查：

```powershell
cmake --preset windows-gpu-tests
cmake --build --preset windows-gpu-tests --parallel 6
ctest --preset windows-gpu-tests --output-on-failure
```

修改公共 API 或安装规则后，重新安装 SDK，并从独立构建目录运行本手册的四个示例及 `examples/sdk_consumer`。只在源码中 add_subdirectory 构建通过，不能证明已安装 SDK 的头文件、传递依赖和包配置完整。

数据/数学/导入变更用 CPU 用例验证；新增图形行为用真实设备和图像/数据回读验证。性能结论还需要真实场景、分辨率和多设备测量，现有实测范围见[验证记录](VALIDATION.md)。两种后端共用高层管线，修改 Shader 或 RHI 能力时同时运行对应后端及像素一致性检查。

## 16. 异步资源、包卸载与纹理工具

### 16.1 从后台解码到 GPU 可用是两级过程

`AssetLoader` 属于 core，包含 `<renderer/asset_loader.hpp>`。后台线程读取和解码图片/静态模型，不访问应用的 Scene/AssetStore 或 GPU；应用在其控制线程调用 publish 才把资源接收进自己的仓库。

下面是只提交一个模型请求的局部片段；假定 `assetRoot` 是已有素材目录，`ownedRoots` 为应用保存的 `std::vector<NodeHandle>`，`packages` 为 `std::vector<uint64_t>`：

```cpp
AssetLoader loader({.workers = 2, .maxRequests = 128,
                    .maxReadyBytes = 128ull * 1024 * 1024});
const auto request = loader.loadModel(assetRoot / "house.glb");
(void)request;
// 以下部分放入每帧应用更新，不用循环等待解码完成。
for (auto &result : loader.publish(assets, {.maxRequests = 4,
                                          .maxBytes = 16ull * 1024 * 1024})) {
    if (result.info.state == AssetLoadState::Published) {
        auto instance = result.model.instantiate(scene, scene.root(), "House");
        ownedRoots.push_back(instance.root);
        packages.push_back(result.package);
    } else if (result.info.state == AssetLoadState::Failed) {
        std::cerr << result.info.error << '\n';
    }
    loader.forget(result.info.id);
}
```

该片段中的 loader 和请求必须在初始化阶段创建，不能每帧重新创建。图片可用 `loadTexture`，发布结果读取 texture 而非 model。Ready 仅表示 CPU 解码完成；Published 表示目标仓库可使用句柄；首次实际绘制仍受 Renderer 的上传预算控制。

`RendererConfig::assetUploadBudgetBytes` 默认为 16 MiB，0 表示不限。预算是新资源载荷的软字节预算，单个超大资源允许独占一次以保证前进；不是硬实时帧预算。上传未完整的绘制暂缓，`pendingResourceDraws` 反映当前视图等待批次。需要加载画面时先显示占位资源，观察实际场景是否已完成资源准备。

应及时消费 Failed/Cancelled/Published 事件并 forget，否则终态记录仍占 maxRequests。取消不强制打断已开始的解码，析构等待后台任务结束。优先级只影响尚未开始的 CPU 请求，不控制 GPU 绘制顺序。

### 16.2 正确卸载资源包

先停止引用包内句柄的业务对象和新提交，再删除相关 Scene 子树并 `assets.releasePackage(package)`。旧快照和未完成工作可继续保留资源；之后不能再用失效包句柄创建新场景快照。场景切换可调用 `extractor.clearCache()` 释放提取器持有的旧根；未完成 future 和应用缓存也需按生命周期释放。

Model 是模板，不是包管理器；包卸载后保留一个 Model 副本不代表旧句柄又可使用。批次同样可能保留旧版本，应在切场景时更换/释放。CPU 有效句柄、存活 CPU 数据和 GPU 原生分配可能在不同时间减少，详见[资源加载](ASSET_LOADING.md)。

### 16.3 DDS、BC 压缩、mip 与数组

包含 `<renderer/texture.hpp>`，无需扩展模块。建议在打包阶段压缩并生成 mip，运行时直接读取 DDS：

```cpp
const auto original = assets.loadTexture(assetRoot / "tree.png", true);
auto compressed = texture::compress(assets.textureDescription(original),
                                    TextureCompression::BC3, true);
texture::saveDds(compressed, assetRoot / "tree.dds");
const auto treeTexture = assets.loadTexture(assetRoot / "tree.dds");
```

BC1 适合本地编码器要求的不透明图片，BC3 支持平滑 Alpha，BC4/BC5 用于线性单/双通道；BC2/BC7 支持读取/解码/上传但没有内置编码器。DDS 自带颜色空间和 mip 元数据，不能用 loadTexture 的 srgb 参数随意覆盖。高层可按设备能力降级 RGBA8，低层 RHI 则要求使用支持的格式。

数组的各层必须同尺寸、颜色空间和 mip 结构；可以先 `texture::makeArray` 再压缩。同一 Sprite/材质的实例通过 `textureLayer` 选层，有助于合批，但排序、管线和材质差异仍会拆批。BC 是有损格式，精细 UI/小字可继续使用未压缩资源；具体容量、格式和坐标约定见[纹理专章](TEXTURES.md)。

`AssetStore::updateTexture` 发布新句柄而非原地修改旧 CPU 纹理，旧快照继续保留旧版本。当前区域更新限单层单 mip 的未压缩 CPU 图片；实时 RenderTexture 属于第 22 节的另一种明确可更新资源。

## 17. Sprite、动画、批次与交互

### 17.1 创建一张朝向相机的图片

仅 core 即可创建 Sprite；假定图片已有实际尺寸，以下取图集中的 64×96 区域，纹理句柄属于 assets：

```cpp
#include <renderer/sprite.hpp>
SpriteDesc description;
description.texture = treeTexture;
description.rect = {0, 0, 64, 96};
description.pixelsPerUnit = 64;
description.pivot = {.5f, 0};
const auto sprite = assets.createSprite(description);
const auto actor = scene.createNode(scene.root(), "Actor");
SpriteRenderer component;
component.sprite = sprite;
component.billboard = BillboardMode::Upright;
scene.setComponent(actor, component);
```

此图片宽 1、高 1.5 世界单位，脚底为锚点。rect 与 trimOffset 使用左下像素原点；外部图集若使用左上坐标应转换一次。Fixed 采用节点图片平面，Upright 保持 Y 向上朝向相机，ViewPlane 平行相机平面。图片几何在当前视图中展开，不需要因相机旋转逐个改业务节点矩阵。

默认 Blend/unlit 适合保留美术颜色和透明边缘。角色部件挂在包含 SortingGroup 的根下，以 sortingOrder 控制帽子/身体等关系；跳跃时让排序根留在地面，只抬高图片子节点。一般软透明深度测试开启、深度写入关闭；Opaque/Mask 写深度，不会因 sortingLayer 改大而忽略深度测试。

SortedAlpha 保留排序约束；OIT 是近似合成。默认 Sorted 精灵在 OIT 合成后走有序队列，烟雾等可选 FollowView；需要烟雾与人物严格逐层交错时，统一使用排序透明或调整素材拆层。参数见 [Sprite 专章](SPRITES.md)。

### 17.2 序列帧与方向

包含 `<renderer/animation/sprite_animation.hpp>`，链接 renderer::animation。下面假定 standing/walking 是同一仓库中有效的两个 Sprite：

```cpp
SpriteAnimationClipDesc animation;
animation.directions = {{{standing, .15, {}}, {walking, .15, "footstep"}}};
SpriteAnimationClip clip(animation);
SpriteAnimator animator;
animator.play(clip, true); // 状态进入时调用一次。
// 每帧推进，direction 为角色世界朝向。
animator.advance(dt);
animator.selectDirection(direction, scene.worldTransform(camera));
animator.apply(scene, actor);
for (const auto &event : animator.takeEvents()) {
    if (event.name == "footstep") {
        // 将此事件交给应用音频层，不在渲染器里耦合音频。
    }
}
```

支持 1/4/8 方向，各方向的帧数/时长/事件结构需一致。每帧重播 play 会不断重置动画；切方向只选方向，不重置播放时间。Clip 不代替资产仓库保活后续创建快照所需的句柄；资源包卸载前应停止相关 Animator。

### 17.3 大量装饰使用 SpriteBatch

包含 `<renderer/sprite_batch.hpp>`；假定 sprite 已创建：

```cpp
std::vector<BatchSprite> flowers;
for (int i = 0; i < 1000; ++i) {
    SpriteRenderer item;
    item.sprite = sprite;
    item.billboard = BillboardMode::Upright;
    flowers.push_back({item, Mat4::translation({float(i % 50), 0, float(i / 50)}),
                       uint64_t(i + 1)});
}
SpriteBatch decoration;
decoration.replace(assets, flowers); // 静态数据初始化/变化时发布，不必每帧 replace。
auto snapshot = extractor.extract(scene, assets, std::span(&decoration, 1));
```

批次包含世界变换，不自动继承某个 Scene 父节点、可见性或排序组。旧快照保留旧批次，替换不改写已提交内容；一个批次也不保证只有一次 Draw。为大世界按区块发布，利于界限裁剪和卸载。地图、粒子、文字返回的批次可以合并到 `std::vector<SpriteBatch>` 后一起提取。

### 17.4 屏幕射线与 Alpha 拾取

链接 renderer::interaction 并包含 `<renderer/interaction/picking.hpp>`。SceneQuery 使用**当前显示的**快照、相机及 framebuffer 尺寸；鼠标客户区坐标需换算成实际像素。调用 groundPoint 得到地面目标，pickSprite 获得 Alpha 检查后的精灵或批次实例 ID。

快照里的节点此刻可能已被业务删除，修改 Scene 前检查 `scene.valid(hit.node)`。批次命中 node 为空，由 instanceId 回到业务对象。当前普通网格不参与该精灵拾取遮挡；空间索引只粗筛候选，最终仍需平面/UV/Alpha 检查。完整坐标换算代码见 [Sprite 交互](SPRITES.md)。

## 18. 相机控制与世界文字

### 18.1 Follow 控制器接入

链接 renderer::camera，包含 `<renderer/camera/camera_controller.hpp>`；camera 是已有 Camera 组件的非根节点，playerWorldPosition 与 dt 由应用提供：

```cpp
CameraControllerConfig config;
config.mode = CameraControlMode::Follow;
config.projection = Camera::Projection::Orthographic;
config.followOffset = {0, 10, 15};
config.orthographicHeight = 14;
CameraController controller(config);
// 初始化后，每帧在物理/角色位置更新之后调用：
controller.setTarget(playerWorldPosition);
controller.update(dt, {.rotation = {yawDelta, pitchDelta}, .zoom = -wheelDelta});
controller.apply(scene, camera);
```

控制器不订阅窗口；应用将键鼠变为设备无关输入。rotation 是已含灵敏度/时间的弧度增量，不能再乘一次 dt；Free movement 才按速度和 dt 积分。传送时使用 `setTarget(position, true)` 重置跟随状态。近远面、宽高比、cullingMask 保留；相机碰撞、导航、镜头震动等策略由应用添加。详见[相机控制](CAMERA.md)。

### 18.2 创建名字牌，正确处理中文字体

链接 renderer::text，包含 `<renderer/text/text.hpp>`。fontPath 必须指向覆盖所需字符的字体；SDK 的 LatoLatin 只是拉丁示例字体，不包含完整中文，也不随包分发系统中文字体。C++ 源文件保存为 UTF-8，MSVC 保持 /utf-8。

```cpp
TextSystem text;
const auto font = text.loadFont(fontPath);
WorldTextDesc name;
name.style.font = font;
name.style.fontSize = 24;
name.style.rasterMode = TextRasterMode::Sdf;
name.text = "玩家";
name.node = actor;
name.transform = Mat4::translation({0, 2.2f, 0});
name.billboard = BillboardMode::ViewPlane;
name.sizeMode = TextSizeMode::ScreenPixels;
name.depthTest = true;
const auto label = text.create(name);
text.prewarm(name.style, "玩家0123456789"); // 加载阶段做常用字预热。
// 每帧在相机写回后，按本视图的实际像素尺寸生成：
auto letters = text.batch(scene, camera, viewportWidth, viewportHeight);
auto snapshot = extractor.extract(scene, assets, std::span(&letters, 1));
```

label 用于 setText/setColor/setVisible/destroy；TextSystem、字体与标签在初始化创建，不能每帧重复创建。文字内部持有字形仓库，批次已经解析并保留字形资源，不把它的句柄交给应用自己的 AssetStore。

ScreenPixels 只支持 ViewPlane，按 framebuffer 维持像素字号；如需要按 DPI 的 dp，应由应用换算。WorldUnits 下文字随距离缩放。depthTest=false 仍是世界文字，在场景 ToneMap/FXAA 之前；纯 HUD 应使用 UI。

位图适合固定字号，SDF 适合缩放/描边；R8、规范字号复用、LRU 和局部图集更新已开启相应默认优化。首次新字符栅格化仍会消耗 CPU，应预热并合理限定缓存。字体回退显式用 setFallbackFonts；复杂塑形、双向排版和完整彩色 Emoji 不在当前范围。多相机的 Billboard/屏幕字号文字要分别生成批次。详见[世界文字](TEXT.md)。

## 19. 游戏 UI、输入与中文组合编辑

### 19.1 文档和控件

链接 renderer::ui，包含 `<renderer/ui/ui.hpp>`。UI 是独立 RML/RCSS 文档树，输出 OverlaySnapshot，不为每个按钮创建 Scene 节点。以下片段假定已提供 LatoLatin 字体路径 fontPath；按钮示例使用英文以匹配该字体：

```cpp
UiContext ui({960, 640});
ui.loadFont(fontPath);
const auto hud = ui.loadDocumentFromMemory(R"(
<rml><head><style>
body { font-family: LatoLatin; font-size: 20px; pointer-events: none; }
button { pointer-events: auto; margin: 20px; }
</style></head><body><button id="pause">Pause</button></body></rml>
)", "hud.rml");
bool paused = false;
auto click = ui.subscribe(hud, "pause", "click", [&](const UiEvent &) {
    paused = !paused;
});
ui.update(0); // 首次输入前先完成布局。
// 每帧构建覆盖层后再提交：
ui.update(dt);
view.overlay = ui.snapshot();
```

保留 click 令牌，离开作用域就取消订阅。setText 用于游戏/用户字符串，会转义标记；setMarkup 用于应用可信 RML。表单程序改值可能触发 change，事件回调应设置明确状态，避免状态来回反转。字体、图片、RCSS 和外部 RML 相对路径按文档资源基准解析，发布时应保留相应文件结构。

### 19.2 输入路由与 DPI

UI 不自动订阅 Window，应用集中路由事件：

```cpp
// 复用已有 window（对象引用，而不是第 3.4 节的 unique_ptr 变量）。
auto input = window.subscribe([&](const WindowEvent &event) {
    const bool captured = ui.processEvent(event);
    if (!captured) {
        // 更新相机/角色的输入状态。
    }
    // 无论是否捕获，都清理已按下游戏键的 Release 和失焦状态，避免“粘键”。
});
```

鼠标客户区坐标与 framebuffer 像素不同；在正尺寸时调用 `ui.setViewport({uint32_t(pixelWidth), uint32_t(pixelHeight), double(clientWidth), double(clientHeight), float(density)})`，RCSS dp 通过 density 缩放。背景 HUD 使用 pointer-events:none，交互面板再启用，避免空白区域截获游戏鼠标。

Windows 原生组合输入需每帧 `window.setTextInputArea(ui.textInputArea())`；注入平台剪贴板服务时闭包中的 Window 必须活着。中文输入事件只是文本数据，还要实际加载包含中文字形的字体。其他平台尚无相同的原生预编辑桥接。完整示例与 IME/剪贴板代码见 [UI 文档](UI.md)和[输入增强](RENDERING_EXTENSIONS.md)。

## 20. 地图、物理、粒子与环境表现

### 20.1 先选择需要的模块

| 组件 | 负责的事情 | 应用还需负责 |
| --- | --- | --- |
| tilemap / TileMap | XZ 瓦片、负坐标、脏块批次缓存 | 图块素材与地图数据 |
| tilemap / TileStreamer | 后台块读取/保存、距离优先、预算发布、滞回卸载 | 数据源、逻辑类型解析、物件/碰撞生命周期 |
| collision / CollisionWorld | 圆/AABB、连续圆形移动与滑动 | 非刚体的移动规则、障碍添加/删除 |
| physics2d / Physics2D | Box2D 刚体、形状、固定步、接触/传感器、查询及同步 | 业务身份、角色控制、事件处理 |
| particles / ParticleSystem | 固定步图片粒子与曲线、火烟雨雪预设 | 发射位置/启停、资源和容量 |
| environment / EnvironmentController | 昼夜太阳、环境光/调色、天气强度过渡 | 天气玩法、声效、粒子参数映射 |
| presentation / SpritePresentation | 遮挡精灵淡出、闪白和恢复 | 跟随目标、效果触发、最终快照再提取 |

这些模块只依赖 core；按需链接，不要求把所有组件加到一个应用。完整基础接口见[图片世界](WORLD_MODULES.md)。

### 20.2 瓦片与流式驻留

普通 TileMap 的局部用法，假定 grassSprite/roadSprite 有效：

```cpp
TileMap map({.tileSize = 1, .chunkSize = 16, .origin = {-16, 0, -16}});
map.set(0, 0, grassSprite);
map.set(-1, 0, roadSprite);
auto terrain = map.batches(assets);
auto snapshot = extractor.extract(scene, assets, terrain);
```

只在格子变化时 set，batches 复用未改块；大地图改用 TileStreamer。后台 load/save 返回纯数据，resolve 在应用线程把稳定类型编号映射为 Sprite。块文件与业务 payload 保存稳定 ID，不序列化运行期句柄。取消的旧任务可能继续完成，但过期结果不会发布到新世界。

加载/卸载事件决定何时创建/移除场景物件、碰撞体和环境声；地图批次自动卸载不替应用删除这些业务对象。脏块保存失败会保留并施加背压，需要修复后重试，不能把失败当成功卸载。滞回、容量、发布/保存预算及 Unicode 文件格式见[流式加载](STREAMING.md)。

### 20.3 俯视角色的刚体控制

包含 `<renderer/physics2d/physics2d.hpp>`，链接 renderer::physics2d：

```cpp
Physics2D physics({.workerCount = 2});
Body2DDesc player;
player.type = Body2DType::Dynamic;
player.lockRotation = true;
player.shapes[0].radius = .25f;
player.shapes[0].friction = 0;
const auto body = physics.createBody(player);
// 每帧应用输入后推进；默认固定步 1/60 秒、零重力。
physics.setVelocity(body, {2, 0});
const auto step = physics.advance(dt);
const auto position = physics.state(body).position;
scene.setLocalTransform(playerRoot, Mat4::translation({position.x, 0, position.y}));
```

这里 playerRoot 是 Scene 根的直接子节点，物理平面的 Y 映射到世界 Z；有父变换/插值/旋转同步时使用模块提供的绑定与 synchronize 接口。若已在外部推进固定步，使用 step，不再重复 advance；持续力需要逐固定步施加。内部物理任务在 advance/step 返回时已完成，之后消费事件和修改场景。

角色一般用锁定旋转的 Dynamic 形状；Kinematic 不等同于“可与所有静态障碍碰撞的角色”。接触和 Sensor 事件中的 userData 映射业务身份，流式卸载时解除绑定/销毁刚体。物理没有自动导航/寻路，也不是三维刚体世界。详见[物理专章](PHYSICS2D.md)。

### 20.4 粒子、环境和淡出

粒子先 advance，再调用 batch(assets) 发布，返回值与 terrain/text 等批次一起输入 extractor。环境先 advance，再 apply 到太阳节点和 RenderView；天气强度变化由应用同步到粒子与音频。受击时调用 SpritePresentation::flash；遮挡淡出需要当前几何快照做查询，修改 Scene 后再提取最终快照。

每个模块都有容量、时间步和参数约束。暂停时可暂停模拟推进并继续 UI/渲染，音频是否暂停由业务决定；不要把“停止渲染”当作“所有系统已经暂停”。

## 21. 音乐、音效和应用事件

链接 renderer::audio，包含 `<renderer/audio/audio.hpp>`。音频独立于 Renderer/Window，可以只做音频或无声卡离线混音：

```cpp
AudioEngine audio;
const auto click = AudioClip::fromFile(assetRoot / "click.wav");
const auto music = AudioClip::fromFile(assetRoot / "music.flac", AudioLoadMode::Stream);
AudioPlayDesc background;
background.bus = AudioBus::Music;
background.loop = true;
background.fadeInSeconds = 1;
auto musicVoice = audio.play(music, background);
audio.setBusVolume(AudioBus::Master, .8f);
// 一次操作触发一次播放；在游戏事件处理里调用。
auto clickVoice = audio.play(click);
(void)clickVoice;
// 应用控制线程定期回收/取得结束事件。
audio.update();
```

AudioClip 是共享资源，VoiceHandle 是一次播放。短音效通常预加载并缓存资源，长音乐可流式；不要每帧从文件重新创建 Clip 或重复 play。流式音乐播放期间源文件必须保留可读。格式为 WAV/MP3/FLAC，当前没有额外的 Ogg/Opus 解码器。

步行事件触发脚步声，碰撞命中触发碰撞声，UI click 触发按钮声；位置、监听器和昼夜交叉淡变由应用调度。音频设备按独立时钟混音，不能拿渲染帧数决定实际播放进度，也不能在实时回调中做资源 IO、Scene 修改或 GPU 操作。加载可在后台准备，AudioEngine 控制集中在创建线程。

无声卡验证使用[音频示例](../examples/audio/main.cpp)的 --offline；空间音频、总线、淡入淡出、优先级和参数限制见[音频说明](AUDIO.md)。

## 22. 自定义材质、小地图和角色预览

### 22.1 材质函数与混合

高层支持 MaterialDesc::effect.vertex / surface 的 GLSL 函数体，用于风摆、颜色变化等；效果参数和时间通过共用接口提供。vertex 位移必须设置足够的 boundsPadding，否则物体可能在变形后被错误裁剪。不要在效果体中另写 main 或假设任意自定义描述符已绑定。

Alpha、PremultipliedAlpha、Additive 表达不同混合语义；素材的 Alpha 约定应与材质一致。时间驱动的树木摆动可只更新 view.timeSeconds，无需逐棵改模型矩阵。完整参数与 Shader 变量见[渲染扩展](RENDERING_EXTENSIONS.md)。

### 22.2 GPU RenderTexture

假定 mapCamera 已存在，mainView 是主视图，且已链接 ui 并创建 ui：

```cpp
const auto mapOutput = engine.createRenderTexture({300, 200});
RenderView mapView = mainView;
mapView.cameraNode = mapCamera;
mapView.overlay = {};
ui.registerImage("map", mapOutput); // 初始化时注册；RML 使用 ui:map。
// 每帧先画小地图，再构建主 UI 与主画面：
engine.renderToTexture(snapshot, mapView, mapOutput);
mainView.overlay = ui.snapshot();
engine.render(snapshot, mainView);
engine.present(); // 窗口模式；离屏不调用。
```

RenderTexture 内容直接留在 GPU，不需回读再上传。只能在创建它的 Renderer 内使用，不能同时作为当前视图的输入和输出；反馈效果采用两个输出轮换。尺寸不可变，调整分辨率时创建新目标并替换引用。额外相机增加附件、帧准备和绘制成本；小地图可用 cullingMask 只画地面和标记。

每个相机的 Billboard/像素字号文字按该视图重建批次。Renderer::readback 读取主输出；需要目标内容展示时优先 GPU 采样，单独相机截图可通过对应视图的 renderToImage 获取。

## 23. 将模块组成应用，管理关闭与保存

### 23.1 推荐的应用分层

```text
my_game/
  CMakeLists.txt
  src/
    main.cpp                  参数、路径和生命周期
    application.cpp           主循环与跨模块调度
    input.cpp                 UI 优先路由、输入状态
    world.cpp                 流式块/物件/业务身份
    audio_system.cpp          音频缓存、声音事件映射
  assets/
    textures/ models/ fonts/ ui/ audio/
  tests/                      应用自己的逻辑和图像测试
```

应用持有模块服务，业务对象保存 NodeHandle、Body/Voice/Label 身份及稳定业务 ID，不继承引擎内部后端。模块之间通过应用事件连接，避免让渲染核心直接控制玩法或音频。

### 23.2 一帧调度顺序

1. pollEvents，UI 先处理输入，应用清理 Release/失焦并生成角色/相机输入。
2. 按时间步推进业务与物理，处理接触/传感器；同步角色位置到 Scene。
3. 推进方向动画、处理脚步/受击事件；更新相机目标并 apply，之后再生成相机相关内容。
4. 推进环境/粒子，调用资源 publish 和流式 update；按区块事件维护物件、碰撞和环境声。
5. 更新音频控制、监听器和结束事件，整理本帧地图/粒子/文字 SpriteBatch。
6. 需要遮挡淡出时提取几何快照、更新 presentation，再提取包含效果的最终快照。
7. 先提交小地图等 RenderTexture，更新 UI/输入候选区域，设置 RenderView.overlay，提交主视图。
8. 窗口模式 present；按请求截图、记录统计；保存当前显示快照用于下一次屏幕拾取。

这是一条应用层参考顺序，按依赖调整；Renderer 不自动推进这些模块。完整 SDK 集成代码位于[森林营地应用](../examples/game/README.md)，它包含物理、方向动画、流式编辑/采集保存、昼夜天气、HUD、中文、PNG/WAV 和音画联动。

### 23.3 关闭和保存

关闭时先停止接收新业务任务/提交并取消事件订阅，再完成需要的存档和后台任务收尾，取得未消费 future 的结果/异常。Renderer 析构会排空自己的队列并清理 GPU；Window 应在其后析构。控制回调引用的 UI/游戏对象不能先于订阅令牌失效。

流式保存需检查成功/失败事件和错误信息，不以“已经发起 requestSave”代替“已经保存完成”。音频停止/设备销毁、资源解码线程等待各有独立生命周期；保留共享快照或资源缓存会延迟资源释放。内存变化应结合未完成工作、包身份、缓存和 VMA 块统计理解。

存档保存业务 ID、变换、资源逻辑路径/类型、动画状态和地图 payload；重新加载后重新创建 SDK 句柄。不保存 owner/index/generation、原生 GPU 指针或 PImpl 内存。

## 24. 运行素材、发布、升级与交付验收

### 24.1 路径和素材部署

第一帧程序只用程序几何，不需要素材目录。实际游戏需部署模型及其外部纹理/bin、图集/DDS、字体、RML/RCSS/图片和音频。相对路径默认取进程工作目录，不是自动取 exe 或 SDK 根目录；VS 启动目录、命令行启动和安装目录可能不同，应在应用启动时统一确定 assetRoot。

下面是应用已有 assets 目录时的局部 CMake 部署片段：

```cmake
add_custom_command(TARGET my_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:my_app>/assets")
```

不要把开发机绝对素材路径硬编码进发布包。Windows Unicode 路径优先使用 filesystem::path（必要时从宽字符串构造），与 UTF-8 文本编码分开处理。字体须有分发许可；SDK 的 LatoLatin 可作为英文示例，中文字体由应用选择并附带许可。FreeType/RmlUi 的存在不代表 SDK 自带完整中文字体。

`renderer_TEXT_FONT` / `renderer_UI_FONT` 是安装 SDK 内的示例字体路径，适合构建/测试。发布应用时应将需要的字体复制到自己的素材目录，并通过自己的 assetRoot 加载；不能依赖用户机器继续保留开发用 SDK。

### 24.2 运行时依赖和可迁移性

CMake 负责静态链接依赖，应用发布通常不需带 glslang/GLFW/VMA 等开发头文件或整个 SDK。需部署应用使用的素材、相关许可证以及与构建匹配的系统运行库；相应图形驱动和音频设备/系统服务由运行环境提供。不要把安装 Vulkan 开发 SDK 当作终端用户运行要求。

完整 SDK 目录可以复制到其他前缀，再重新指定 CMAKE_PREFIX_PATH 配置消费者。不要只搬主 .lib，丢失传递依赖、包配置或字体/示例；自定义 include/lib/share 目录时保留安装布局。SDK 同版本的头文件和库保持配套，新增值类型字段后重编应用，PImpl 不等于任意 ABI 兼容。

### 24.3 诊断和验收

先用第 3.4 节程序在 Release 下运行窗口/离屏，再加入业务资源，最后增加模块。遇到问题按第 14 节定位；以下是当前新增能力的常见情况：

| 现象 | 检查方向 |
| --- | --- |
| 所需组件未找到 | SDK 是否制作/安装该模块，配置是否找到了另一份旧 SDK |
| 只有第一次出现物体/文字时卡顿 | 解码、Shader/管线首次创建、字形栅格化及上传预算；复用/预热 |
| 部分物体等待数帧才出现 | pendingResourceDraws 与新资源预算，Published 不等于 GPU 完全就绪 |
| 中文方框或空白 | 字体覆盖率、回退列表、UTF-8；UI 与世界文字分别加载字体 |
| UI 无点击或拖出后状态异常 | 保存订阅令牌、初次布局、输入路由、DPI、Release 与失焦处理 |
| 小地图被拒绝或读写反馈 | Renderer 身份、目标存活、当前输出被同时采样 |
| 相机旋转后文字/点击错位 | 当前视图的批次与显示快照、客户区/像素换算 |
| 卸载后 VMA 块字节没有归零 | 活着的快照/提交、暂存池和空闲块；看 allocation 与 block 的区别 |
| 图形调试零错误但仍有异常 | 校验层是否实际启用；同时检查 CPU 异常、资源身份和像素输出 |

上线验收至少确认：两种实际支持后端的目标机器可运行；窗口缩放/最小化/全屏及 DPI 正常；固定帧截图符合预期；模块关闭时应用有明确配置错误或降级；资源包/流式卸载和退出能释放任务；素材路径与许可完整。SDK 文档应能从文档首页独立导航，示例可在另一个应用构建目录通过 find_package 编译。

性能记录固定设备、驱动、分辨率、场景、线程、阴影/OIT/FXAA、验证层和 vsync，区分 CPU、GPU、等待及首次使用；现有报告是特定负载的实测，不是引擎最大容量。Vulkan 提交复用与 VMA 分配器的 created/reused、allocation/block 字段见[诊断文档](DIAGNOSTICS.md)和[优化说明](OPTIMIZATION.md)。
