# 图片角色与三维世界：Sprite / Billboard

[文档导航](README.md) · [二次开发手册](DEVELOPMENT_GUIDE.md) · [SDK 安装](SDK.md)

当前实现第一阶段的 2.5D 游戏表现基础：图片资源和图集、三种 Billboard、方向序列帧、落地点排序、部件排序组、屏幕射线与图片 Alpha 拾取。使用现有 Scene → RenderSnapshot → Renderer 链路，支持 OpenGL 4.5 Core 和 Vulkan 1.3。

## 运行森林示例

在源码根目录构建并运行：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_ANIMATION=ON -DRENDERER_BUILD_INTERACTION=ON
cmake --build --preset windows-release --parallel 6
.\build\windows\Release\renderer_sprite_demo.exe --threaded
.\build\windows\Release\renderer_sprite_demo.exe --hidden --frames 60 --threaded --oit --self-test --capture build/forest.ppm
```

`RENDERER_BUILD_EXAMPLES=ON` 且两个扩展都启用时，同时构建原三维示例与 `renderer_sprite_demo`。森林示例的完整源码是 [sprite_app.cpp](../examples/development/sprite_app.cpp)，使用共用的 [forest_assets.hpp](../examples/common/forest_assets.hpp) 生成原创演示图集，不需要下载游戏素材。

需要增加精灵数量并比较后端和线程时，运行 `renderer_sprite_benchmark`，详见[精灵性能测试](SPRITE_BENCHMARK.md)。

| 操作 | 行为 |
| --- | --- |
| W/S/A/D | 沿相机水平朝向移动角色，接近障碍时尝试沿轴滑动 |
| Q/E | 旋转相机，Billboard 与四方向动画一起更新 |
| 左键点击树木/石头 | 根据图片 Alpha 选择物体并着色 |
| 左键点击地面 / 右键 | 设置地面移动目标，显示目标圈 |
| 滚轮 | 调整正交视图范围 |
| F / O | 切换 FXAA / OIT |
| Esc | 关闭窗口 |

默认开启 FXAA，`--no-fxaa` 关闭；`--oit` 开启 OIT；`--threaded` 使用专用渲染线程。隐藏运行必须指定正的帧数。`--self-test` 使用固定时间步、自动行走和相机旋转，并检查动画事件、地面投影及场景数量；建议运行至少 24 帧。

示例中的圆形障碍、移动和选择逻辑属于应用代码。当前没有通用物理引擎、导航寻路或完整游戏框架，点击障碍另一侧时角色可能停在障碍前。后续可接导航网格/A*，不需要改变图片组件。

## 1. 世界与图片分开

引擎使用右手坐标、Y 向上，推荐游戏移动在 XZ 平面进行。节点世界原点作为图片锚点；图片的尺寸、裁剪偏移和朝向在视图准备阶段展开为共享四边形。

```text
PlayerGround（世界位置，SortingGroup，业务碰撞范围）
├─ Body（SpriteRenderer，动画更新当前图片）
└─ Hat（SpriteRenderer，sortingOrder = 1）
```

主相机旋转不会修改节点逻辑矩阵；同一份快照可以交给不同视图，每个视图分别计算图片几何。`RenderSnapshot::instanceTransform()` 对精灵返回逻辑世界矩阵，`instanceCount()` 统计网格与精灵，`spriteCount()` 单独统计精灵。

所有示例片段使用 `using namespace renderer;`，假定已有 `assets`、`scene`、`extractor`、`camera`、`window` 和 `engine`。实际可编译程序见森林示例。

## 2. 创建精灵资源

包含 `renderer/sprite.hpp`。资源属于 `AssetStore`，创建后不可变：

```cpp
TextureSampling sampling;
sampling.mipmapped = false;
sampling.addressU = sampling.addressV = TextureAddress::Clamp;
const auto texture = assets.loadTexture("atlas.png", true, sampling);

SpriteDesc desc;
desc.texture = texture;
desc.rect = {0, 0, 64, 96};
desc.pixelsPerUnit = 64;
desc.pivot = {0.5f, 0};
const auto standing = assets.createSprite(desc);

const auto actor = scene.createNode(scene.root(), "Actor");
scene.setComponent(actor, SpriteRenderer{.sprite = standing});
```

此图片宽 1、高 1.5 世界单位。默认 `AlphaMode::Blend`、`unlit=true`，保留图片颜色与柔和边缘。`tint` 为线性颜色，RGB 可大于 1，Alpha 范围 [0,1]。`sizeMultiplier` 为正缩放，`rotation` 为图片平面内弧度，`flipX/flipY` 围绕锚点翻转。

### 裁剪、坐标与图集

`SpriteRect` 和 `trimOffset` 的原点都在左下。`originalSize` 是裁剪前尺寸；两者为 0 时自动取 rect 宽高。`pivot` 基于裁剪前图片计算，保证大小不同的动画帧仍在同一个落地点。

例如原始图片 100×120，只保存从 (20,0) 开始的 60×90 内容：`originalSize={100,120}`、`trimOffset={20,0}`、`pivot={0.5f,0}`。换帧只改变当前 SpriteHandle，不能再把节点按内容中心移动一次。

常用打包工具输出左上原点元数据时，先转换：

```text
rect.y       = textureHeight - top - contentHeight
trimOffset.y = originalHeight - trimTop - contentHeight
```

`loadTexture()` 已将图片转换为左下原点，不要再反转像素或 UV。当前不支持打包时旋转 90° 的图片条目。

可以用 PImpl 的 `SpriteAtlas` 建立命名索引：

```cpp
SpriteDesc a = desc;
SpriteDesc b = desc;
b.rect.x = 64; // 要求图集至少宽 128、高 96。
const std::vector<SpriteAtlasEntry> entries{{"idle", a}, {"walk", b}};
SpriteAtlas atlas(assets, texture, entries);
const auto walking = atlas.sprite("walk");
```

构造时统一指定纹理，名字必须非空且唯一；任何条目失败都回滚本次创建的精灵。图集负责名称索引，析构不销毁精灵；按需调用 `assets.destroySprite(handle)`。`spriteDescription()` 读取规范化描述。

图集不负责离线打包或解析某个工具的专用 JSON。使用者传入像素区域、原始尺寸和裁剪偏移。在线性过滤或 mip 下，需在离线打包时加入足够的边缘外扩与隔离带；未经处理的图集可先关闭 mip。仅设置 Clamp 不能隔离同一图集内部的相邻图片。

### 合批与资源生命周期

同一仓库的精灵共用一个四边形；相同纹理、Alpha 模式、裁剪阈值、unlit 参数共用材质。GPU 每实例保存 UV 矩形、颜色、矩阵与标记，因此换图集帧、改颜色、翻转或缩放都不必重建材质/网格。排序后仅合并相邻兼容项，不能为了合批打乱遮挡关系。

精灵内部保有纹理引用，显式销毁 TextureHandle 后，已有精灵仍可提取。销毁 SpriteHandle 后，下一次提取引用该句柄的场景会失败；已经提取的快照保有资源，不依赖原仓库或场景存活。

## 3. Billboard 模式

| 模式 | 图片方向 | 常见用途 |
| --- | --- | --- |
| `Fixed` | 节点局部 XY 平面，继承世界变换 | 地面标记、水平阴影、固定朝向图片 |
| `Upright` | 世界 Y 竖直，水平方向跟随相机 | 人物、树木、石头 |
| `ViewPlane` | 图片平行相机平面 | 图标、烟雾、特殊美术风格 |

Upright/ViewPlane 保留节点位置和 X/Y 基向量长度，替换其空间朝向；负行列式通过横向镜像保留。推荐使用 flipX/flipY 明确控制美术翻转，使用 `rotation` 控制平面内旋转。Billboard 不保留父节点剪切，Fixed 则使用完整世界矩阵。

俯视极点没有唯一水平朝向，Upright 和方向动画采用世界 +Z 回退。视锥裁剪在视图展开后使用实际图片 AABB。几何退化时跳过绘制。

## 4. 落地点、排序与透明

Blend 精灵按以下字段由先到后排序：

1. `sortingLayer`，较大值后绘制。
2. `sortingOrder`，较大值后绘制。
3. 排序点的相机空间深度，从远到近。
4. 排序对象的稳定节点身份，避免相同深度抖动。
5. 分组内的部件 `sortingOrder`。

没有分组时，排序点是节点世界变换后的 `sortOffset`，默认为脚底原点。相机旋转时深度方向随相机更新，不固定按世界 Z 排序。

```cpp
const auto group = scene.createNode(scene.root(), "Character");
scene.setComponent(group, SortingGroup{});
const auto body = scene.createNode(group, "Body");
scene.setComponent(body, SpriteRenderer{.sprite = standing});
const auto equipment = scene.createNode(group, "Equipment");
SpriteRenderer equipmentRenderer{.sprite = walking};
equipmentRenderer.sortingOrder = 1;
scene.setComponent(equipment, equipmentRenderer);
```

最外层 SortingGroup 的层、顺序、排序点统一用于子树的 Blend 精灵；其内部更深的分组不再另行拆开。部件自身的 sortingOrder 控制组内顺序，自身的 sortingLayer 和 sortOffset 在组内不生效。组内相同顺序的部件保持快照遍历顺序。

角色跳跃时，可让分组节点留在地面，只抬高图片子节点，使遮挡排序仍使用地面位置。

Opaque/Mask 使用真实图片平面深度并写入深度，排序层无法覆盖深度测试。Blend 测试已有场景深度、关闭深度写入。需要明确人物/树木遮挡时建议先使用 Blend；跨度大的建筑、桥梁或相互穿插的纸片仍可能需要拆层，当前没有逐像素精确排序。

### 与 OIT 共存

精灵默认 `SpriteTransparency::Sorted`。常规 SortedAlpha 视图中，精灵与普通 Blend 网格一起排序；普通网格仍以包围盒中心作为深度参考，层和顺序视为 0。

OIT 视图先合成 OIT 物体，再执行 `Ordered Sprites` Pass，绘制默认精灵。这样开启 OIT 不会把人物和树木混成平均颜色。此额外 Pass 只在实际存在可见的默认 Blend 精灵时创建，无额外颜色目标。

烟雾等效果可设置 `SpriteRenderer::transparency = SpriteTransparency::FollowView`，让它随视图进入 OIT。OIT 与 Sorted 队列的关系是固定的“先 OIT、后 Sorted”，不保证烟雾与角色逐层精确交错；如果需要统一前后顺序，使用 SortedAlpha 视图。

### 光照和阴影

`SpriteDesc::unlit=false` 可使用现有场景光照，图片法线来自展开后的平面；当前 SpriteDesc 不提供独立法线贴图。默认关闭投影/接收阴影，可在组件开启。Opaque/Mask 可投影，Blend 不投影。阴影使用当前主相机已经展开的图片几何，不会再按灯光方向旋转，但相机旋转仍会改变这块纸片的几何阴影。

森林示例使用固定在地面的柔和图片阴影。当前没有自动接触阴影、角色遮挡淡出、轮廓高亮、纸片骨骼或专用二维光照系统。

## 5. 方向序列帧动画

包含 `renderer/animation/sprite_animation.hpp`，链接 `renderer::animation`。Clip 使用不可变共享 PImpl，Animator 使用独占 PImpl。Clip 中的精灵句柄必须属于同一仓库，使用期间不能销毁；Clip 不代替仓库拥有资源。

```cpp
SpriteAnimationClipDesc animation;
animation.directions = {{{standing, 0.15, {}}, {walking, 0.15, "footstep"}}};
SpriteAnimationClip clip(animation);
SpriteAnimator animator;
animator.play(clip, true);

animator.advance(1.0 / 60);
animator.selectDirection({0, 0, 1}, scene.worldTransform(camera));
animator.apply(scene, actor);
for (const auto &event : animator.takeEvents()) {
    if (event.name == "footstep") {
        // 在应用层播放脚步音效。
    }
}
```

`directions` 支持 1、4、8 个方向，每方向帧数、每帧持续时间及事件名必须相同，图片可以不同。方向 0 朝向相机水平背向量；随后向相机右侧递增。四方向为朝向相机、向右、背对相机、向左；八方向每隔 45° 一组。

`play()` 重置时间和方向，清空旧事件并产生首帧的可选事件，保留速度且取消暂停。切换待机/行走时调用一次 play，不要每帧重播。方向变化不重置时间，也不重复触发事件。

`advance()` 接收非负秒数，支持速度和暂停；跨多个帧或循环仍按时间收集事件。单次生成加待取事件最多 4096 个，超出时抛出异常且不推进，调用者应及时 takeEvents 并控制时间步。非循环播放停在末帧，`finished()` 返回 true。`apply()` 保留组件的颜色、朝向、排序和翻转设置，仅在图片改变时更新场景版本。

当前提供序列帧与手动动画切换，未实现自动状态机、混合过渡、通用挂点轨道和分层骨骼动画；场景子节点可以先承担静态装备挂接。

## 6. 射线、地面与图片拾取

包含 `renderer/interaction/picking.hpp`，链接 `renderer::interaction`。`SceneQuery` 以不可变快照及视图创建缓存，必须使用正在显示的快照、相机和帧缓冲尺寸。它只依赖核心公共接口，不访问 OpenGL；无需链接 OpenGL 后端。

```cpp
const auto snapshot = extractor.extract(scene, assets);
const RenderView view{.cameraNode = camera};
const auto [width, height] = window.framebufferSize();
const auto [clientWidth, clientHeight] = window.clientSize();
if (width > 0 && height > 0 && clientWidth > 0 && clientHeight > 0) {
    SceneQuery query(snapshot, view, uint32_t(width), uint32_t(height));
    // 鼠标事件使用客户区坐标，必须先换算到实际帧缓冲像素。
    const Vec2 mouse{100, 120};
    const Vec2 pixel{mouse.x * float(width) / float(clientWidth),
                     mouse.y * float(height) / float(clientHeight)};
    if (auto ground = query.groundPoint(pixel)) {
        // ground 是世界 XZ 地面上的移动目标。
    }
    if (auto hit = query.pickSprite(pixel)) {
        if (scene.valid(hit->node)) {
            // hit->node 是图片透明区域过滤后的最前物体。
        }
    }
}
```

`screenRay()` 支持透视和正交投影，射线原点在近裁剪面。`groundPoint()` 默认求交 Y=0，可指定高度；交点在近远裁剪面外时为空。`worldToScreen()` 返回左上原点的像素 XY 与 [0,1] 深度，视锥外为空。

`pickSprite()` 尊重继承可见性、相机层掩码、Billboard、翻转、裁剪偏移、Mask 阈值和 Blend 的排序组。默认跳过有效 Alpha 小于 0.1 的 Blend 像素；Alpha=0 总是跳过。向构造函数传入 `SceneQueryFilter` 可用 excludedNodes 排除阴影、目标圈等，也可使用额外 layerMask；筛选属于交互层，不修改渲染组件。

CPU 使用纹理第 0 级和 magFilter 的最近点/双线性规则检查 Alpha，不模拟实际 GPU mip/LOD/FXAA。Opaque 视为实心，Mask 通过阈值后视为实心；同一 OIT 队列没有确定层叠，拾取最近交点。当前只检查精灵，不检查普通网格的遮挡，也没有空间索引；每次拾取遍历当前缓存精灵。查询保有快照，可在只读线程使用，但返回的节点可能在可变场景中已经删除，操作前检查 valid。

## 7. SDK 与下一阶段

Sprite、图集、Billboard、排序和只读 SpriteViewSnapshot 属于 `renderer::core`；动画和 SceneQuery 分别属于可选的 `renderer::animation`、`renderer::interaction`。头文件和静态库按启用模块安装；两个扩展只依赖核心公开接口，没有增加第三方依赖。开关、目录结构和接入迁移见 [模块说明](MODULES.md)。

安装后，从 SDK 内构建四个开发示例：

```powershell
cmake --install build/windows --config Release --prefix C:/SDK/renderer
cmake -S C:/SDK/renderer/share/renderer/examples/development -B build/development -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build/development --config Release --parallel 6
ctest --test-dir build/development -C Release --output-on-failure
.\build\development\Release\development_sprite.exe --threaded
```

下一阶段可以增加地图分块/瓦片、导航、纸片骨骼和装备轨道、粒子、昼夜与局部光照、UI/中文字体、音频和存档。当前示例只是可交互的渲染验证场景，没有生存、制作、背包或 AI 玩法系统。
