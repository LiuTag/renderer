# 接口与使用约定

[返回文档导航](README.md) · [最小完整程序](GETTING_STARTED.md) · [二次开发手册与配套工程](DEVELOPMENT_GUIDE.md)

本文代码片段除特别说明外使用 `using namespace renderer;`，并假定已创建 `Scene scene`、`AssetStore assets` 和 `RenderExtractor extractor`。

高层接口同时支持 OpenGL/Vulkan。默认 `Window` 配合 `Renderer::createOpenGL()`；Vulkan 使用 `WindowConfig::graphicsApi = GraphicsApi::Vulkan` 和 `Renderer::createVulkan()`，低层对应 `rhi::Device::createVulkan()`。后续场景、快照、视图和命令接口相同，详见 [Vulkan 接入与同步](VULKAN.md)。

音乐和音效使用独立的 `<renderer/audio/audio.hpp>`，显式链接 `renderer::audio`。资源、播放实例、音乐流、混音与线程约定见[音频接口说明](AUDIO.md)；无需通过 Scene 或 Renderer 创建音频对象。

## 场景树与组件

`Scene` 是场景修改入口。创建节点时必须指定同一场景内的父节点；`scene.root()` 始终存在。节点保存局部仿射矩阵，可同时拥有网格、相机、灯光组件。

```cpp
auto assembly = scene.createNode(scene.root(), "Assembly");
auto part = scene.createNode(assembly, "Part");
auto mesh = assets.createMesh(geometry::cube());
auto material = assets.createMaterial({.baseColor = {0.2f, 0.6f, 0.9f, 1}});
scene.setComponent(part, MeshRenderer{mesh, {material}});
scene.setLocalTransform(assembly, Mat4::translation({0, 0, -3}));
scene.setLocalTransform(part, Mat4::translation({1, 0, 0}) * Mat4::scaling({2, 1, 1}));
// 世界矩阵 = 父世界矩阵 * 局部矩阵；矩阵相乘时从右向左应用。
auto world = scene.worldTransform(part);
```

| 操作 | 契约 |
| --- | --- |
| `createNode(parent, name)` | 创建单位变换、可见的节点，返回安全句柄 |
| `setLocalTransform(node, transform)` | 接受有限仿射矩阵，支持非均匀缩放和剪切 |
| `worldTransform(node)` | 按需更新并返回世界矩阵 |
| `setComponent<T>` | 添加或替换 MeshRenderer、SpriteRenderer、SortingGroup、Camera、Light |
| `component<T>` | 返回 `optional<T>` 副本，缺少组件时为空 |
| `removeComponent<T>` | 移除指定组件 |
| `setVisible(node, false)` | 隐藏节点及其后代，包括其中的相机和灯光 |
| `reparent(node, parent, mode)` | 换父节点，禁止成环和移动根节点 |
| `destroySubtree(node)` | 迭代删除整棵子树，禁止删除根节点 |
| `valid(node)` | 验证场景归属、索引、代际和槽位状态 |

组件读取返回副本，修改后要写回：

```cpp
if (auto component = scene.component<MeshRenderer>(part)) {
    component->castShadow = false;
    component->layerMask = 0x02;
    scene.setComponent(part, *component);
}
scene.reparent(part, scene.root(), ReparentMode::KeepWorld);
```

`KeepLocal` 保留局部矩阵，世界姿态随新父节点改变。`KeepWorld` 使用 `inverse(newParentWorld) * oldWorld` 计算新局部矩阵；新父矩阵不可逆时抛出异常。直接存储矩阵可保留剪切。

句柄的 bool 转换只判断是否非空。已删除节点的句柄可能仍能转为 true，应以 `valid()` 或接口校验为准；不要持久化进程内句柄作为文件资产 ID。

## 相机、灯光与可见性

相机节点局部 -Z 为观察方向。`Mat4::lookAt()` 返回世界到相机的矩阵，用它放置相机节点时需要取逆。

```cpp
auto cameraNode = scene.createNode(scene.root(), "Camera");
Camera camera;
camera.projection = Camera::Projection::Orthographic;
camera.orthographicHeight = 12;
camera.aspectRatio = 0; // 根据当前视口计算宽高比。
camera.cullingMask = 0x02;
scene.setComponent(cameraNode, camera);
scene.setLocalTransform(cameraNode, *Mat4::lookAt({8, 6, 8}, {0, 0, 0}).inverse());

auto lightNode = scene.createNode(scene.root(), "Sun");
Light sun;
sun.type = LightType::Directional;
sun.intensity = 3;
scene.setComponent(lightNode, sun);
scene.setLocalTransform(lightNode, Mat4::rotationX(radians(-45)));
```

每个视图选择一个快照中的可见相机。相机 `cullingMask` 与对象 `layerMask` 按位相与非零才参与视图。方向光和聚光灯同样沿节点局部 -Z 照射，点光源使用节点世界位置。

当前每视图最多 64 个显式灯光；首个启用 `castShadow` 的方向光提供阴影。点光源、聚光灯参与光照但不生成阴影贴图。没有显式灯光时使用 `RenderView.lightDirection` 作为默认方向光的光线传播方向。

`MeshRenderer.castShadow` 控制投影，`receiveShadow` 控制接收阴影。视锥外但层掩码匹配的投影者仍参与阴影，Alpha Blend 物体不参与投影；Alpha Mask 的裁剪规则在阴影阶段保持一致。

## 网格、材质与颜色空间

`MeshDesc` 使用三角形拓扑、uint32 索引，以及位置/法线/UV 顶点。没有显式 submesh 时创建一个覆盖全部索引的默认 submesh。`Submesh.materialSlot` 是 `MeshRenderer.materials` 的下标，提取快照时验证材质槽是否齐全。

资源创建后不可变。更换材质参数时创建新材质，再更新节点组件中的句柄；已有快照继续使用旧材质。

```cpp
MaterialDesc description;
description.baseColor = {0.8f, 0.9f, 1.0f, 0.4f};
description.metallic = 0.1f;
description.roughness = 0.25f;
description.alphaMode = AlphaMode::Blend;
description.doubleSided = true;
auto glass = assets.createMaterial(description);
```

| 输入 | 约定 |
| --- | --- |
| `baseColor`、`emissive` | 线性 RGB，Alpha 为线性覆盖/混合系数 |
| 基色、自发光纹理 | 通常使用 `srgb=true`，采样时解码 |
| 法线、金属度/粗糙度、AO 纹理 | 使用 `srgb=false`，避免改变数据数值 |
| 金属度/粗糙度纹理 | B 为金属度、G 为粗糙度，与材质系数相乘 |
| AO 纹理 | 使用 R 通道，由 `occlusionStrength` 调节 |
| `Opaque` | 不透明深度写入 |
| `Mask` | 按 `alphaCutoff` 丢弃片元，保留深度写入 |
| `Blend` | 根据视图选择排序 Alpha 或加权 OIT，线性空间合成，关闭深度写入 |

默认 SortedAlpha 的透明排序是物体/submesh 级近似，交叉或相互穿透的透明表面不保证逐像素正确。可设置 `view.transparency = TransparencyMode::WeightedBlendedOIT` 使用加权无序透明，此时按材质/网格合批，并通过颜色累积和透过率合成到 HDR。该模式是近似合成，开启方法、阶段及精度/显存取舍见 [OIT 说明](OIT.md)。

手工 `TextureDesc` 使用左下原点 RGBA8；`loadTexture()` 会翻转解码图片以符合该约定。glTF 保留像素与 UV 的配套约定，不应额外手动翻转。最终 `Renderer::readback()` 返回左上原点、显示编码的 RGBA8，与手工上传行序不同。

可设置 `view.antiAliasing = AntiAliasing::FXAA` 开启 ToneMap 后的画面抗锯齿，默认 None；可以逐帧切换，与排序透明、OIT 和独立渲染线程兼容。不会改变网格或 RHI 采样数。颜色空间、代价和示例见[FXAA 说明](ANTI_ALIASING.md)。

## 资源与快照生命周期

图片角色和场景物体可使用 `SpriteRenderer`、`SpriteAtlas`、`SpriteAnimator` 和 `SceneQuery`，完整接口约定和用法见 [Sprite / Billboard](SPRITES.md)。精灵默认使用独立于 OIT 的排序策略；在 OIT 视图中会在合成后增加可选 `Ordered Sprites` Pass。`RenderSnapshot::instanceCount()` 为网格与精灵总数，`spriteCount()` 单独统计精灵。

SpriteAnimator 和 SceneQuery 分别需要独立链接 animation、interaction 组件；渲染核心通过 `spriteView()` 提供只读 SpriteViewSnapshot，不承担动画与交互。目录与迁移见[模块说明](MODULES.md)。

```text
AssetStore 中的句柄 -> 不可变 CPU 资源
Scene 中的组件     -> 仓库句柄
extract()          -> 解析并保留资源引用，复制世界变换/相机/灯光
RenderSnapshot     -> 独立于原 Scene / AssetStore 的不可变数据
Renderer           -> 按资源身份缓存 GPU 对象，完成后延迟回收
```

`destroyMesh/Texture/Material` 立即使仓库句柄失效。已提取快照仍然有效；如果后续提取的场景仍引用已销毁的网格或材质句柄，则抛出异常。材质创建时已经保留纹理数据引用，销毁原纹理句柄不影响该材质。

`Model` 保存模板节点和资源句柄，可重复实例化，但本身不替代资源仓库；创建和后续提取实例时应保留对应资源句柄有效。删除实例使用 `scene.destroySubtree(instance.root)`，不会自动删除共享资产。

```cpp
#include <renderer/model.hpp>
auto model = assets.loadModel("assets/scene.glb");
auto first = model.instantiate(scene, scene.root(), "First");
auto second = model.instantiate(scene, scene.root(), "Second");
scene.setLocalTransform(second.root, Mat4::translation({3, 0, 0}));
// first/second 节点独立，网格和材质共享；相机列表位于 instance.cameras。
```

支持静态 glTF/GLB、OBJ/MTL 和图片导入。动画、蒙皮、形变、压缩几何等不在当前范围，不支持的必需扩展会报错；导入失败会清理本次已创建资源。详细边界见[架构文档](ARCHITECTURE.md)。

## 帧提交、线程与等待

通过 `Renderer::createVulkanOffscreen()` 或 Windows 的 `createOpenGLOffscreen()` 可以不创建应用 Window，渲染到独立纹理并直接保存 PNG。场景与视图接口一致，离屏不需要 present；平台差异、尺寸和同步约定见[离屏说明](OFFSCREEN.md)。

键鼠及窗口状态通过 `Window::subscribe<T>()` 或统一 WindowEvent 回调订阅，返回的 EventSubscription 采用 PImpl 并在析构时取消。用户回调在主线程 pollEvents 中有序执行，不进入渲染线程。具体事件、重入/异常和生命周期约定见[窗口事件说明](WINDOW_EVENTS.md)。

窗口支持 `WindowConfig.mode`、`Window::setMode()` 和 `Window::mode()`，可选普通窗口、无边框和独占全屏。切换在主线程执行，保留上下文；退出时恢复先前位置、尺寸和最大化状态。显示器、独占视频模式及 resize 约束见[全屏说明](FULLSCREEN.md)。

Scene、AssetStore、RenderExtractor 按单写线程使用，提取期间不要并发修改其输入。快照不可变，可复制后跨线程读取。应用在窗口创建线程调用 Renderer 接口及窗口事件接口，内部渲染线程负责上下文操作。

```cpp
RenderView view;
view.cameraNode = cameraNode;
auto future = engine.submit(extractor.extract(scene, assets), view);
// 可继续修改 Scene；已提交快照不会被这些改动影响。
scene.setLocalTransform(part, Mat4::rotationY(radians(30)));
auto stats = future.get(); // 接收 CPU 渲染任务的结果或异常。
engine.present();
```

| 接口/配置 | 等待语义 |
| --- | --- |
| `renderThread=false` | `submit()` 在调用线程执行，仍以 future 返回结果 |
| `renderThread=true` | 使用专用线程；`maxQueuedFrames` 限制待执行任务数，队列满时提交者等待 |
| `render(snapshot, view)` | 等价于 `submit(...).get()`，等待 CPU 准备及命令提交 |
| `future.get()` | 不代表 GPU 已完成，也不交换窗口缓冲 |
| `present()` | 按队列顺序切换到上下文线程交换缓冲；可能受垂直同步影响 |
| `readback()` | 阻塞读取最近输出，可用 `Image::savePng()` / `savePpm()` 保存 |
| `renderToImage(snapshot, view)` | 一个队列任务内绘制并回读指定快照；同步等待 GPU，无需 present |
| `resizeOffscreen(width, height)` | 仅独立纹理目标可用；下一次绘制重建附件 |
| `waitIdle()` | 等待此前任务及 GPU 工作完成 |
| Renderer 析构 | 排空队列并在上下文线程销毁资源，最后释放保留的窗口引用 |

`framesInFlight` 是 GPU 帧槽数，`maxQueuedFrames` 是 CPU 待执行队列上限，两者分别控制缓冲复用和排队内存。专用线程模式使用 `Renderer::present()`；`Window::present()` 不可在此模式下代替它。

## Shader 与绑定约定

`ShaderCompiler` 接收带 `set/binding` 的 GLSL 450，输出 SPIR-V 1.5 和资源反射；OpenGL Pipeline 自动合并阶段布局并转译为桌面 GLSL。Shader 编译使用 Vulkan 语义，不需要 Vulkan 运行时。

```cpp
#include <renderer/shader.hpp>
ShaderCompiler compiler;
auto shader = compiler.compile(ShaderStage::Compute, R"(
#version 450
layout(local_size_x=1) in;
layout(std430,set=0,binding=0) buffer Output { uint value; } outputData;
void main() { outputData.value = 42u; }
)", "answer.comp");
// shader.bindings 包含组号、绑定号、块大小与成员偏移。
```

宏格式为 `NAME` 或 `NAME=VALUE`，include 搜索路径通过 `ShaderCompileOptions.includeDirectories` 提供。缓存以预处理结果和阶段为键；Pipeline 缓存共享已链接的 OpenGL 程序。

RHI 的 `BindingGroupDesc.group` 对应 `set`，条目 `binding` 对应逻辑绑定号。每个逻辑 `(group,binding)` 的资源种类和块布局必须在阶段间兼容。手工调用 `toOpenGL()` 时，非零组需要显式 `BindingRemap`；通过 Device 创建 Pipeline 时由后端完成映射。

CPU 数据布局应对照反射检查 std140/std430 的偏移、最小大小以及设备对齐，不能只假设 C++ 结构体大小相同。

## RHI 与 RenderGraph

高层 Renderer 自行管理一套 Device。同一窗口不能再创建另一套低层 Device；自定义渲染管线可选择直接使用 `rhi::Device::createOpenGL(window)`。

CommandEncoder 只录制 CPU 命令，可在工作线程分别录制。`finish()` 返回不可变 CommandList 后编码器不可继续使用。命令列表只保存句柄，提交前不能销毁被引用资源；提交后销毁句柄，由设备根据完成序号延迟释放原生对象。

下面是可在现有设备上执行的完整 Buffer 拷贝流程。代码中的外部 Buffer 均导入图，并按值捕获逻辑句柄，避免局部变量引用失效。

```cpp
#include <renderer/render_graph.hpp>
#include <array>

void copyWithGraph(renderer::rhi::Device &device) {
    using namespace renderer;
    const std::array<uint32_t, 4> input{1, 2, 3, 4};
    const auto source = device.createBuffer(
        {.size = sizeof(input)}, std::as_bytes(std::span{input}));
    const auto destination = device.createBuffer(
        {.size = sizeof(input), .memory = rhi::Memory::Readback});
    {
        RenderGraph graph;
        auto src = graph.importBuffer(source, {.size = sizeof(input)}, rhi::Access::TransferRead);
        auto dst = graph.importBuffer(destination,
            {.size = sizeof(input), .memory = rhi::Memory::Readback}, rhi::Access::None);
        graph.addPass("Copy", {
            {src, rhi::Access::TransferRead, rhi::Stage::Transfer},
            {dst, rhi::Access::TransferWrite, rhi::Stage::Transfer},
        }, [src, dst](rhi::CommandEncoder &commands, const GraphResources &resources) {
            commands.copyBuffer(resources.buffer(src), resources.buffer(dst), 4 * sizeof(uint32_t));
        });
        graph.exportResource(dst); // 保留该输出的生产链，防止 Copy 被剔除。
        auto compiled = graph.compile();
        device.wait(compiled.execute(device));
        auto bytes = device.readBuffer(destination);
        // bytes 为 input 的字节副本，可交给调用者处理。
    }
    device.destroy(destination);
    device.destroy(source);
}
```

Pass 按资源生产顺序声明。图编译验证读区间已初始化，并为重叠 RAW/WAR/WAW 访问建立依赖；`dependsOn(pass, dependency)` 补充显式执行边。单个 Pass 的存储读写应声明一条 `StorageReadWrite`，不要重复声明相互重叠的读写访问。

`GraphRange` 中纹理范围以 mip 为单位，Buffer 范围以字节为单位，数量为零表示到资源末尾。`GraphResources` 只能在回调中解析本 Pass 声明过的资源，不能持有它的引用跨越回调。

图不会分析回调发出的任意命令。外部可变资源必须导入并声明访问；外部互相别名的纹理视图由调用者统一建模。编译图可以在同一设备上重复执行，回调捕获对象要保持有效；执行和析构均在设备线程进行。Device 包装对象可移动，编译图持有共享后端状态。

直接使用 RHI 时，资源屏障也由调用者声明。`submit()` 返回 `CompletionToken`，可用 `complete()` 非阻塞查询、`wait()` 等待。持久映射 Upload 缓冲仍需等待此前 GPU 访问结束，coherent 属性并不消除 CPU/GPU 写读竞争。

当前 RHI 范围为 2D 纹理、float 顶点属性、uint32 索引、UBO/SSBO、组合采样纹理和 Storage Image。格式、绑定上限与对齐从 `capabilities()` / `formatFeatures()` 查询；描述符数组等超出范围的请求会明确拒绝。

## 统计与诊断

`RenderStats.drawCalls` 包含场景、阴影、ToneMap 和可选的 OIT Resolve / FXAA；`transparencyResolveDrawCalls`、`antiAliasingDrawCalls` 分别统计合成和抗锯齿绘制，`triangles` 只统计场景三角形。`prepareMilliseconds` 为前端准备耗时，`submitMilliseconds` 包含后端处理及可能的帧槽等待，均不是 GPU 执行时间。

GPU 时间来自已经完成的旧帧。仅在 `gpuTimingValid` 为 true 时使用 `gpuMilliseconds` 和 `passes`；首几帧没有计时是正常情况。排错和复现命令见[验证与排错](VALIDATION.md)。
