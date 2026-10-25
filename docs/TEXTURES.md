# 压缩纹理、数组与离线工具

[文档导航](README.md) · [精灵](SPRITES.md) · [文字质量与缓存](TEXT.md) · [对照示例](../examples/texture_text/README.md)

纹理数据与 DDS 工具属于 core，两种后端共用相同接口。无需开启额外模块；公开接口不包含 OpenGL、Vulkan 或第三方类型。DDS 保存 GPU 可直接上传的块载荷，加载后不常驻整图 RGBA 解码副本。

## 格式与适用范围

| 格式 | 每 4×4 块 | 用途与编码支持 |
| --- | --- | --- |
| RGBA8 / sRGB | 64 字节 | 无损原始图片，可离线生成 mip |
| BC1 / sRGB | 8 字节 | 不透明颜色；内置 CPU 编码要求 Alpha 全为 255 |
| BC2 / sRGB | 16 字节 | 读取外部 DDS、解码和 GPU 上传；无内置编码 |
| BC3 / sRGB | 16 字节 | 含透明的图片精灵，内置 CPU 编码 |
| BC4 | 8 字节 | 线性单通道，内置 CPU 编码，解码为 R/0/0/1 |
| BC5 | 16 字节 | 线性双通道，内置 CPU 编码，解码为 R/G/0/1 |
| BC7 / sRGB | 16 字节 | 读取外部 DDS、解码和 GPU 上传；无内置编码 |

这些是桌面 BC 格式，不包含 ASTC、ETC、KTX2/Basis。BC 会损失画质；小字、精细 UI 和很细的透明边缘通常继续使用 RGBA8。BC3 的平滑 Alpha 适合常见树木/角色图片；BC1 的本地编码器没有透明模式。BC4/5 不允许 sRGB。PBR 法线图使用 BC5 时，Shader 将 RG 映射到 XY 并重建正 Z；其他颜色贴图不会执行此转换。

OpenGL 根据驱动的 S3TC/RGTC/BPTC 支持查询格式能力，Vulkan 查询 BC 特性和逐格式能力。高层默认在格式不支持时临时解码为 RGBA8 上传，不修改 AssetStore 中的压缩原件；`RendererConfig::textureCompressionFallback=false` 可要求明确失败。`forceTextureDecompression=true` 用于验证降级路径，优先强制使用 RGBA8。RHI 的 createTexture 直接拒绝不支持格式，不自动替换格式。

## 创建、编码和加载

```cpp
#include <renderer/texture.hpp>
using namespace renderer;

AssetStore assets;
auto original = assets.loadTexture("tree.png", true);
auto bc3 = texture::compress(assets.textureDescription(original),
                             TextureCompression::BC3, true);
texture::saveDds(bc3, "cache/tree.dds");
auto tree = assets.loadTexture("cache/tree.dds");
```

compress 支持单层和数组，以第 0 级为源重新生成完整 mip 链。texture::mipmaps 生成未压缩 RGBA8 的完整链。面积盒过滤覆盖奇数边缘；sRGB 在线性空间过滤，RGB 按 Alpha 加权后恢复普通 Alpha，减轻透明边缘串色。大量资源应在打包阶段处理；运行时只读取 DDS。AssetLoader 也可后台读取 DDS，再通过 AssetStore::adopt 接收。

DDS 支持二维、二维数组、完整或部分 mip，DX10 中的 RGBA8/BC1/2/3/4/5/7，以及常见 legacy DXT/ATI 格式。DX10 的格式决定 sRGB，loadTexture 的 srgb 参数不会覆盖 DDS 自带元数据；旧 DDS 按线性处理。拒绝立方体、体纹理、signed BC4/5、BC6H、未知格式和截断/多余载荷。最大边长 16384、数组层数 2048、单个描述载荷 1 GiB，设备还可能有更低限制。

TextureDesc 有两种 CPU 数据形式：pixels 保存所有层的第 0 级 RGBA8，要求 mipLevels=1、compression=None；data 保存显式层/mip 载荷，pixels 必须为空。data 的顺序为第 0 层的所有 mip、第 1 层的所有 mip……每级块行紧密排列。texture::subresource 返回对应字节范围。显式 data 不在 GPU 自动生成 mip；压缩载荷必须离线提供所需各级。

origin 描述行序。普通图片和 texture::decode 返回 BottomLeft，DDS 与 compress 返回 TopLeft。高层采样自动处理两种行序，包括法线/MR/AO/自发光贴图；CPU Alpha 拾取也处理。RHI 不翻转上传字节；回读按照 RHI 的坐标约定返回 TopLeft，直接使用 RHI 上传 DDS 时应自行匹配 UV。saveDds 不重新压缩；压缩 data 必须已经是 TopLeft。

## 纹理数组与合批

```cpp
std::array<TextureDesc, 3> pictures{treeA, treeB, treeC};
auto array = texture::makeArray(pictures); // 尺寸与颜色空间必须一致
auto textureHandle = assets.createTexture(texture::compress(array, TextureCompression::BC3));
SpriteDesc description;
description.texture = textureHandle;
description.rect = {0, 0, treeA.width, treeA.height};
description.textureLayer = 0;
auto sprite = assets.createSprite(description);

// 同一 Sprite 的不同实例选择不同图片，层号不拆分材质批次。
SpriteRenderer instance;
instance.sprite = sprite;
instance.textureLayer = 2;
scene.setComponent(node, instance);
```

makeArray 解码各输入的基准层并合并为 RGBA8 数组，可继续离线压缩。所有层大小/格式/mip 数相同；不同尺寸图片应预先填充或放入图集。SpriteRenderer::textureLayer 默认 UINT32_MAX，表示使用 SpriteDesc 的层号。UV/裁切/翻转规则继续适用于当前层。普通网格可通过 MaterialDesc::baseTextureLayer 选择基色层。高层目前仅基色贴图支持数组，辅助贴图保持二维。

数组避免图集相邻图块过滤串色，但仍受透明绘制顺序和材质/管线状态约束；只合并排序后相邻兼容实例。它没有变更 OIT 或有序透明的规则。

## RHI、UI 与统计

RHI TextureDesc 的 layers>1 或 arrayTexture=true 建立二维数组。Device::upload 的最后一个参数选择层；createTextureView 可选择 mip/层范围，单层视图默认变为普通二维，arrayView=true 保留数组维度。Shader 反射识别 sampler2DArray / image2DArray，绑定时验证维度；支持数组采样、Storage Image、逐层上传/回读/拷贝。数组不能直接作为整层 framebuffer 附件，需创建单层二维视图。压缩纹理不可作为颜色/深度附件或 Storage Image，不支持 GPU generateMipmaps。

RenderGraph 的范围跟踪仍以 mip 为单位，数组层保守地视作同一资源。多个别名视图需应用统一建模。压缩拷贝要求起点为 4×4 块边界，宽高须按块对齐或到达源和目标的边缘。

UiContext::registerImage(name, description, layer) 可以选取数组层；压缩/DDS 数据在 UI 适配层解码、转线性预乘为普通二维图片。文件方式读取 DDS 使用第 0 层。UI 此路径不保留压缩 GPU 存储；实时 RenderTexture 注册保持原有 GPU 路径。

AssetMemoryStats::textureBytes 统计实际 pixels/data 载荷。RendererMemoryStats 的 textureBytes 包含实际 GPU mip/层字节，compressedTextureBytes 是其中仍以 BC 存储的部分；textureArrayLayers 统计缓存中数组对象的层数。降级为 RGBA8 会增大 GPU 字节与上传预算，预算按实际上传载荷计算。统计不包含驱动分配粒度或暂存开销，旧快照保留的版本也可能继续占用内存。

命令行打包器、双后端窗口/离屏示例和 SDK 用法见[对照示例](../examples/texture_text/README.md)。

## Alpha8 与不可变局部更新

TextureChannels::Alpha8 是线性单通道覆盖率，DDS 使用 DX10 A8，GPU 使用 R8。AssetStore::updateTexture 返回新句柄并保留原版本；当前限单层单 mip、关闭 mip 生成的未压缩 CPU 图片。区域、驻留父页复制与降级完整上传见[优化文档](OPTIMIZATION.md)。RHI R8 保持红通道语义，高层基色将其恢复为白 RGB/覆盖率；UI 仍转换到自身 RGBA 约定。
