# 二次开发手册

本文面向使用 Renderer SDK 编写三维应用、模型查看器和自定义渲染流程的 C++ 开发者。内容以当前 **0.2.0、C++20、OpenGL 4.5 Core / Vulkan 1.3** 实现为准。示例默认使用 OpenGL，切换 Vulkan 只需调整窗口配置和工厂；阅读本文不需要接触原生图形 API 或 Shader 编译器的内部类型。

完成本手册中的示例后，可以独立完成：安装 SDK、创建交互场景、组织节点和资源、控制相机、导入模型、选择透明模式、处理异步提交，以及通过 RHI 和 RenderGraph 实现自定义 GPU 工作。

[返回文档导航](README.md) · [安装目录与 CMake 详解](SDK.md) · [公共接口速查](API_GUIDE.md)

## 阅读路线

| 开发目标 | 建议阅读 |
| --- | --- |
| 第一次接入并显示三维物体 | 第 1–4 节，以及 `development_scene` |
| 不创建应用窗口并保存渲染图片 | [离屏渲染与 PNG](OFFSCREEN.md)，以及 `examples/offscreen` |
| 模型查看器、交互相机和编辑工具 | 第 5–9 节，以及 `scene_app.cpp` |
| 图片角色、树木与三维地面结合的游戏 | [Sprite 专章](SPRITES.md)，以及 `development_sprite` |
| 播放音乐、音效或添加空间声音 | [音频专章](AUDIO.md)，以及 `renderer_audio_demo` |
| 自定义 Shader 和 GPU 计算 | 第 10–11 节，以及 `development_compute` |
| 自定义绘制、后处理和渲染图 | 第 11–12 节，以及 `development_graph` |
| 优化性能、管理内存和定位错误 | 第 9、13、14 节 |
| 修改引擎源码或扩展接口 | 第 15 节和[架构说明](ARCHITECTURE.md) |

下文的短代码片段是局部用法，除特别说明外使用 `using namespace renderer;`，并假定对应的 `scene`、`assets`、`window`、`engine` 等对象已经创建。需要直接编译运行时，使用第 2 节的完整配套工程。

## 1. 开发方式与能力边界

### 1.1 选择接入层级

| 方式 | 应用负责什么 | 引擎负责什么 | 适用情形 |
| --- | --- | --- | --- |
| 高层 `Renderer` | 创建资源、修改 Scene、选择 RenderView、提交快照 | 裁剪、排序、合批、光照、阴影、透明、ToneMap、GPU 缓存和同步 | 大多数三维应用和模型查看器 |
| `rhi::Device` + `CommandEncoder` | 定义 Shader、管线、资源、命令及访问屏障 | 原生 API 映射、命令验证、完成标记及延迟销毁 | Compute、专用绘制、已有渲染框架接入 |
| RHI + `RenderGraph` | 声明 Pass、完整访问关系和命令回调 | 依赖分析、无效 Pass 剔除、屏障和临时资源复用 | 多阶段绘制、后处理、自定义渲染流水线 |

同一窗口在同一时间只能绑定一个高层 Renderer 或一个低层 Device。高层 Renderer 不公开其内部 Device、渲染图和颜色目标，SDK 当前也没有向高层流水线注册任意 Pass 的接口。需要完全控制 Shader 或增加绘制阶段时，可以独立使用 RHI/RenderGraph，或者按第 15 节修改引擎源码。

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

对外服务类使用 PImpl，数学类型、句柄和描述结构是普通值类型。PImpl 隐藏实现依赖，但公共接口仍使用 `std::string`、容器、future 等 C++ 类型；应使用与 SDK 兼容的编译器、架构和运行库，不把它当成跨语言 C ABI。

### 1.3 当前可用范围

当前实现静态场景、PBR、多种灯光、一个方向光阴影、两种透明策略、可选 FXAA、图片及静态模型导入、图形/计算 RHI 和渲染图；支持 OpenGL 及可选 Vulkan 后端。后者的构建、窗口和 SDK 用法见 [Vulkan 专章](VULKAN.md)。

音乐与音效通过独立 `renderer::audio` 接入，不占用窗口或图形设备；默认启用，可通过 `RENDERER_BUILD_AUDIO=OFF` 裁剪。音频事件与游戏/动画事件的连接由应用负责，详见[音频说明](AUDIO.md)。

SDK 已提供 Sprite/Billboard、方向序列帧及精灵拾取，详见 [Sprite 专章](SPRITES.md)。当前没有模型动画/骨骼/蒙皮、逐像素精确透明、折射、通用 ECS、自定义组件运行时注册、场景文件保存、普通网格拾取、UI 框架绑定或原生窗口句柄嵌入接口。应用可以在自身业务层组织对象、输入映射、保存格式和相机控制；需要新增 GPU 能力时再扩展引擎。

## 2. 安装 SDK 并运行配套工程

### 2.1 构建和安装

在引擎源码根目录执行：

```powershell
cmake --preset windows -DCMAKE_INSTALL_PREFIX=C:/SDK/renderer
cmake --build --preset windows-release --parallel 6
cmake --install build/windows --config Release
```

SDK 的 `include` 保存公共头文件，`lib/Release` 保存库和静态传递依赖，`lib/cmake/renderer` 保存包配置，`share/renderer` 保存文档、示例及许可。安装路径覆盖、Debug/Release 共存、仅核心包和离线构建见 [SDK 说明](SDK.md)。

### 2.2 自己的应用工程

创建 `my_app/CMakeLists.txt` 和 `my_app/main.cpp`：

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

然后配置应用，不需要把引擎源码复制到工程内：

```powershell
cmake -S my_app -B my_app/build -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build my_app/build --config Release
```

`renderer::opengl` 自动传递 `renderer::core`、C++20 要求、公共 include 目录及所需的静态依赖。不要手工枚举 glslang/GLFW 等库，不要链接 `renderer::_...` 内部目标。Windows 默认 Release 使用 `/MD`，Debug 使用 `/MDd`；应用和 SDK 应匹配配置。

只使用 CPU 场景、资源、模型、Shader 编译或图编译时，申请 `COMPONENTS core` 并链接 `renderer::core`。打开窗口和调用任何 OpenGL 工厂需要 OpenGL 组件，即使窗口隐藏也一样。

### 2.3 四个完整示例

源码工程位于 [examples/development](../examples/development/CMakeLists.txt)，安装后位于 `C:/SDK/renderer/share/renderer/examples/development`。根工程开启 `RENDERER_BUILD_EXAMPLES` 时直接复用本次构建的引擎目标；单独配置示例目录时通过 `find_package` 使用已安装 SDK。

无需安装 SDK，即可从源码根目录运行：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=ON
cmake --build --preset windows-release --parallel 6
.\build\windows\Release\development_scene.exe
.\build\windows\Release\development_sprite.exe --threaded
```

示例和测试开关彼此独立，完整用法见[示例说明](../examples/README.md)、[测试说明](../tests/README.md)。安装后仍可使用独立工程：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/development -B build/development -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build/development --config Release --parallel 6
ctest --test-dir build/development -C Release --output-on-failure
```

四个测试都需要 OpenGL 驱动和桌面环境；隐藏窗口不等同于无图形上下文。需要完全不使用显示环境的检查时，使用 [SDK 核心包示例](SDK.md)。

森林示例需安装 animation 和 interaction 扩展；独立示例工程会按可用组件决定是否提供它。扩展的目录、开关、显式链接及 CPU SDK 验证见[模块说明](MODULES.md)。

| 程序 | 完整源码 | 内容与预期结果 |
| --- | --- | --- |
| `development_scene` | [scene_app.cpp](../examples/development/scene_app.cpp) | 棋盘纹理立方体、地面、透明外壳、方向光、轨道相机、独立渲染线程 |
| `development_compute` | [compute_app.cpp](../examples/development/compute_app.cpp) | GPU 生成 16 个整数，回读并核对 `7 10 13 ... 52` |
| `development_graph` | [graph_app.cpp](../examples/development/graph_app.cpp) | 自定义三角形 Pipeline、临时纹理、拷贝、呈现、剔除无用 Pass 和时间戳 |
| `development_sprite` | [sprite_app.cpp](../examples/development/sprite_app.cpp) | 图片森林、方向行走、帽子部件、落地点排序、Alpha 拾取、地面阴影和简单障碍 |

交互场景启动方式：

```powershell
.\build\development\Release\development_scene.exe
.\build\development\Release\development_scene.exe --hidden --frames 4 --capture build/first.ppm
.\build\development\Release\development_scene.exe --model C:/assets/scene.glb
```

| 操作 | 效果 |
| --- | --- |
| 鼠标右键拖动 | 围绕目标点旋转相机 |
| 滚轮 | 调整轨道距离 |
| W/S、A/D | 沿当前水平视向前后、左右平移目标点 |
| Q/E | 向下、向上移动目标点 |
| Space | 暂停/恢复立方体旋转，仅首次 Press 切换 |
| O | 切换排序 Alpha 与加权 OIT |
| F | 切换 FXAA 抗锯齿，默认关闭，也可用 --fxaa 启动 |
| Esc | 通过窗口默认逻辑请求关闭 |

隐藏运行必须提供正的 `--frames`。`--capture` 输出 PPM 并创建父目录；`--model` 导入模型并把实例平移到 X=2，模型自身尺寸和朝向保持其导入结果，应用可按第 6 节继续调整。

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

`extract()` 解析当前场景和资源；它不会执行 GPU 命令。`render()` 完成 CPU 帧准备和 GPU 命令提交；它不等于 GPU 执行结束。`present()` 才交换窗口缓冲。不要漏掉任意一步。

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

手工 TextureDesc 的像素字节数必须等于 `width * height * 4`，尺寸为正且每边不超过 16384。`loadTexture()` 会把常见图片解码并转换为左下原点；模型导入遵循模型 UV 与图片的配套约定，不要再给 glTF 的纹理统一额外翻转。

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

相机控制器属于应用层。配套 `scene_app.cpp` 提供完整轨道相机：右键拖动修改 yaw/pitch、滚轮修改距离、键盘平移目标，再用 `lookAt()` 生成姿态。当前 SDK 没有鼠标捕获或无限相对移动接口，指针离开客户区后的交互需求需要单独扩展。

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

高层 render 会覆盖本帧输出。SDK 当前没有对多个 RenderView 保留输出再分屏合成的接口；连续调用 render 不会自动构成分屏。需要分屏或编辑器视口纹理时，使用自己的 RHI 目标与合成流程，或新增高层输出接口。

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

这条路径用于应用自建的 RHI Pipeline。高层 MaterialDesc 当前不接受任意 ShaderArtifact；要改变内置 PBR Shader，需要修改高层后端及对应布局。

## 11. 直接使用 RHI

### 11.1 设备与资源

```cpp
Window window({.width = 128, .height = 128, .visible = false});
auto device = rhi::Device::createOpenGL(window);
const auto caps = device.capabilities();
const auto supported = device.formatFeatures(rhi::Format::Rgba16Float);
```

查询能力后再选择附件格式、绑定数量和内存范围。当前资源范围主要是 2D 纹理、float 顶点属性、uint32 索引、UBO/SSBO、组合采样纹理和 Storage Image；不要假定纹理数组、MSAA、各种顶点整型布局或描述符数组已经可用。

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
| drawCalls | 包含场景、阴影、OIT Resolve、ToneMap 和可选 FXAA，不能直接等于对象数 |
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

### 13.3 错误处理

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

## 扩展到图片世界

在 Sprite/动画/拾取基础之上，使用 `SpriteBatch` 承载地图和粒子；按需链接 tilemap、collision、particles、environment、presentation。完整接口、更新次序、参数、生命周期及功能边界见[图片世界模块](WORLD_MODULES.md)。可运行的 `examples/world` 同时支持两种后端、源码构建和独立 SDK 构建，详见[示例说明](../examples/world/README.md)。
