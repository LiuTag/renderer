# 快速开始

[返回文档导航](README.md)

## 1. 准备环境

- CMake 3.25 或以上，以及支持 C++20 的编译器。
- Windows 预设使用 Visual Studio 2022 的桌面 C++ 工具链，目标为 x64。
- 运行窗口示例和 GPU 测试需要相应的 OpenGL 4.5 Core 或 Vulkan 1.3 驱动及桌面环境。
- 首次配置需联网获取 GLFW、glslang、SPIRV-Cross。默认 OpenGL 构建不需要 Vulkan SDK；启用 Vulkan 的源码构建需要开发头文件，安装后的 SDK 使用者不需要。两种构建都不需要 Python。

以下命令从仓库根目录执行。其他平台的构建路径存在，但本机验证范围仅包括 Windows，详见[验证记录](VALIDATION.md)。

## 2. 构建并运行已有示例

```powershell
cmake --preset windows
cmake --build --preset windows --parallel 6
ctest --preset windows
.\build\windows\Debug\renderer_demo.exe --threaded
```

`windows` 为多配置生成器，默认构建预设选择 Debug。Release 继续使用同一个构建目录：

```powershell
cmake --build --preset windows-release --parallel 6
.\build\windows\Release\renderer_demo.exe --threaded
```

示例包含共享网格实例、PBR 球体、透明面板、层级小车、灯光和阴影。空格暂停动画，F11 切换无边框全屏，Alt+Enter 切换独占全屏，Esc 退出。详细约定见[全屏说明](FULLSCREEN.md)。

| 参数 | 含义 |
| --- | --- |
| `--threaded` | 将上下文转移到独立渲染线程 |
| `--frames 30` | 渲染指定帧数后退出，动画使用固定步长 |
| `--hidden` | 隐藏窗口，必须同时设置正数 `--frames` |
| `--capture build/frame.ppm` | 回读最后一帧并保存为 PPM，示例会创建父目录 |
| `--model tests/assets/triangle.glb` | 加载额外的静态模型 |
| `--instances 900` | 设置实例场数量 |
| `--no-shadows` | 关闭阴影 |
| `--oit` | 开启 Weighted Blended OIT，默认仍为排序 Alpha |
| `--fxaa` | 开启最终画面的 FXAA 抗锯齿，也可以按 F 切换 |
| `--no-vsync` | 关闭垂直同步 |
| `--borderless` | 以桌面分辨率的无边框全屏启动 |
| `--fullscreen` | 以独占全屏启动，要求可见窗口 |
| `--monitor 0` | 指定全屏显示器下标，默认自动选择 |

自动运行并截图：

```powershell
.\build\windows\Release\renderer_demo.exe --threaded --hidden --frames 30 --capture build/frame.ppm
```

Vulkan 构建、示例与 `GraphicsApi::Vulkan` 的窗口创建方式见 [Vulkan 专章](VULKAN.md)。相机、场景、资源和帧流程共用下文接口。

## 3. 创建最小应用

下面可直接保存为应用的 `main.cpp`。窗口先创建，渲染器后创建，使渲染器先析构；事件处理始终留在应用主线程。

```cpp
#include <renderer/renderer.hpp>
#include <renderer/window.hpp>
#include <iostream>

int main() {
    using namespace renderer;
    try {
        Window window({.width = 960, .height = 640, .title = "First scene"});
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        auto engine = Renderer::createOpenGL(window, {.renderThread = true});

        const auto mesh = assets.createMesh(geometry::cube());
        const auto material = assets.createMaterial({
            .baseColor = {0.85f, 0.35f, 0.12f, 1},
            .metallic = 0.2f,
            .roughness = 0.4f,
        });
        const auto group = scene.createNode(scene.root(), "Group");
        const auto object = scene.createNode(group, "Cube");
        scene.setComponent(object, MeshRenderer{mesh, {material}});

        const auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{});
        // lookAt 返回视图矩阵，相机节点需要其逆矩阵作为世界姿态。
        scene.setLocalTransform(camera, *Mat4::lookAt({3, 2, 5}, {0, 0, 0}).inverse());

        RenderView view;
        view.cameraNode = camera;
        view.toneMapping = ToneMapping::ACES;
        // 无显式 Light 时使用 RenderView 提供的默认方向光。
        float angle = 0;
        while (!window.shouldClose()) {
            window.pollEvents();
            angle += 0.01f;
            scene.setLocalTransform(group, Mat4::rotationY(angle));
            auto snapshot = extractor.extract(scene, assets);
            engine.render(snapshot, view);
            engine.present();
        }
        engine.waitIdle();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

接入其他 CMake 工程时，例如将本仓库置于该工程的 `third_party/renderer`：

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_renderer_app LANGUAGES C CXX)
set(RENDERER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RENDERER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/renderer)
add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE renderer::opengl)
if(MSVC)
    target_compile_options(my_app PRIVATE /utf-8)
endif()
```

`renderer::opengl` 会传递核心库及公共头文件路径。仅使用场景、导入、Shader 和图编译时可链接 `renderer::core`；调用窗口或 OpenGL 工厂需要链接 OpenGL 目标。也支持安装 SDK 后使用 `find_package(renderer CONFIG REQUIRED COMPONENTS opengl)` 接入，安装目录和完整示例见 [SDK 安装与二次开发](SDK.md)。

## 4. 按需裁剪构建

| 选项 | 默认值 | 含义 |
| --- | --- | --- |
| `RENDERER_BUILD_OPENGL` | ON | 构建 OpenGL 后端和窗口层 |
| `RENDERER_BUILD_VULKAN` | OFF | 构建 Vulkan 1.3 后端，源码构建需要 Vulkan 开发头文件 |
| `RENDERER_BUILD_ANIMATION` | ON | 构建独立动画扩展，只依赖 core |
| `RENDERER_BUILD_INTERACTION` | ON | 构建独立交互扩展，只依赖 core |
| `RENDERER_BUILD_AUDIO` | ON | 构建独立 miniaudio 音频扩展，离线测试无需声卡 |
| `RENDERER_BUILD_EXAMPLES` | ON | 编译源码 examples 下全部可用示例，无需预先安装 SDK；自动跳过缺失模块的图形示例 |
| `RENDERER_BUILD_TESTS` | ON | 测试总开关，控制 tests 下的测试程序和示例运行检查；OFF 时不编译或注册任何测试 |
| `RENDERER_GL_TESTS` | OFF | 测试总开关开启时增加窗口/GPU 测试及图形示例检查，需要 OpenGL |
| `RENDERER_VULKAN_TESTS` | OFF | 测试总开关开启时增加 Vulkan GPU/呈现检查；两种后端存在时增加像素对比 |
| `RENDERER_INSTALL_SDK` | ON | 安装公共头文件、库、静态依赖和 CMake 包配置 |

旧 `RENDERER_BUILD_DEMO` 名称仅用于已有缓存首次迁移时初始化新开关；显式设置 `RENDERER_BUILD_EXAMPLES` 优先。示例与测试分别由 `examples/CMakeLists.txt`、`tests/CMakeLists.txt` 管理，目标清单和组合用法见[示例说明](../examples/README.md)、[测试说明](../tests/README.md)。

```powershell
# 只构建示例。
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_TESTS=OFF
# 只构建测试。
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_TESTS=ON -DRENDERER_GL_TESTS=OFF
```

不需要显示环境的 CPU 构建：

```powershell
cmake -S . -B build/core -DRENDERER_BUILD_OPENGL=OFF -DRENDERER_BUILD_VULKAN=OFF -DRENDERER_BUILD_EXAMPLES=OFF
cmake --build build/core --config Debug --parallel 6
ctest --test-dir build/core -C Debug --output-on-failure
```

CPU 核心仍依赖 Shader 工具链。离线环境需提前准备相应版本的依赖源码，并使用 FetchContent 覆盖路径：

```powershell
cmake --preset windows -DFETCHCONTENT_SOURCE_DIR_GLFW=C:/deps/glfw-3.4 -DFETCHCONTENT_SOURCE_DIR_GLSLANG=C:/deps/glslang-15.4.0 -DFETCHCONTENT_SOURCE_DIR_SPIRV_CROSS=C:/deps/SPIRV-Cross-vulkan-sdk-1.4.321.0
```

关闭 OpenGL 时不需要 GLFW。依赖版本与许可见[第三方依赖](../THIRD_PARTY.md)，后续用法见[接口说明](API_GUIDE.md)。
