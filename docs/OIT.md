# 可选加权透明：Weighted Blended OIT

[返回文档导航](README.md) · [接口说明](API_GUIDE.md)

## 开启方式

透明策略按 `RenderView` 选择，默认仍为 `SortedAlpha`。模型和材质接口保持一致，只有 `AlphaMode::Blend` 表面进入透明阶段。

```cpp
renderer::RenderView view;
view.cameraNode = cameraNode;
view.transparency = renderer::TransparencyMode::WeightedBlendedOIT;
engine.render(extractor.extract(scene, assets), view);
engine.present();

// 同一个 Renderer 可在帧之间切换，内部自动重建对应渲染图。
view.transparency = renderer::TransparencyMode::SortedAlpha;
```

交互示例提供 `--oit`：

```powershell
.\build\windows-gpu-tests\Debug\renderer_demo.exe --threaded --oit
.\build\windows-gpu-tests\Debug\renderer_demo.exe --threaded --oit --hidden --frames 30 --capture build/oit.ppm
```

## 两种模式

下表描述普通网格及 `SpriteTransparency::FollowView` 精灵。默认 `SpriteTransparency::Sorted` 的 Blend 精灵在 OIT Resolve 后单独执行 `Ordered Sprites`，保持落地点/排序组关系；存在这类可见精灵时再增加一个 Pass。两个透明队列之间是固定先后关系，不能保证逐层精确交错，见 [Sprite 说明](SPRITES.md)。

| 行为 | SortedAlpha | WeightedBlendedOIT |
| --- | --- | --- |
| 透明排序 | 实例包围盒中心深度，从远到近 | 材质、网格、submesh、镜像绕序 |
| 合批 | 只合并深度排序后相邻的兼容项 | 相同状态可聚类并实例化 |
| 深度测试 / 深度写入 | 开启 / 关闭 | 开启 / 关闭 |
| 透明输出 | 直接混合到 HDR | 累积颜色和透过率，再合成到 HDR |
| 透明阶段 | Transparent | OIT Accumulation + OIT Resolve |
| 图的 Pass 数（FXAA 关闭时） | 5 | 6 |

Opaque、Mask、灯光和阴影规则不变。Mask 保留 Alpha 裁剪与深度写入；Blend 物体仍不投射阴影，可以接收阴影。

## 合成原理

```text
Shadow -> Opaque PBR -> OIT Accumulation -> OIT Resolve -> ToneMap -> Present
```

透明累积加载不透明阶段的场景深度，只测试、不写入，避免被墙等不透明表面遮挡的透明片元参与计算。两个附件使用独立的加法混合状态：

| 目标 | 格式 / 清除值 | 写入值 | 混合方式 |
| --- | --- | --- | --- |
| 颜色累积 | RGBA32F / 0 | `(颜色 * alpha, alpha) * weight` | 源 ONE、目标 ONE |
| 剩余透过率 | R32F / 1 | `alpha` | 源 ZERO、目标 ONE_MINUS_SRC_COLOR |

由此得到：

```text
accum.rgb = Σ(color * alpha * weight)
accum.a   = Σ(alpha * weight)
revealage = Π(1 - alpha)

averageColor = accum.rgb / max(accum.a, epsilon)
coverage     = 1 - revealage
HDR          = averageColor * coverage + opaqueHDR * revealage
```

加法累积和透过率乘法在数学上可交换，不需要按物体深度提交；实际浮点运算可能存在微小舍入差异。Resolve 使用全屏三角形和 Alpha 混合加载既有 HDR，避免读写同一 HDR 纹理形成反馈。没有透明片元时透过率为 1，Resolve 丢弃片元以保持背景。

所有光照、累积和合成都在线性空间完成，曝光、ToneMap 与 sRGB 编码放在之后。PBR 与 unlit 共用原来的材质着色逻辑，只替换片元输出阶段。

可以同时设置 `view.antiAliasing = AntiAliasing::FXAA`，在 ToneMap 后过滤最终画面。开启时以上两种模式都再增加一个 Pass，OIT Resolve 仍在 ToneMap 和 FXAA 之前。详见[抗锯齿说明](ANTI_ALIASING.md)。

## 精度和代价

当前权重启发式结合 Alpha 与 [0,1] 窗口深度，范围限制为 0.01–3000。它让较近、覆盖率较大的表面占更大颜色权重，但不会产生与逐像素精确排序完全一致的结果。权重定义见 `src/backends/common/portable_shaders.hpp`。

累积使用 RGBA32F，透过率使用 R32F，共增加每像素 20 字节的逻辑颜色存储，不含驱动分配开销。1920×1080 时约 39.6 MiB；还增加透明多附件写入和一个全屏合成阶段。FP32 累积用于避免高亮 HDR 与多层权重之和超出 FP16 的范围，默认排序模式不分配这两个目标。

OIT 没有让透明过度绘制消失；收益取决于排序/合批节省与显存带宽开销。深度相近、高覆盖率或强烈不同颜色的多层表面可能出现层次弱化、颜色渗混。当前没有 Depth Peeling、精确逐像素排序、折射或彩色透射。

## 实现位置与统计

- `include/renderer/renderer.hpp`：TransparencyMode、视图配置与 Resolve 绘制统计。
- `include/renderer/rhi.hpp`：ColorBlendState 与逐附件混合描述；为空时兼容原有 BlendMode。
- `src/frame_builder.cpp`：根据透明策略选择深度排序或状态排序。
- `src/backends/common/portable_shaders.hpp`：共享 PBR 输出、权重及 Resolve。
- `src/backends/common/renderer_backend.cpp`：两套图路径、目标生命周期、Pipeline 缓存和切换。
- `src/backends/opengl/rhi_device.cpp`：独立附件混合校验和 OpenGL indexed blend 状态设置。

`transparentDrawCalls` 只统计透明几何绘制；`transparencyResolveDrawCalls` 在 OIT 模式为 1，否则为 0。`drawCalls` 包含 Resolve，`triangles` 仍仅统计场景几何。GPU Pass 计时来自已完成的旧帧，模式刚切换时可能暂时显示上一模式的 Pass 名称。

CPU 测试检查 OIT 聚类合批、默认排序行为及无效枚举；GPU 测试检查真实绘制顺序反转、交叉表面、覆盖率、零/全 Alpha、不透明深度遮挡、运行时模式切换、多层 HDR、实例化、异步计时、窗口缩放、空帧清除及渲染线程。

算法参考：[McGuire / Bavoil 原论文](https://jcgt.org/published/0002/02/09/)，[作者的实现说明](https://casual-effects.blogspot.com/2015/03/implemented-weighted-blended-order.html)。本实现使用相同累积/透过率思路，并调整权重尺度及缓冲精度以配合当前 HDR 管线。
