# Vulkan 后端

[文档导航](README.md) · [模块与依赖](MODULES.md) · [SDK 安装](SDK.md) · [验证记录](VALIDATION.md)

`renderer::vulkan` 实现 Vulkan 1.3 RHI、窗口表面与交换链，使用现有 Scene、AssetStore、RenderSnapshot、ShaderArtifact、CommandEncoder 和 RenderGraph。OpenGL/Vulkan 共用 `src/backends/common/renderer_backend.cpp` 的高层管线，PBR、阴影、排序 Alpha、Weighted OIT、FXAA、Sprite/Billboard 和实例化使用同一套前端数据与 Shader。

通过 `Renderer::createVulkanOffscreen()` 或 `rhi::Device::createVulkanOffscreen()` 可以完全跳过 GLFW 初始化、窗口、表面和交换链。高层用 `renderToImage(...).savePng(...)` 取得实际渲染结果；无需 `present()`。普通窗口路径和离屏路径共用图形/计算实现，离屏选择设备时不要求呈现队列和 swapchain 扩展，详见[离屏渲染](OFFSCREEN.md)。

## 构建与运行

默认 `RENDERER_BUILD_VULKAN=OFF`，保留原 OpenGL 默认构建，不要求所有使用者安装 Vulkan SDK。启用 Vulkan 的源码构建需要 Vulkan 1.3 或更新的开发头文件，可安装 LunarG Vulkan SDK；Windows 的 FindVulkan 会使用 `VULKAN_SDK`。正常运行及已安装 SDK 的二次开发不需要这些头文件，只需要支持相应功能的 Vulkan 驱动与系统 loader。

在现有 Windows 构建中同时启用两个后端：

```powershell
cmake --preset windows -DRENDERER_BUILD_OPENGL=ON -DRENDERER_BUILD_VULKAN=ON
cmake --build --preset windows-release --parallel 6
.\build\windows\Release\renderer_vulkan_demo.exe --threaded --oit --fxaa
.\build\windows\Release\renderer_vulkan_sprite_demo.exe --threaded --oit
```

两个 Vulkan 示例分别复用原三维示例和森林示例的源码，只在创建窗口和渲染器时选择后端。森林示例需要 animation 和 interaction。普通三维示例可用 `--hidden --frames 12 --capture frame.ppm` 自动运行并回读；也支持原有全屏与抗锯齿参数。

只构建 Vulkan 并运行测试，也提供专用预设：

```powershell
cmake --preset windows-vulkan
cmake --build --preset windows-vulkan --parallel 6
ctest --preset windows-vulkan
```

该预设关闭 OpenGL、开启 Vulkan、示例与 Vulkan 检查。`RENDERER_VULKAN_TESTS=ON` 需要 Vulkan 后端已开启，且受 `RENDERER_BUILD_TESTS` 总开关控制；关闭测试时不会编译或注册 GPU 测试。Vulkan 示例检查也受 Vulkan 测试开关控制。

全部后端、模块、示例和测试都开启时，当前有 30 项 CTest，其中九项带 vulkan 标签，包括跨后端对比、两个演示示例及精灵性能测试。可在原 windows 构建中开启并执行：

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build --preset windows --parallel 6
ctest --test-dir build/windows -C Debug --output-on-failure
```

## SDK 接入

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS vulkan)
target_link_libraries(my_app PRIVATE renderer::vulkan)
```

公共头文件没有 Vulkan/GLFW 类型。后端使用仓库内固定版本 Volk 动态加载 Vulkan，Volk 实现对象已编入 `renderer_vulkan`；无需为应用单独链接 Vulkan import library。SDK 携带平台、共用管线、GLFW 和 core 所需的静态依赖，CMake 自动传递，使用者保留完整安装目录即可。

两个后端共用的内部平台库和管线库导出为内部目标，不是应用需要调用的额外组件。只安装 Vulkan 的包可在关闭 OpenGL、GLAD 的情况下工作。源代码中的 `examples/` 聚合入口也可以直接从已安装 SDK 独立构建 Vulkan 示例：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples -B build/sdk-examples -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_VULKAN_TESTS=ON
cmake --build build/sdk-examples --config Release --parallel 6
ctest --test-dir build/sdk-examples -C Release -R vulkan --output-on-failure
```

## 选择窗口与渲染器

窗口创建时必须指定图形 API，创建后不能原地切换：

```cpp
#include <renderer/window.hpp>
#include <renderer/renderer.hpp>
using namespace renderer;

Window window({.width = 1280,
               .height = 800,
               .title = "Vulkan application",
               .graphicsApi = GraphicsApi::Vulkan});
auto renderer = Renderer::createVulkan(window, {.renderThread = true});
```

之后的节点、资源、相机、快照提取和视图设置完全相同：

```cpp
auto snapshot = extractor.extract(scene, assets); // 对象由应用按快速开始创建。
renderer.render(snapshot, view);
renderer.present();
auto image = renderer.readback();
```

`Device::createVulkan(window)` 则提供低层 RHI，计算、图形命令和渲染图用法不变。低层应用先录制 `encoder.present(texture)`，提交后调用 `window.present()`；窗口内部通过弱引用转交给当前 Vulkan 设备。高层或专用线程模式使用 `Renderer::present()`，由内部队列路由。未绑定 Vulkan 设备的窗口不能呈现。

OpenGL 工厂与 Vulkan 窗口混用、或反过来混用会抛出异常。`DeviceCaps::backend`、`Renderer::deviceInfo()` 可检查实际后端。窗口事件、全屏和尺寸接口继续在窗口创建线程使用。

## 实现与同步

- 设备要求 Vulkan 1.3 的 dynamicRendering、synchronization2、独立附件混合，以及支持图形/计算的队列和 GPU 时间戳；呈现队列可以不同，交换链使用对应共享模式。
- 图形/计算管线直接使用统一 ShaderArtifact 的 SPIR-V。逻辑 set/binding 成为描述符组；各阶段声明合并为 PipelineLayout，自动检查类型和缓冲区大小。
- 每次提交拥有命令缓冲、fence、描述符分配池及引用的原生资源。句柄销毁立即失效，GPU 使用的对象由提交记录保留到 fence 完成；纹理视图保留底层存储。
- 上传内存使用持久映射的 host-coherent 分配，专用 GPU 缓冲和纹理通过 staging 上传；同步回读等待对应提交完成。初次资源准备可能等待传输完成，稳态帧提交保持异步。
- RHI 纹理目前统一使用 GENERAL 布局，显式范围屏障映射到 synchronization2；渲染作用域边界还建立保守的可见性依赖。此实现优先保证纹理视图、资源复用和绑定缓存的一致性，尚未进行按用途最优布局、内存子分配及传输队列并行优化。
- 描述符在提交期间缓存，按实际 PipelineLayout 分配和写入；缓存记录不跨提交悬挂引用。
- 时间戳转换为纳秒，与 OpenGL 接口一致；统计读取已经完成的旧帧，回读/等待依然是同步操作。

控制方法仍集中在设备线程；Vulkan 不使用 OpenGL 上下文，但保持相同线程归属检查。高层渲染队列的启动握手将设备操作权交给专用线程，析构排空任务并释放 GPU 资源，窗口最后引用保留在应用线程。

## 坐标、颜色与呈现

RHI 继续采用右手坐标、NDC 深度 `[0,1]`，上传图片为左下行序，回读图片为左上行序。Vulkan 保留 RHI 图片的行序，通过光栅绕序设置和最终呈现翻转保持前端一致；Shader 的纹理坐标和 Billboard 逻辑无需修改。

交换链使用 RGBA8/BGRA8 sRGB 格式。高层最终 sRGB 图片通过一次 blit 呈现，避免重复颜色编码。低层 Rgba8 或浮点颜色目标先按原有呈现字节语义转换，再呈现；深度纹理禁止直接呈现。

创建交换链选择可用的图像数和表面范围，vsync 开启使用 FIFO；关闭时优先 MAILBOX，再选 IMMEDIATE，否则回退 FIFO。缩放或 OUT_OF_DATE/SUBOPTIMAL 会安全重建。最小化造成零尺寸时跳过呈现。取得图像的 semaphore 随提交完成回收，呈现完成 semaphore 按交换链图像持有，避免它仍被呈现引擎使用时复用。

## 调试与当前边界

`debug=true` 时，如果安装了 `VK_LAYER_KHRONOS_validation`，就启用校验；存在对应扩展时同时开启同步校验。错误写入 stderr，并累加到 `debugErrorCount()`。驱动环境没有校验层时仍可运行，校验层不是发布 SDK 的必需文件。

当前已在 Windows / NVIDIA GeForce RTX 2080 Ti / Vulkan SDK 1.4.350.0 实测。自动检查覆盖低层计算、间接命令、资源/视图与图复用、透明/OIT、阴影/PBR、FXAA、Sprite、回读、线程、呈现/缩放/全屏，以及同一快照的 OpenGL/Vulkan 像素对比。其他 GPU、Linux 和 macOS 尚未实机验证；不代表跨设备的像素完全相等或性能结论。

Vulkan 边框采样目前支持标准透明黑、实心黑和实心白，任意自定义边框色在使用 Border 寻址时会明确拒绝。后端支持二维/二维数组、BC 压缩、层视图与存储图像，附件保持单采样；数组附件需建立单层二维视图；MSAA、多窗口同设备共享资源、HDR 显示器输出和多 GPU 不在本次实现范围。当前呈现表面需支持 transfer destination 和 RGBA8/BGRA8 sRGB 格式。

底层设计参考 [GLFW Vulkan 接入](https://www.glfw.org/docs/latest/vulkan_guide.html)与 [Khronos 同步说明](https://docs.vulkan.org/guide/latest/synchronization.html)，依赖来源见[第三方说明](../THIRD_PARTY.md)。
