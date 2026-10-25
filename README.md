# Renderer

C++20 实时三维渲染引擎，支持 OpenGL 4.5 Core 与可选 Vulkan 1.3 后端，共用场景、Shader 与高层渲染管线。对外服务类统一使用 PImpl，公共头文件不包含原生图形 API 或第三方实现类型。

支持 [BC 压缩纹理与 DDS、纹理数组](docs/TEXTURES.md)，精灵可以逐实例选择数组层并合批；[世界文字](docs/TEXT.md)增加可选 SDF、缩放抗锯齿、描边及自动 LRU 缓存。[texture_text 示例](examples/texture_text/README.md)提供对照画面和 CPU DDS 打包工具，两种后端共用接口。

[性能优化](docs/OPTIMIZATION.md)提供批量异步传输与暂存池、场景缓存/BVH/拾取、共享固定精灵几何及 R8 字形局部更新。新的 [performance 示例](examples/performance/README.md)覆盖六类负载及参考模式，记录 P95/P99、GPU、上传和内存指标。

中文文档：[文档导航](docs/README.md) · [快速开始](docs/GETTING_STARTED.md) · [接口说明](docs/API_GUIDE.md) · [架构与扩展](docs/ARCHITECTURE.md) · [验证与排错](docs/VALIDATION.md)。

当前源码与 SDK 包版本为 0.2.0。近期新增能力和工程整理见[版本变更记录](CHANGELOG.md)，完整能力及边界见[实现清单](IMPLEMENTATION.md)。

[渲染基础能力综合示例](examples/features/README.md)展示异步图片/模型加载、资源包卸载与上传预算、CPU/GPU 内存统计和调试面板、帧槽实例增量上传、自定义 GLSL 材质与混合模式，以及直接用于 UI 的小地图/角色预览 GPU 纹理。文字支持预热/缓存回收，Windows UI 已接入组合编辑与系统剪贴板桥接。详见[资源加载](docs/ASSET_LOADING.md)、[渲染扩展](docs/RENDERING_EXTENSIONS.md)和[性能诊断](docs/DIAGNOSTICS.md)。

[森林营地综合示例](examples/game/README.md)将十二个可选模块组合为可操作应用：方向动画与脚步声、推箱子与碰撞声、流式地图和采集保存、火烟雨雪、昼夜配乐、遮挡淡出和拾取。运行 `renderer_game_demo --backend vulkan` 或 `--backend opengl`；支持离屏 PNG 和实际混音 WAV 输出。

[离屏渲染与 PNG](docs/OFFSCREEN.md)：无需应用窗口即可取得渲染图片。`Renderer::createVulkanOffscreen()` 完全跳过窗口/表面/交换链；Windows OpenGL 使用 WGL pbuffer。统一通过 `renderToImage(snapshot, view).savePng(path)` 保存，示例为 `renderer_offscreen_demo`。

二次开发请从[完整开发手册](docs/DEVELOPMENT_GUIDE.md)开始：包含 SDK 接入、场景/材质/模型、相机与输入、透明和线程、Shader/RHI/RenderGraph、性能排错与源码扩展，并附可以通过已安装 SDK 独立构建的[示例工程](examples/README.md)。

制作图片角色与三维地面结合的游戏，参见 [Sprite / Billboard 开发说明](docs/SPRITES.md)：支持图集、脚底锚点、方向序列帧、排序组及透明区域拾取。运行 `renderer_sprite_demo --threaded` 体验可移动、可选择物体的森林示例。

[图片世界模块](docs/WORLD_MODULES.md)支持分块瓦片、后台流式加载、连续圆形碰撞、图片粒子、昼夜天气、遮挡淡出、闪白、内描边和接触阴影。[Box2D 二维物理](docs/PHYSICS2D.md)作为独立 physics2d 模块提供刚体、复合形状、触发/接触事件、受力、查询、距离关节和场景同步。运行 `renderer_world_demo --backend opengl` 或 `--backend vulkan` 体验集成场景；地图和粒子通过 `SpriteBatch` 发布。

[精灵性能测试](docs/SPRITE_BENCHMARK.md)使用相同森林图集，扫描精灵数量和图集图片种类数，并对比 OpenGL/Vulkan 和单线程/独立渲染线程，支持预热、帧分位数、GPU Pass 计时与 CSV。运行 `renderer_sprite_benchmark --backend all --threads all`。

透明可按视图选择传统排序 Alpha 或 [Weighted Blended OIT](docs/OIT.md)：设置 `view.transparency = TransparencyMode::WeightedBlendedOIT`，示例使用 `--oit` 开启。

可选 [FXAA 抗锯齿](docs/ANTI_ALIASING.md)：设置 `view.antiAliasing = AntiAliasing::FXAA`，默认关闭；支持 OIT 和运行时切换，示例使用 `--fxaa` 或按 F 开关。

窗口支持 [键盘、鼠标与窗口事件订阅](docs/WINDOW_EVENTS.md)：`window.subscribe<KeyEvent>(handler)` 或统一订阅 WindowEvent，保存返回的 RAII 令牌，事件在主线程 pollEvents 中分派。

窗口也支持[无边框和独占全屏](docs/FULLSCREEN.md)：创建时配置或调用 `Window::setMode()` 动态切换，退出时恢复原窗口位置、尺寸和最大化状态。示例提供 `--borderless` / `--fullscreen`、F11 和 Alt+Enter。

源码为 UTF-8；公共接口和关键算法已补充中文注释，重点解释资源生命周期、场景版本、批处理、图编译及线程同步。

模块已拆分为 `renderer::core`、`renderer::opengl`、可选的 `renderer::vulkan`，以及 animation、interaction、audio、tilemap、collision、particles、environment、presentation、physics2d、camera、ui、text 十二个可选模块。应用显式链接所需后端与扩展；构建开关和迁移方式见[模块说明](docs/MODULES.md)。

`renderer::camera` 提供轨道、跟随和自由相机，封装姿态计算、平滑、限幅及父节点写回。应用只传入目标和设备无关输入，综合游戏已接入，见[相机控制文档](docs/CAMERA.md)。

`renderer::ui` 使用 RmlUi/FreeType 提供游戏基础控件、文档/皮肤、文字和输入路由，输出不可变覆盖层，在双后端的 ToneMap/FXAA 后合成。综合游戏已有实际 HUD 和控制按钮，另有独立窗口/离屏示例，见[UI 文档](docs/UI.md)。

`renderer::text` 提供独立世界文字：字体与回退、字形图集/排版、节点绑定、三种 Billboard、固定屏幕字号和遮挡开关。综合游戏已加入名字牌、火堆文字和上浮伤害数字，见[世界文字文档](docs/TEXT.md)。

[音乐与音效](docs/AUDIO.md)使用 miniaudio：音乐流式读取、短音效叠加、分组音量/静音、淡入淡出和基础空间音频。运行 `renderer_audio_demo` 播放演示声，或加 `--offline` 进行无声卡混音检查。

源码的全部[示例](examples/README.md)由 `RENDERER_BUILD_EXAMPLES` 控制，根工程可直接构建，无需先安装 SDK；[测试](tests/README.md)由 `RENDERER_BUILD_TESTS` 总开关控制，图形测试另设 `RENDERER_GL_TESTS` / `RENDERER_VULKAN_TESTS`。例如仅构建库：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_TESTS=OFF
cmake --build --preset windows-release --parallel 6
```

## 已实现

| 模块 | 能力 |
| --- | --- |
| 场景 | 安全节点句柄、父子变换、可见性、层过滤、MeshRenderer、透视/正交 Camera、方向/点/聚光 Light |
| 资源 | 不可变 Mesh/Texture/Material、submesh、材质槽、可重复实例化 Model、资源独立的渲染快照 |
| 图片与动画 | Sprite/图集、Fixed/Upright/ViewPlane、裁剪与翻转、落地点/部件排序、1/4/8 方向序列帧及事件、屏幕射线与 Alpha 拾取 |
| 图片世界 | 不可变精灵批次、分块/流式地图与持久化、圆/AABB 连续移动、固定步图片粒子、昼夜天气、遮挡淡出/闪白/内描边 |
| 二维物理 | 独立 Box2D 模块，刚体、复合形状、固定步、事件、查询、距离关节与场景同步 |
| 离屏输出 | Vulkan 无窗口渲染、Windows OpenGL pbuffer、独立尺寸、同步回读与 PNG/PPM 保存 |
| 音频 | 独立 miniaudio 模块、WAV/MP3/FLAC、音乐流、并发音效、混音分组、包络、优先级、空间定位与离线混音 |
| RHI | Buffer、Texture/View、Sampler、Graphics/Compute Pipeline、BindingGroup、命令列表、直接/间接绘制、Dispatch、资源拷贝、Barrier、Fence、Timestamp、能力查询 |
| RenderGraph | 读写依赖、范围和初始化验证、环/反馈检测、无效 Pass 剔除、临时资源物理复用、可缓存的编译图 |
| 材质 | metallic/roughness PBR、基色/法线/MR/AO/自发光纹理、不透明/Alpha Mask/透明、双面、unlit |
| 光照与后处理 | 多灯光、方向光阴影与 3×3 PCF、RGBA16F HDR、排序 Alpha / 可选加权 OIT、ACES/Reinhard/直通 ToneMap、可选 FXAA、线性调色、屏幕空间接触阴影、sRGB 输出 |
| 导入 | 静态 glTF 2.0/GLB、OBJ/MTL、PNG/JPEG 等图片；层级、材质、相机、KHR_lights_punctual |
| 性能与线程 | 视锥裁剪、实例化批处理、Shader/Program/Pipeline 缓存、帧上传缓冲、延迟销毁、有界异步提交、独立渲染线程、GPU 分 Pass 计时 |

两种后端共用同一条高层渲染链路。Vulkan 的开关、窗口创建、交换链和 SDK 接入见 [Vulkan 说明](docs/VULKAN.md)，总体设计见[架构说明](docs/ARCHITECTURE.md)。

## 构建

需要 CMake 3.25+、C++20 编译器；默认 OpenGL 后端需要 4.5 Core 驱动，Vulkan 后端需要 Vulkan 1.3。启用 Box2D 的源码构建还需要 C17 编译器。Windows 预设使用 Visual Studio 2022 工具链。当前实际验证为 Windows + NVIDIA，其他平台尚未实机验证。

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
.\build\windows\Debug\renderer_demo.exe --threaded
```

Release：

```powershell
cmake --build --preset windows-release
.\build\windows\Release\renderer_demo.exe --threaded
```

首次配置会下载经 SHA256 校验的 glslang、SPIRV-Cross，以及启用功能需要的 GLFW 和 Box2D 源码。GLAD/Volk、TinyGLTF/JSON/stb 图片编解码和 miniaudio 源码已包含在项目中。默认 OpenGL 构建不需要 Vulkan SDK；启用 Vulkan 的源码构建需要开发头文件。固定版本、管理方式和离线覆盖见[第三方依赖](THIRD_PARTY.md)。正常构建不需要 Python 或额外 Shader 编译器可执行文件。

同时启用 Vulkan：

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON
cmake --build --preset windows-release --parallel 6
.\build\windows\Release\renderer_vulkan_demo.exe --threaded --oit --fxaa
```

可通过 FetchContent 源码覆盖参数离线构建：

```powershell
cmake --preset windows -DFETCHCONTENT_SOURCE_DIR_GLFW=C:/deps/glfw-3.4 -DFETCHCONTENT_SOURCE_DIR_GLSLANG=C:/deps/glslang-15.4.0 -DFETCHCONTENT_SOURCE_DIR_SPIRV_CROSS=C:/deps/SPIRV-Cross-vulkan-sdk-1.4.321.0
```

仅构建 CPU 核心、Shader 工具链、导入器和 CPU 测试：

```powershell
cmake -S . -B build/core -DRENDERER_BUILD_OPENGL=OFF -DRENDERER_BUILD_VULKAN=OFF -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_ANIMATION=OFF -DRENDERER_BUILD_INTERACTION=OFF -DRENDERER_BUILD_AUDIO=OFF -DRENDERER_BUILD_TILEMAP=OFF -DRENDERER_BUILD_COLLISION=OFF -DRENDERER_BUILD_PARTICLES=OFF -DRENDERER_BUILD_ENVIRONMENT=OFF -DRENDERER_BUILD_PRESENTATION=OFF -DRENDERER_BUILD_PHYSICS2D=OFF -DRENDERER_BUILD_CAMERA=OFF -DRENDERER_BUILD_UI=OFF -DRENDERER_BUILD_TEXT=OFF
cmake --build build/core --config Debug
ctest --test-dir build/core -C Debug --output-on-failure
```

其他平台：

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release
cmake --build build/local --parallel
ctest --test-dir build/local --output-on-failure
```

产物为静态库。场景、导入、Shader、RHI 接口及图编译链接 `renderer::core`；使用图形设备工厂或 Renderer 时链接 `renderer::opengl` 或 `renderer::vulkan`。

支持安装 SDK 并通过 `find_package(renderer CONFIG REQUIRED COMPONENTS opengl)` 接入外部工程：

```powershell
cmake --preset windows -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer
cmake --build --preset windows-release --parallel 6
cmake --install build/windows --config Release
```

公共头文件、静态库及传递依赖安装到指定前缀；Debug/Release 可共存。详细用法见 [SDK 安装与二次开发](docs/SDK.md)。

## 源码目录与版本管理

```text
include/renderer/  SDK 公共接口
src/              核心、共用管线、窗口平台与 OpenGL/Vulkan 后端
modules/          十二个可选模块（动画、交互、音频、地图、碰撞、粒子、环境、表现、二维物理、相机、UI、世界文字）
examples/         应用示例与精灵性能测试
tests/            单元/集成测试及必要素材
cmake/            SDK 包配置
docs/             中文使用、架构和验证文档
third_party/      随仓库保存的固定依赖源码和许可
```

Git 跟踪源码、构建配置、文档、第三方原文和测试素材。`build/`、`.cache/`、IDE 本机文件及生成的 SDK/性能报告由忽略规则管理，保留在本地。文本行尾由 `.gitattributes` 统一为 LF，第三方文件按原始字节保存；OBJ/GLB/图片等测试素材纳入提交。首次检出后的构建方式使用上文命令，第三方下载版本及离线覆盖方式见[依赖说明](THIRD_PARTY.md)。

## 场景与渲染

```cpp
#include <renderer/renderer.hpp>
#include <renderer/window.hpp>

using namespace renderer;
Window window;
auto renderer = Renderer::createOpenGL(window, {.renderThread = true});
AssetStore assets;
Scene scene;
RenderExtractor extractor;

auto mesh = assets.createMesh(geometry::sphere());
auto material = assets.createMaterial({
    .baseColor = {0.9f, 0.55f, 0.15f, 1},
    .metallic = 0.8f,
    .roughness = 0.25f,
});
auto group = scene.createNode(scene.root(), "Group");
auto object = scene.createNode(group, "Sphere");
scene.setComponent(object, MeshRenderer{mesh, {material}});

auto camera = scene.createNode(scene.root(), "Camera");
scene.setComponent(camera, Camera{});
scene.setLocalTransform(camera, *Mat4::lookAt({3, 2, 5}, {0, 0, 0}).inverse());

while (!window.shouldClose()) {
    window.pollEvents();
    auto snapshot = extractor.extract(scene, assets);
    auto stats = renderer.render(snapshot, {
        .cameraNode = camera,
        .shadows = true,
        .toneMapping = ToneMapping::ACES,
    });
    renderer.present();
}
```

`createNode(parent)` 建立层级，`setComponent<T>()` 添加或替换组件，`component<T>()` 返回副本。修改局部变换会按版本更新受影响的世界变换。`reparent()` 支持 KeepLocal/KeepWorld，一般仿射矩阵可保留剪切；无效父节点、环、奇异 KeepWorld 和根节点删除会报错。`destroySubtree()` 迭代删除，不依赖递归深度。

节点和资源句柄校验 owner/index/generation。可见性向子节点继承；相机 cullingMask 与 MeshRenderer.layerMask 决定视图过滤。MeshRenderer.castShadow/receiveShadow 可单独控制。没有显式灯光时使用 RenderView 的默认方向光；显式灯光节点的局部 -Z 为照射方向。

Scene、AssetStore、RenderExtractor 按单写线程使用。RenderSnapshot 可复制、不可变，持有解析后的资源引用；原场景、资源句柄甚至 AssetStore 被销毁后，既有快照仍可渲染。

## 异步提交

```cpp
auto future = renderer.submit(extractor.extract(scene, assets), view);
// 此处可以继续处理应用逻辑、修改 Scene、准备下一帧。
auto stats = future.get();
renderer.present();
```

RendererConfig.renderThread 开启独立渲染线程，maxQueuedFrames 控制有界队列。render() 是 submit().get() 的便利形式。线程中的错误通过 future 传回。窗口事件、尺寸查询和缩放保留在创建窗口的主线程；GPU 调用、呈现和资源销毁在上下文线程执行。

开启渲染线程时使用 Renderer::present()，Window::present() 会提示线程归属错误。Renderer 析构会排空队列、完成 GPU 工作，并在主线程释放保留的窗口生命周期。

## 导入模型与图片

```cpp
#include <renderer/model.hpp>
auto model = assets.loadModel("assets/scene.glb");
auto instance = model.instantiate(scene, scene.root(), "Imported");
scene.setLocalTransform(instance.root, Mat4::translation({0, 0, -5}));
// instance.nodes / cameras / lights 可用于选择和修改导入的节点。
auto texture = assets.loadTexture("assets/albedo.png", true);
```

glTF 支持外部/嵌入缓冲、interleaved/normalized/sparse accessor、三角形/strip/fan、材质纹理和采样器、节点 TRS/矩阵、透视/正交相机，以及 unlit、emissive-strength、punctual-light 扩展。缺失法线会生成。OBJ 支持负索引、凹多边形三角化、材质槽和基础 MTL 参数。

导入器的范围为静态三角网格：不支持的必需 glTF 扩展、动画/蒙皮、压缩/形变几何、非 TEXCOORD_0 或纹理变换会明确报错。OBJ 的高级 MTL 贴图选项也会报错。文件导入失败时回滚已创建的资源。无限远 glTF 相机/灯光使用当前静态场景配置的有限范围。

## RHI / RenderGraph / Shader

公共接口分别位于 `include/renderer/rhi.hpp`、`render_graph.hpp` 和 `shader.hpp`。低层 Device 与高层 Renderer 独占各自窗口的上下文，同一窗口只创建其中一个。

ShaderCompiler 将带显式 set/binding 的 GLSL 450 编译为 SPIR-V，返回绑定、Block 大小与成员偏移反射。定义支持 NAME 或 NAME=VALUE，文件编译支持 includeDirectories。缓存键使用预处理后的源码和阶段。创建 RHI Pipeline 时自动合并阶段布局；OpenGL 转译并映射为紧凑绑定点，Vulkan 直接使用 SPIR-V 和描述符组。

下面仅示意访问声明与依赖，不包含实际计算命令；可直接执行的 Buffer 拷贝示例见[接口说明](docs/API_GUIDE.md#rhi-与-rendergraph)。

```cpp
RenderGraph graph;
auto intermediate = graph.createBuffer({.size = 1024});
graph.addPass("Produce", {{intermediate, rhi::Access::StorageWrite, rhi::Stage::Compute}},
    [&](rhi::CommandEncoder& commands, const GraphResources& resources) {
        auto buffer = resources.buffer(intermediate);
        // 将 buffer 放入 StorageBuffer 绑定组，绑定 Compute Pipeline 并 dispatch。
    });
graph.addPass("Consume", {{intermediate, rhi::Access::TransferRead, rhi::Stage::Transfer}},
    [&](rhi::CommandEncoder& commands, const GraphResources& resources) {
        // 录制读取 intermediate 的命令。
    }, true);
auto compiled = graph.compile();
auto completion = compiled.execute(device);
device.wait(completion);
```

Pass 按资源生产顺序声明，可用 dependsOn() 增加显式依赖。范围跟踪以纹理 mip 和 Buffer 字节为单位；exportResource() 和有副作用的 Pass 保留输出依赖。兼容且生命周期不重叠的临时资源复用物理对象。编译图可重复执行，持有的设备状态不因 Device 包装对象移动而失效。回调只应解析该 Pass 声明过的图资源；外部可变资源必须导入并声明访问，外部互相别名的纹理视图需由调用者统一建模。

当前 RHI 桌面基础 Profile 使用 2D/二维数组纹理与桌面 BC 压缩、uint32 索引、float 顶点属性、UBO/SSBO、组合采样纹理与 Storage Image。描述符数组等超出 Profile 的请求会明确拒绝。能力、格式支持、对齐、绑定数量可查询；提交的资源访问由上层声明。创建/提交/销毁 Device 资源在设备上下文线程执行，CommandEncoder 可在工作线程独立录制。

## 诊断与验证

RenderStats 提供场景/阴影/透明 draw call、三角形、上传量、资源数及 CPU 准备/提交时间。drawCalls 包含阴影、场景、ToneMap 和可选的 OIT Resolve/FXAA；合成和抗锯齿分别有独立计数。triangles 为场景绘制三角形。提交时间可能包含帧槽等待。GPU 时间来自已完成的旧帧，通过 gpuTimingValid 和 passes 判断有效性。

数学约定：右手系、列主序、列向量、NDC 深度 [0,1]，LESS/清除 1。材质颜色在线性空间计算，颜色纹理按 sRGB 解码，最终输出显示编码颜色。独立图片导入转成左下原点，glTF 保留其 UV/像素配套约定。readback() 返回左上原点 RGBA8，可用 savePng()/savePpm() 保存。

```powershell
cmake --preset windows-gpu-tests
cmake --build --preset windows-gpu-tests
ctest --preset windows-gpu-tests --verbose
```

完整双后端配置包含 93 项 CTest 检查，覆盖核心、十二个扩展、窗口和离屏渲染、综合应用及安装后的 SDK 使用方式。窗口测试大多使用隐藏窗口，全屏检查会短暂显示窗口；Vulkan 离屏检查不初始化 GLFW、无需显示服务。复现命令与实机验证范围见[验证说明](docs/VALIDATION.md)。

```powershell
.\build\windows\Release\renderer_demo.exe --threaded --hidden --frames 30 --capture build/demo.ppm
.\build\windows\Release\renderer_demo.exe --threaded --model tests/assets/triangle.glb
```

示例包含共享实例场、PBR 金属/粗糙度球、透明物体、层级小车、方向/点光源和阴影。空格暂停，Esc 退出。--hidden 必须与正数 --frames 配合；有限帧模式使用固定动画步长。

第三方来源与许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
