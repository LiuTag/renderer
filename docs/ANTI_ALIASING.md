# 可选抗锯齿：FXAA

[返回文档导航](README.md) · [接口说明](API_GUIDE.md) · [透明 OIT](OIT.md)

当前提供单帧后处理抗锯齿 FXAA，按 RenderView 开启，默认关闭。不需要修改窗口配置或重建 Renderer，可以在帧之间切换，并与排序 Alpha、加权 OIT、HDR 曝光和 ToneMap 组合使用。

## 开启与关闭

```cpp
renderer::RenderView view;
view.cameraNode = cameraNode;
view.antiAliasing = renderer::AntiAliasing::FXAA;
engine.render(extractor.extract(scene, assets), view);
engine.present();

// 后续帧恢复原有单采样输出路径。
view.antiAliasing = renderer::AntiAliasing::None;
```

| 值 | 行为 |
| --- | --- |
| AntiAliasing::None | 不添加抗锯齿 Pass，不分配 FXAA 输入目标 |
| AntiAliasing::FXAA | ToneMap 后增加一次 FXAA 全屏绘制 |

非法枚举值在帧准备时抛出异常。接口不改变场景几何采样数，当前 RHI 仍没有 MSAA 配置，也没有 SMAA/TAA 实现。

## 渲染流程与颜色空间

```text
Shadow → Opaque PBR → Transparent / OIT → ToneMap → FXAA → Present
```

关闭抗锯齿时保持原有 ToneMap 直接写最终显示目标的路径。开启后，ToneMap 将曝光和色调映射后的颜色手工编码为 sRGB 数值，写入普通 RGBA8 临时纹理；FXAA 对这些显示编码数值进行边缘检测与双线性过滤。

FXAA 输入使用普通 UNORM，而不是自动解码的 sRGB 纹理，确保插值发生在感知颜色空间。过滤后先解码回线性值，再写入最终 Rgba8Srgb 附件，由附件完成最终编码；Present 不再二次编码。这个顺序遵循 [NVIDIA FXAA 白皮书的集成说明](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf)。

输入和输出是两张不同纹理，避免读写反馈。每次帧均覆盖整个目标，没有历史帧缓存。分辨率由输入纹理 textureSize 取得，窗口缩放、全屏切换及抗锯齿开关变化时自动重建对应渲染图和绑定。

## 本实现的过滤方式

Shader 在局部亮度对比不足时直接返回中心颜色；其余区域通过 3×3 邻域判断边缘方向，沿边缘两侧进行最多各 12 像素的端点搜索，并结合亚像素偏移重采样。亮度估计包含 RGB 三个通道。

固定相对对比阈值为 1/8、暗部绝对阈值为 1/32，亚像素偏移权重为 0.75。这是基于 FXAA 思路编写的固定参数实现，不对应 NVIDIA 某个版本的完整预设；当前未对外暴露质量档位或阈值调节。

几何轮廓、Alpha Mask、透明表面以及高对比材质图案都可参与最终画面过滤。平坦区域保持原颜色，测试允许额外 RGBA8 编码造成的至多 1 个字节量化差异。

FXAA 可能让细线、细小纹理和文字变柔，无法恢复单采样阶段已经丢失的几何细节，也不解决所有时间闪烁。高层的 Screen Overlay/UI 已在 FXAA 后合成，避免界面再次被场景过滤。任意自定义 Pass 组合仍通过 RHI/RenderGraph 实现，高层不提供通用 Pass 注入接口。

## 成本和统计

开启时增加一个全分辨率 RGBA8 临时目标，即每像素 4 字节：1920×1080 约 7.91 MiB，不含驱动分配开销；同时增加一次全屏绘制和自适应邻域采样。实际 GPU 耗时取决于分辨率、图像对比和设备，需读取时间戳测量。

| 透明模式 | 关闭 FXAA | 开启 FXAA |
| --- | --- | --- |
| SortedAlpha | 5 个 Pass | 6 个 Pass |
| WeightedBlendedOIT | 6 个 Pass | 7 个 Pass |

上表为普通网格路径；OIT 中存在默认 Sorted Blend 精灵时额外增加 `Ordered Sprites` Pass，仍在 ToneMap/FXAA 前绘制，见 [Sprite 说明](SPRITES.md)。

`RenderStats.antiAliasingDrawCalls` 在开启时为 1，关闭时为 0；总 drawCalls 包含这次绘制。sceneDrawCalls、transparentDrawCalls 和 triangles 的几何统计不因 FXAA 改变。GPU Pass 列表包含名称为 FXAA 的计时。

GPU 时间来自已完成的旧帧，开关刚切换时可能仍显示前一模式的 Pass 列表；只有 gpuTimingValid 为 true 时使用 GPU 统计。CPU 的 antiAliasingDrawCalls 始终对应本次提交。

## 示例

```powershell
.\build\windows\Release\renderer_demo.exe --fxaa --threaded
.\build\windows\Release\renderer_demo.exe --fxaa --oit --threaded --hidden --frames 12 --capture build/fxaa.ppm
.\build\development\Release\development_scene.exe --fxaa
```

两个交互示例均可按 F 切换 FXAA，只处理 Press，长按不重复切换。主示例标题显示当前开关状态。SDK 独立使用者示例也开启 FXAA 与 OIT，验证安装后的接口和静态库一致。

## 验证与实现位置

`renderer.opengl.aa` 使用真实 OpenGL 回读，分别在单线程和专用渲染线程验证斜边、平坦颜色、开关恢复、两种透明、HDR/ToneMap、异步计时和缩放后的尺寸。

斜边用独立 8×8 几何覆盖率生成感知颜色参考，而不是要求某些像素简单变灰。当前 256×192 白色三角形用例的平均平方误差从 55.0931 降为 11.5096；该数值仅说明此用例的边缘改善，不代表任意画面的统一质量或性能提升。

```powershell
ctest --test-dir build/windows-gpu-tests -C Debug -R renderer.opengl.aa --output-on-failure
# 可选：保存该测试场景的原图与 FXAA 图，目录由测试创建。
.\build\windows-gpu-tests\Debug\renderer_aa_tests.exe build/aa-comparison
```

- `include/renderer/renderer.hpp`：AntiAliasing、RenderView 开关及统计字段。
- `src/frame_builder.cpp`、`src/internal/frame.hpp`：参数校验和帧设置传递。
- `src/backends/common/portable_shaders.hpp`：感知颜色输出变体、FXAA Shader。
- `src/backends/common/renderer_backend.cpp`：可选目标、Pipeline、图关系、绑定及绘制统计。
- `tests/engine_tests.cpp`、`tests/aa_gl_tests.cpp`：CPU 参数与 GPU 图像/生命周期验证。

未引入新的第三方链接库或开发依赖；编译和安装仍使用现有 Shader 工具链与 SDK 规则。
