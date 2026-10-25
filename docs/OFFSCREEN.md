# 离屏渲染与本地图片

[返回文档导航](README.md) · [接口约定](API_GUIDE.md) · [SDK 接入](SDK.md)

离屏入口不要求应用创建 `Window`。场景、资源、快照及 `RenderView` 与交互程序相同，最终画面保存在引擎纹理中，可以同步取得 RGBA8 图像并直接保存 PNG，用于自动化截图、缩略图、图片生成及渲染回归。

此前测试图片来自 `Renderer::readback()`：先创建隐藏 GLFW 窗口，执行真实 GPU 渲染，再回读最终输出纹理，保存 PPM 后转换为 PNG。它不是桌面或窗口截图，但仍依赖一个 GLFW 窗口。新入口进一步移除了这项应用窗口依赖；PNG 编码也已集成进核心库。

## 后端与平台

| 路径 | 创建应用窗口 | 原生窗口系统依赖 | 当前实现 |
| --- | --- | --- | --- |
| `createVulkanOffscreen()` | 不创建 | 不初始化 GLFW，不创建 surface/swapchain，不要求呈现队列或 `VK_KHR_swapchain` | 普通 Vulkan 图形/计算设备与纹理，适用于严格无窗口需求 |
| `createOpenGLOffscreen()` | 不创建 | Windows WGL 初始化短暂使用一个隐藏引导 HWND，随构造完成销毁；随后使用 pbuffer/DC/GLRC | Windows 已实测；其他系统暂未实现，调用会明确抛出异常 |
| 原有 `createOpenGL(window)` / `createVulkan(window)` | 应用负责创建 | 依赖 GLFW 窗口；`visible=false` 仍有原生窗口 | 适合交互、输入和屏幕呈现 |

Windows OpenGL 的 pbuffer 不作为画面输出，实际输出始终在 FBO 纹理中，无需运行窗口事件循环。不过驱动可能用隐藏原生对象实现 pbuffer：本机 NVIDIA 驱动会创建内部 `NVOpenGLPbuffer` HWND。因此 OpenGL 路径不能承诺操作系统层面完全没有窗口，也不承诺脱离图形桌面会话运行。严格零窗口或服务器渲染请使用 Vulkan 离屏入口。

离屏仍需要支持相应功能的 GPU 驱动及足够显存。Vulkan 无需显示服务和桌面，但仍需要可用的 Vulkan 1.3 loader/设备。当前实机验证平台是 Windows / RTX 2080 Ti；Linux 和其他显卡尚未实机验证。

## 最小完整用法

构建时开启 Vulkan 后端，应用链接 `renderer::vulkan`；公共头文件不依赖 Vulkan SDK。

```cpp
#include <renderer/renderer.hpp>
#include <filesystem>

int main() {
    using namespace renderer;
    auto engine = Renderer::createVulkanOffscreen(
        {.width = 1280, .height = 720}, {.renderThread = true});
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;

    const auto camera = scene.createNode(scene.root(), "Camera");
    scene.setComponent(camera, Camera{});
    scene.setLocalTransform(camera, Mat4::translation({0, 0, 4}));
    const auto cube = scene.createNode(scene.root(), "Cube");
    const auto mesh = assets.createMesh(geometry::cube());
    const auto material = assets.createMaterial(
        {.baseColor = {.2f, .6f, .9f, 1}, .unlit = true});
    scene.setComponent(cube, MeshRenderer{mesh, {material}});

    RenderView view{.cameraNode = camera, .shadows = false,
                    .antiAliasing = AntiAliasing::FXAA};
    const auto snapshot = extractor.extract(scene, assets);
    auto image = engine.renderToImage(snapshot, view);
    image.savePng(std::filesystem::path(L"output/中文目录/方块.png"));
}
```

使用 OpenGL 时只需替换为 `Renderer::createOpenGLOffscreen(...)` 并链接 `renderer::opengl`。两种工厂均采用已有 PImpl `Renderer`，没有向外暴露 HWND、HGLRC、VkImage 或第三方类型。

## 接口契约

| 接口 | 语义 |
| --- | --- |
| `create*Offscreen(target, config)` | 选择纹理宽高与帧槽、调试、渲染线程；无需先创建 Window 或初始化 GLFW |
| `renderToImage(snapshot, view)` | 在一个渲染队列任务中绘制并回读，返回本次快照结果；同步等待 GPU，不需要 `present()` |
| `render()` / `submit()` | 沿用原有 CPU 提交与有界队列语义；只渲染到纹理，通常比逐帧回读更适合测量吞吐 |
| `readback()` | 回读最近一次已经提交的最终纹理；首次渲染前抛出异常；不重新绘制场景 |
| `resizeOffscreen(w, h)` | 按队列顺序更新尺寸并等待更新完成；下一次绘制重建图附件，非法尺寸不改变原尺寸 |
| `present()` | 离屏没有窗口/交换链，明确抛出 `std::logic_error` |
| `Image::savePng(path)` | 保存 RGBA PNG，自动创建父目录，覆盖同名文件；保留输入颜色、Alpha 和左上行序 |
| `Image::savePpm(path)` | 保留旧 PPM 接口，忽略 Alpha，供已有流程使用 |

离屏尺寸每边必须为 `[1,16384]`，还需小于等于 `DeviceCaps.maxTextureSize`。它是明确的像素尺寸，不受窗口最小尺寸、DPI 或显示器分辨率影响。相机 `aspectRatio=0` 时仍自动采用目标宽高比。

缩放前已经排队的帧保持各自提交时的尺寸。缩放后、下一帧绘制前调用 `readback()`，得到的仍是上一次输出；不会用新尺寸解释旧像素。窗口渲染器不能调用 `resizeOffscreen()`，应通过窗口尺寸接口调整。`renderToImage()` 也可以用于已有窗口渲染器，窗口最小化造成零尺寸时会抛出异常。

对同一个 Renderer 的操作仍建议由一个应用控制线程发起。`renderThread=true` 将上下文/设备操作交给专用线程；OpenGL pbuffer 也支持这个迁移，资源在拥有上下文的线程析构。不要在两条应用线程中并发修改 Scene/AssetStore 或同时控制同一个 Renderer。

## 颜色、透明和性能

- 图像是左上原点、显示编码的 RGBA8；图片查看器可直接打开 PNG，不要再次做 gamma 编码或上下翻转。
- 手工 `TextureDesc` 的像素为左下原点，`AssetStore::loadTexture()` 会按既有纹理约定转换；不要把输出图像直接当作纹理上传而忽略行序。
- PNG 写入器保留传入的 Alpha。不过高层 ToneMap 当前输出 Alpha 固定为 1，渲染含透明物体的场景会得到已与背景合成的画面；这不等于支持透明背景导出。
- PNG 验证像素数量、尺寸和整数范围；编码包含过滤字节的输入不能超过约 1 GiB。编码/文件创建/写入失败抛出异常；写入失败可能留下不完整文件，不保证原子替换。
- `savePng()` 只访问 CPU Image，可在应用自己的文件工作线程中执行。使用 `filesystem::path` / `u8string` 传入 Unicode 路径，Windows 示例命令的 `--output` 按 UTF-8 参数解释。
- `renderToImage()` 和 `readback()` 都包含 GPU → CPU 同步与数据传输，PNG 编码还增加 CPU 时间和内存占用。不要将保存图片耗时当作纯渲染耗时；当前没有异步 GPU 回读环或视频编码接口。

## 一帧在内部如何执行

```text
Scene / SpriteBatch → RenderExtractor → RenderSnapshot
  → prepareFrame：相机 / Billboard / 裁剪 / 排序 / 合批
  → 共用 RenderGraph：阴影 → 不透明与透明 HDR → 可选 OIT / 排序精灵
  → ToneMap / 调色 → 可选 FXAA → 导出的最终颜色纹理
  → RHI::readTexture → 左上原点 RGBA8 Image → PNG 文件
```

离屏图没有 `Present` Pass，不会把输出复制到默认帧缓冲或交换链。PBR、Alpha Mask/排序透明、OIT、Sprite 排序、接触阴影、调色和 FXAA 复用原有管线设置。阴影与后处理资源仍由渲染图管理，导出最终纹理保证它及其依赖不会被当成无用 Pass 剔除。

## 森林示例与构建

源码位于 [examples/offscreen](../examples/offscreen/README.md)，仅依赖 core 与可用后端，不需要动画、物理或地图模块。使用共用森林图集生成角色、树石、花草和地面阴影，程序没有 `Window`、事件订阅或事件轮询。

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON -DRENDERER_BUILD_EXAMPLES=ON
cmake --build build/windows --config Release --target renderer_offscreen_demo --parallel 6
.\build\windows\Release\renderer_offscreen_demo.exe --backend vulkan --threaded --output build/windows/forest-vulkan.png
.\build\windows\Release\renderer_offscreen_demo.exe --backend opengl --width 1920 --height 1080 --output build/windows/forest-opengl.png
```

参数：`--backend opengl|vulkan`、`--output file.png`、`--width`、`--height`、`--threaded`、`--oit`、`--no-fxaa`、`--self-test`。同时存在两种后端时默认选择 Vulkan；只有 OpenGL 时选择 OpenGL。示例不显示窗口，运行完成后退出。

安装 SDK 后可独立配置；保留相邻的 `examples/common` 图集源码：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/offscreen -B build/windows/offscreen-sdk `
  -DCMAKE_PREFIX_PATH=C:/SDK/renderer `
  -DRENDERER_VULKAN_TESTS=ON -DRENDERER_GL_TESTS=ON
cmake --build build/windows/offscreen-sdk --config Release --parallel 6
ctest --test-dir build/windows/offscreen-sdk -C Release --output-on-failure
```

`RENDERER_BUILD_EXAMPLES` 控制示例编译；`RENDERER_BUILD_TESTS` 控制测试注册，GPU 检查分别由既有 OpenGL/Vulkan 测试开关控制。VS 中示例属于 `examples`，测试属于 `tests`。PNG 编码实现静态编入 core，SDK 使用者不需要额外安装 libpng、zlib 或 stb 开发文件。

## 自定义纹理管线

自定义 Shader 或渲染图可以直接创建低层离屏设备：

```cpp
#include <renderer/rhi.hpp>
#include <renderer/renderer.hpp>
using namespace renderer;
auto device = rhi::Device::createVulkanOffscreen();
auto color = device.createTexture({320, 200, 1, rhi::Format::Rgba8, "Output"});
rhi::CommandEncoder commands;
rhi::RenderingDesc pass;
pass.colors.push_back({color, rhi::LoadOp::Clear, rhi::StoreOp::Store, {.2f, .4f, .6f, 1}});
commands.beginRendering(pass);
// 在这里绑定 Pipeline、资源和顶点，录制绘制命令。
commands.endRendering();
auto done = device.submit(commands.finish());
device.wait(done);
auto pixels = device.readTexture(color);
Image{pixels.width, pixels.height, std::move(pixels.pixels)}.savePng("output/custom.png");
device.destroy(color);
device.waitIdle();
```

低层 `Rgba8` 回读得到原始 UNORM 字节，不自动应用高层 ToneMap/gamma；需要显示编码时由自定义管线负责。也可创建 `rhi::Device::createOpenGLOffscreen()`，使用相同命令或 RenderGraph。离屏设备不能提交 `encoder.present(texture)`。高层 Renderer 的最终 GPU 纹理仍由内部持有；需要自行采样或组合 GPU 纹理时选择 RHI/RenderGraph。

## 验证范围

`renderer.image` 在不创建设备时验证 PNG 签名、RGBA/半透明像素、上下行序、Unicode 路径、重复保存和错误输入/IO。

`renderer.opengl.offscreen` 和 `renderer.vulkan.offscreen` 分别在独立进程运行，程序不创建 Window，也不初始化 GLFW。覆盖低层纹理渲染与回读、奇数尺寸、显示编码、单线程/渲染线程像素一致性、队列背压、缩放、非法操作恢复、OIT/FXAA、GPU 计时、PNG 输出及零调试错误。Windows 上还枚举窗口：Vulkan 检查没有新增进程顶层 HWND；OpenGL 检查没有可见窗口或残留的 GLFW/引擎引导窗口，允许驱动的隐藏对象。

`renderer.offscreen.opengl.example` / `renderer.offscreen.vulkan.example` 验证森林程序直接输出 PNG。输出位于 CTest 构建目录，方便查看和与以后版本比较。
