# 世界空间文字模块

[文档导航](README.md) · [模块划分](MODULES.md) · [SDK](SDK.md) · [文字示例](../examples/text/README.md) · [游戏示例](../examples/game/README.md)

`renderer::text` 是可独立启停的 FreeType 世界文字模块。它管理字体、回退、排版、字形图集和标签，通过核心公共 SpriteBatch 接入场景；不依赖 UI、RmlUi、窗口或原生图形 API。TextSystem 使用 PImpl，公开头文件不包含 FreeType 类型。

## 与屏幕 UI 的区别

| 入口 | 坐标与用途 | 渲染位置 |
| --- | --- | --- |
| ui / OverlaySnapshot | 菜单、HUD、背包等屏幕控件 | ToneMap/FXAA 之后 |
| text / SpriteBatch | 路牌、名字牌、世界提示和伤害数字 | 场景透明队列，经过场景后处理 |

文字标签可以绑定 Scene 节点，继承父子变换和可见性。模块没有把字体解析或排版放入 Scene/core；Scene 不需要依赖 FreeType，也不新增强制的 TextRenderer 组件。应用管理标签身份与玩法，把文字批次与地图/粒子批次一起交给 RenderExtractor。

UI 与 text 使用同一份静态 FreeType 代码，分别拥有字体对象和字形缓存；text 不读取 RmlUi 的内部缓存，也不要求创建 UiContext。

## 构建和安装

默认 `RENDERER_BUILD_TEXT=ON`。关闭后不构建/安装文字库与专用头文件，也不构建其示例/测试；综合游戏依赖全部模块，因此缺失 text 时跳过。UI 和 text 都关闭后不下载或构建 FreeType。

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS text vulkan)
target_link_libraries(my_game PRIVATE renderer::text renderer::vulkan)
```

CPU 工具可以只请求 text。SDK 私有传递 FreeType 静态库，不安装其开发头文件或独立包；消费者无需下载字体库或安装 Vulkan SDK。完整 SDK 每配置只安装一份 FreeType。

FreeType 固定为 2.14.3，URL/SHA256 在 cmake/rendererFreetype.cmake；离线源码配置可用 FETCHCONTENT_SOURCE_DIR_FREETYPE。示例字体为上游固定 6.3 版本的 LatoLatin-Regular.ttf，路径由 renderer_TEXT_FONT 提供。UI 开启时复用其字体文件；独立 text 配置仅下载这一个示例字体，不获取 RmlUi 源码。可用 FETCHCONTENT_SOURCE_DIR_RENDERER_DEMO_FONT 指定包含该文件的目录。字体与 FreeType 的许可原文随 SDK 安装，详见[第三方说明](../THIRD_PARTY.md)。

## 最小接入

```cpp
#include <renderer/text/text.hpp>
#include <renderer/renderer.hpp>
using namespace renderer;

TextSystem text;
auto font = text.loadFont(fontPath);
WorldTextDesc name;
name.style.font = font;
name.style.fontSize = 24;
name.text = "Ranger";
name.node = playerNode;
name.transform = Mat4::translation({0, 2.2f, 0});
name.billboard = BillboardMode::ViewPlane;
name.sizeMode = TextSizeMode::ScreenPixels;
name.depthTest = true;
auto nameId = text.create(name);

// 每帧先更新角色/物理、相机，再为本次视图构建批次。
auto letters = text.batch(scene, cameraNode, viewportWidth, viewportHeight);
std::vector<SpriteBatch> batches{terrainBatch, particlesBatch, letters};
auto snapshot = extractor.extract(scene, assets, batches);
renderer.render(snapshot, view);
```

text 自己持有字形 AssetStore，返回的批次已解析并保留资源；不要把其字体/字形句柄传给应用 AssetStore。字体和标签 ID 属于创建它们的 TextSystem，跨系统使用会被拒绝。

标签绑定有效节点时，最终变换为 `scene.worldTransform(node) * description.transform`；未绑定时 transform 为世界矩阵。绑定节点被销毁、属于其他场景或继承隐藏时，该标签跳过发布，应用仍应根据生命周期调用 destroy。改变父节点变换会在下一次 batch 中生效，不需要重新创建字体或文字。

## 字号、朝向与锚点

| 参数 | 作用 |
| --- | --- |
| style.fontSize | 逻辑像素字号 [1,256]；位图按字号缓存，SDF 共享规范字号缓存 |
| pixelsPerUnit | WorldUnits 下每世界单位对应多少排版像素，值越大世界文字越小 |
| Fixed | 使用完整节点 XY 平面，支持旋转、缩放、非均匀缩放和剪切 |
| Upright | 保持世界 Y 向上，按主相机水平方向旋转；保留节点 XY 轴尺度 |
| ViewPlane | 平行主相机平面；世界尺寸模式保留节点 XY 轴尺度 |
| ScreenPixels | 按当前投影/深度和视口计算世界几何，维持实际像素字号；只允许 ViewPlane |
| anchor | 左下原点归一化逻辑排版框，[0,1]；默认底部居中，{.5,.5} 居中 |

文字的排版像素以左上为原点，生成世界几何时转换为局部 Y 向上。锚点根据逻辑排版框计算，字体自身的 bearing/升降部会让可见墨迹与逻辑框略有差异。

ScreenPixels 使用物理 framebuffer 像素，不自动应用 UI 的 density。需要例如 20dp 的名字牌时，应用将实际像素字号设为 `round(20*dpiScale)`。此模式只继承节点的位置，忽略节点的缩放和朝向，防止角色缩放使名字牌尺寸改变；透视、正交、固定宽高比相机及视口缩放都有对应计算。

WorldUnits 使用普通世界尺寸，透视距离变远时会自然变小。普通位图大幅放大时会变软；经常缩放的文字可选择 SDF，下文说明两种模式的取舍。

## 遮挡、排序与拾取

depthTest=true 时，字形使用普通透明精灵路径，深度测试开启、深度写入关闭，可被不透明/Mask 场景遮挡。文字始终使用有序透明：即使视图开启 Weighted Blended OIT，字形也在 OIT 合成后按原有精灵排序规则绘制，避免对字形颜色进行近似混合。

depthTest=false 时，使用共用的 Unoccluded World Sprites Pass，在普通透明/有序精灵之后关闭深度读写。它保留世界坐标、投影和视锥裁剪，仍然在 ToneMap/FXAA 之前，因此会受到场景曝光/调色影响；需要完全独立的 HUD 应使用 ui。

渲染器首次遇到此队列时启用该 Pass，之后空队列跳过绘制，避免伤害数字出现/消失时反复重建场景附件。

sortingLayer/sortingOrder 较大者后绘制，同一标签字形共享一个排序点并保留字形顺序。模块只合并相邻兼容状态，不通过打乱透明顺序合批。字形不投射阴影，也不受场景光照，柔和边缘来自 FreeType 覆盖率和普通 Alpha 混合。

返回批次的每个字形 instanceId 都是对应的 WorldTextId。可选 interaction 的 SceneQuery 可进行字形 Alpha 拾取并返回这个身份；其 node 为空，因为批次没有逐字形 Scene 节点。关闭深度的字形/精灵在拾取中也具有相应优先级。查询只考虑其已有精灵数据，不会自动求解所有网格的可见性；没有专门的整段文字矩形命中/编辑光标接口。

## 排版、回退与动态更新

```cpp
TextStyle style{.font = font, .fontSize = 32,
                .maxWidth = 220, .lineSpacing = 1.2f,
                .alignment = TextAlignment::Center};
auto metrics = text.measure(style, "First line\nSecond line");

auto chinese = text.loadFont(chineseFontPath);
text.setFallbackFonts(std::span{&chinese, 1});
text.setText(nameId, "Player 42");
text.setColor(nameId, {1, .7f, .2f, .8f});
text.setTransform(nameId, Mat4::translation({0, 2.4f, 0}));
```

文本是严格 UTF-8，支持 CR/LF、Tab、左/中/右对齐和字形级自动换行。maxWidth 使用排版像素，0 不自动换行；单字比给定宽度更大时允许溢出，不做裁切。Tab 按空格宽度停靠。measure 不需要场景或 GPU，可生成必要的 CPU 字形缓存。

拉丁字距使用 FreeType kerning。首版没有 HarfBuzz、双向排版、连字塑形、词典断词、富文本/逐字样式或彩色 Emoji；支持灰度/单色字形位图。中文应加载包含中文字形的轮廓字体并加入回退列表，示例 Lato 不包含完整中文。找不到字符时使用主字体的替代符号/问号/缺字字形。

loadFont 支持 Unicode 文件路径和字体集合索引，文件最多 64 MiB。字体、标签操作和 batch 都在创建 TextSystem 的线程进行；模块不创建线程。渲染线程只读取已发布 SpriteBatch/RenderSnapshot。

## 缓存、资源与性能

字体文件只读取一次。字形按实际字体、栅格字号、字形索引与模式缓存；位图使用 fontSize，SDF 使用统一的 sdfFontSize，因此不同显示字号共享字形。颜色、变换、可见性变化不重新栅格化。静态标签保留排版结果，setText/setDescription 或回退列表变化后按需重新排版。setText 对相同内容不更新。

灰度字形默认存入带透明边框的 Alpha8/R8 图集，采样时恢复白 RGB 与覆盖率，关闭 mip；compactAtlas=false 保留 RGBA8 对照。普通字形会复用同页材质与四边形，利用核心实例合批。文字的三角形和实例计入普通场景统计，不计为 UI overlay。

图集按需增加。页面变更时发布新的不可变纹理和字形资源，释放当前仓库中的旧句柄；旧批次/快照仍保留旧资源，因此修改文字、扩展图集甚至销毁 TextSystem 都不会破坏已提交帧。GPU 缓存与延迟销毁由既有渲染器处理。

TextSystemConfig 限制页大小/数量、缓存字形数、标签数和单批字形数。automaticEviction 默认开启：字形数不足时移除最旧的未使用字形，页面不足时淘汰最旧的未使用页面；空洞在整页淘汰后回收，不实时搬移图集。batch 会先保护本次视图所有待发布标签所需的字形，measure/prewarm 保护本次文本；保护内容仍放不下时抛出容量异常，需提高上限或拆分系统。将 automaticEviction=false 可恢复严格容量模式。被淘汰字形关联的标签布局会在下次使用时重建。

trimCache(targetPages=0) 主动回收冷页面，会保护所有 description.visible=true 标签所需的页面，直到达到目标或没有可回收页，返回实际释放页数；此处不依据场景节点/视锥再筛选。clear 只清标签。prewarm(style,text) 提前生成 CPU 字形/图集；resetGlyphCache 显式回收全部当前缓存并使标签重新排版；unloadFont 仅卸载未被标签和回退列表使用的字体并重建共享缓存。旧批次仍有效。

statistics 提供当前字形/页面/像素字节、累计 cacheHits/cacheMisses/evictedGlyphs/evictedPages，以及 fontFileBytes。页面与字体统计不包含旧快照独立保留的版本、FreeType 内部内存或分配器开销。失败的测量/批量构建可能已增加部分 CPU 缓存，不保证回滚缓存，但不会修改已返回的不可变批次。LRU 容量约束针对当前缓存，旧帧仍有引用时不会强制破坏其资源。

每次 batch 为当前相机展开世界几何并发布精灵数组，之后核心执行整体/逐字形裁剪、排序和 GPU 上传。多相机需要分别调用 batch；不要把针对一个相机计算的 Billboard/屏幕字号批次复用于另一个相机。大量标签可按应用区域拆分 TextSystem/批次，当前没有自动文字分块；整理后的实例可受核心帧槽增量上传缓存保护，跨视图/排序重排仍可能产生更新。

## SDF、抗锯齿与描边

```cpp
TextSystem text({.sdfFontSize = 64, .sdfSpread = 8});
TextStyle style;
style.font = text.loadFont(fontPath);
style.fontSize = 36;
style.rasterMode = TextRasterMode::Sdf;
style.outlineColor = {.03f, .2f, .25f, 1};
style.outlineWidth = 1;
style.edgeSoftness = 1;
text.prewarm(style, "森林营地 Player 0123456789");
```

SDF 使用 FreeType 距离场光栅器，缓存规范字号的轮廓距离，再按逻辑字号缩放几何、推进量和 kerning。Shader 在线性 Alpha 通道读取距离，通过屏幕导数重建平滑覆盖率；outlineWidth 生成外描边，颜色与填充正确合成。描边按逻辑字号像素计，edgeSoftness [0.25,4] 调整边缘过渡，默认 1。Bitmap 模式仍使用原覆盖率与内描边，默认保持位图模式。

sdfFontSize [16,128] 默认 64，sdfSpread [2,32] 默认 8；更大的规范字号提高细节、增加图集占用，spread 增大可容纳更宽描边但降低同一字节范围内的距离精度。开启描边时要求 `outlineWidth*sdfFontSize/fontSize < min(sdfSpread,16)`，超限明确报错，避免外沿裁断。墨迹度量不包含距离场空白边框或描边。

SDF 适合可缩放名字牌、路牌与大字；较小的固定像素文字可使用位图 hinting。当前是单通道距离场，极细笔画/尖角和大幅缩小仍有精度限制，不是 MSDF，也未实现复杂文字塑形或彩色 Emoji。中文可使用包含中文字形的轮廓字体；不同显示字号共享同一 SDF 缓存。图集默认 Alpha8/R8、无 mip，不进行有损 BC 压缩。

核心 SpriteDesc::distanceFieldRange 与 SpriteRenderer::edgeSoftness 也支持应用提供的 Alpha SDF 图片。CPU 拾取按第 0 级距离 0.5 判断内轮廓，不模拟屏幕导数或外描边，因此不是最终像素逐点匹配。SDF 世界文字保持原有深度、排序、OIT 和快照语义；UI 字体仍由 RmlUi 独立管理，可用 UiContext::releaseFontCache 显式释放并重建。

## 示例与验证

- [text 示例](../examples/text/README.md)：固定路牌、竖直 Billboard、移动名字牌、遮挡/字号开关和双后端 PNG。
- [game 示例](../examples/game/README.md)：跟随角色的名字牌、火堆文字和上浮/淡出的伤害数字。
- renderer.text：字体/回退、严格 UTF-8、排版/换行、kerning、图集缓存/容量、绑定/隐藏/销毁、屏幕字号和快照生命周期。
- renderer.text.opengl / renderer.text.vulkan：真实像素遮挡、深度开关、OIT/FXAA、屏幕字号、缩放、排队旧帧和字体销毁。
- [texture_text 示例](../examples/texture_text/README.md)：并排比较位图/SDF 放大、外描边与压缩/数组精灵，支持双后端窗口和离屏 PNG。
- renderer.text.quality：SDF 字号复用、度量、LRU/保护/严格容量、旧批次资源保留；renderer.{opengl,vulkan}.text_quality 检查实际平滑边缘、描边和回收后的旧帧像素。
- 四项 renderer.text.{opengl,vulkan}.{window,offscreen}.example：窗口/离屏、渲染线程、实际文字和 PNG。

复现命令及实测范围见[验证记录](VALIDATION.md)。

图集更新现在合并脏矩形，并通过不可变新版本从旧 GPU 页面复制后局部上传。首次使用或父页面不驻留时完整上传；compactAtlas/incrementalAtlas 可关闭作参考。TextStats 增加 pageRevisions/dirtyPixelBytes，实际 GPU 字节以 RenderStats 为准。详情见[上传与图集优化](OPTIMIZATION.md)。
