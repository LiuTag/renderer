# 材质效果、混合与实时相机纹理

[文档导航](README.md) · [异步资源](ASSET_LOADING.md) · [调试统计](DIAGNOSTICS.md) · [示例](../examples/features/README.md)

这些能力位于 core 和双后端共用管线。应用不需要包含 OpenGL/Vulkan 头文件，可选 ui/text 继续只依赖 core 公共接口。

## 自定义材质函数

MaterialDesc::effect 接受 GLSL 函数体，ShaderCompiler 继续负责 SPIR-V、反射和 OpenGL 转译。没有用户自己的 main 或额外绑定声明。

```cpp
MaterialDesc material;
material.baseColorTexture = treeTexture;
material.unlit = true;
material.doubleSided = true;
material.alphaMode = AlphaMode::Blend;
material.effect.vertex = R"(
    position.x += sin(effectTime*1.8 + position.y*4.0)
                * effectParameters.x * (position.y+0.5);
)";
material.effect.surface = R"(
    base.rgb *= mix(vec3(1), vec3(1,0.6,0.3), effectParameters.y);
)";
material.effect.parameters = {.04f, 0, 0, 0};
material.effect.boundsPadding = .15f;
auto handle = assets.createMaterial(material);
view.timeSeconds = applicationSeconds;
```

vertex 可修改 position、normal、texcoord，分别为局部位置、法线和原始 UV。之后共用代码执行世界变换与图集 UV 展开。surface 可修改线性 RGBA `base`，读取展开后的 `uv` 和 `worldPosition`。两个函数都可读取四个浮点 effectParameters 和以秒为单位的 effectTime；时间由 RenderView 提供，默认 0。

surface 在 Alpha Mask 裁剪之前执行，随后处理精灵描边/覆盖色和光照。方向光阴影复用顶点效果与 surface Alpha 裁剪；材质原有投影/透明限制保持一致。函数体每段最多 64 KiB；编译错误随渲染调用/future 返回诊断。相同函数源码与状态复用 Pipeline；只改参数不会创建另一种 Shader，但不可变材质仍需创建新资源并替换句柄。

位移必须提供保守的世界空间 boundsPadding，否则原几何可能已在 CPU 被裁剪。它只扩大可见性与阴影范围，不改变 CPU 拾取的原始几何/Alpha；自定义溶解、UV 变形等效果的精确交互应由应用处理。

精灵可指定 `SpriteDesc::material`。材质必须引用相同 baseColorTexture，Alpha 模式、阈值、受光与混合方式以材质为准；未指定时继续自动共享默认精灵材质。这个接口也能用于粒子所引用的精灵。

## 透明输出混合

| MaterialBlend | 颜色合成 | 用途 |
| --- | --- | --- |
| Alpha | 源颜色×源 Alpha＋目标颜色×(1−源 Alpha) | 普通半透明图片 |
| PremultipliedAlpha | Shader 输出预乘 RGB，目标仍乘 (1−源 Alpha) | 与预乘输出一致的管线组合 |
| Additive | 源颜色×源 Alpha＋目标颜色 | 发光粒子、火花、魔法 |

源纹理统一提供普通未预乘 RGBA；PremultipliedAlpha 的乘法发生在输出阶段，不要预先处理源图片。默认 Alpha。Blend 以外的材质不执行透明混合；排序与深度写入规则继续由 AlphaMode 决定。

```cpp
SpriteDesc glow = spriteDescription;
glow.alphaMode = AlphaMode::Blend;
glow.blend = MaterialBlend::Additive;
auto glowSprite = assets.createSprite(glow);
// ParticleEmitterDesc::sprite = glowSprite;
```

SortedAlpha 保持既有绘制顺序。Weighted OIT 下，加色物体进入 OIT 合成后的有序队列，不污染透射率；普通 FollowView 透明仍进入 OIT，Sorted 角色保持有序。PremultipliedAlpha 在 OIT 中使用相同的加权累积公式，Resolve 后再合成。

## 多相机直接渲染到 GPU 纹理

```cpp
auto mapOutput = renderer.createRenderTexture({300, 200});
RenderView mapView = mainView;
mapView.cameraNode = mapCamera;
mapView.overlay = {};

// 同一 Renderer、同一队列；这里不需要窗口呈现或 CPU 回读。
renderer.renderToTexture(snapshot, mapView, mapOutput);

auto mapTexture = assets.createTexture(mapOutput);
// mapTexture 可以用于普通材质/精灵。
ui.registerImage("map", mapOutput);
// RML: <img src="ui:map" width="300" height="200"/>
mainView.overlay = ui.snapshot();
renderer.render(snapshot, mainView);
renderer.present(); // 仅窗口模式
```

RenderTexture 采用共享 PImpl，保留尺寸和所属渲染器身份，不公开原生句柄。初始黑色，输出为 sRGB，Alpha 固定为 1，不生成 mip。每个输出保存独立 RenderGraph/附件缓存，视口和设置不变时复用；GPU 复制和屏障确保后续视图能采样。renderToTexture 等待 CPU 提交，GPU 继续异步执行；不在 CPU 下载和重新上传像素。

它必须在创建它的 Renderer 内写入和采样。跨渲染器或渲染器已销毁时明确拒绝。本次输出不能同时作为该视图的输入；需要反馈效果时使用两个目标轮换。视图/层掩码可排除显示这张纹理的物体。

尺寸不可变，需要另一分辨率时创建新输出并替换应用引用。创建输出会初始化其像素并分配资源，宜在加载阶段完成。所有引用释放后，缓存和原生纹理在后续收集/提交完成后释放。切换目标时保留各自图，额外相机也会增加附件、帧准备和绘制成本。

普通资源/快照的不可变契约继续成立；RenderTexture 是明确的实时内容例外，同一个引用按渲染队列执行顺序看到最新内容。CPU TextureDesc 的 pixels 为空，不能用作图片像素数据；高层输出完全不透明，因此 CPU Alpha 拾取按不透明图片处理。readback/renderToImage 仍是显式同步回读路径。

每个相机的 Billboard 和屏幕字号文字都要单独展开；不能复用另一相机的文字批次。小地图可以使用相机层过滤，仅显示地面和标记，从而减少开销。

## 文字和中文输入增强

TextSystem::prewarm 可在加载阶段预生成文字；resetGlyphCache 回收当前字形/图集并使标签在下次 batch 重新排版，旧快照仍有效。unloadFont 卸载没有标签或回退列表引用的字体。支持可选 SDF、缩放抗锯齿与外描边、自动 LRU 和 trimCache；UI 提供共享字体缓存回收。参数与限制见[文字文档](TEXT.md)。压缩/数组纹理及 DDS 工具见[纹理文档](TEXTURES.md)。

UI 使用 TextCompositionEvent 区分预编辑与提交，支持替换选区、组合光标、取消和失焦恢复。Windows 由 IMM32 转换原生消息，并按 UiContext::textInputArea 给出的客户区区域定位候选窗口；应用需要每帧传给 Window::setTextInputArea。其他平台保留已提交字符输入，尚未提供其原生预编辑适配。

```cpp
ui.setPlatformServices({
    [&](const auto &value) { window.setClipboardText(value); },
    [&] { return window.clipboardText(); }
});
// pollEvents 的统一订阅中先交给 ui.processEvent(event)。
ui.update(dt);
window.setTextInputArea(ui.textInputArea());
```

服务闭包的有效期由应用保证；未注入时使用内部剪贴板，离屏/CPU-only UI 不要求窗口。中文仍需应用提供字体；示例可在本机读取 Windows 微软雅黑，也可用 --font 指定字体，不随 SDK 再分发系统字体。
