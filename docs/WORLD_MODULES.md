# 图片世界：地图、碰撞、粒子与环境表现

[文档导航](README.md) · [模块边界](MODULES.md) · [Sprite 基础](SPRITES.md) · [集成示例](../examples/world/README.md)

这一轮在已有 Sprite、图集、Billboard、动画、排序、拾取和局部灯光之上，增加了批量精灵、瓦片分块、连续圆形碰撞、图片粒子、昼夜天气、遮挡淡出、闪白、内描边和接触阴影。OpenGL 与 Vulkan 使用同一组公共接口和 Shader。它们是可组合的基础能力，游戏的伤害、AI、导航和存档由应用实现。

## 核心与可选模块

| 所在位置 | 能力 | 公共头文件 / 目标 |
| --- | --- | --- |
| core | 无逐粒子节点的不可变批次、裁剪、排序与实例合批 | `renderer/sprite_batch.hpp` / `renderer::core` |
| core | 精灵颜色覆盖、Alpha 轮廓内描边 | `renderer/sprite.hpp` |
| core | 线性调色、屏幕空间接触阴影 | `renderer/renderer.hpp` |
| tilemap | 稀疏 XZ 地图、脏块缓存、卸载 | `renderer/tilemap/tilemap.hpp` / `renderer::tilemap` |
| collision | 圆/AABB 查询、连续圆形移动、切向滑动 | `renderer/collision/collision.hpp` / `renderer::collision` |
| particles | CPU 固定步模拟、颜色/尺寸曲线、火/烟/雨/雪预设 | `renderer/particles/particles.hpp` / `renderer::particles` |
| environment | 昼夜时钟、太阳/环境光/调色、天气强度过渡 | `renderer/environment/environment.hpp` / `renderer::environment` |
| presentation | 相机到目标之间的图片淡出、短时颜色覆盖 | `renderer/presentation/presentation.hpp` / `renderer::presentation` |

五个新模块均只依赖 core 的公共接口，没有互相链接，也不包含引擎私有头文件。服务类使用 PImpl。各模块和 Scene、AssetStore、RenderExtractor 一样按单写线程使用；批次和快照发布后可共享给渲染线程。

## SDK 接入

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS
    opengl tilemap collision particles environment presentation)
target_link_libraries(my_game PRIVATE renderer::opengl
    renderer::tilemap renderer::collision renderer::particles
    renderer::environment renderer::presentation)
```

使用 Vulkan 时把 `opengl` 替换为 `vulkan`，并以 `GraphicsApi::Vulkan` 创建窗口及调用 `Renderer::createVulkan`。地图和特效代码无需改动。新模块默认启用，各自可用 `RENDERER_BUILD_TILEMAP`、`RENDERER_BUILD_COLLISION`、`RENDERER_BUILD_PARTICLES`、`RENDERER_BUILD_ENVIRONMENT`、`RENDERER_BUILD_PRESENTATION` 关闭；SDK 只安装启用模块的头文件和静态库。新增公开值类型字段后，使用旧 SDK 的应用应重新编译，不能混用旧库。

## 批量精灵

普通角色继续使用 Scene 节点。静态装饰、地图格子、粒子可以通过批次输入，避免逐个维护场景树节点：

```cpp
#include <renderer/renderer.hpp>
using namespace renderer;

// assets、scene、extractor 和 flowerSprite 由应用创建。
std::vector<BatchSprite> flowers;
for (int i = 0; i < 1000; ++i) {
    SpriteRenderer sprite;
    sprite.sprite = flowerSprite;
    sprite.billboard = BillboardMode::Upright;
    flowers.push_back({sprite, Mat4::translation({float(i % 50), 0, float(i / 50)}),
                       uint64_t(i + 1)});
}
SpriteBatch batch;
batch.replace(assets, flowers);
auto snapshot = extractor.extract(scene, assets, std::span{&batch, 1});
```

`replace` 复制输入并解析资源，成功后一次发布新数据；失败保留旧批次。批次复制与快照提取共享已有数据，不会重新复制整个精灵数组。旧快照保留旧版本和资源，即使原批次、图片句柄、仓库被删除，也不影响它。

每批次有世界包围盒：Fixed 图片使用实际几何，Billboard 使用包含任意相机朝向的保守界限。关闭方向光阴影时，可先整批裁剪，再展开可见批次；开启阴影时仍保留相机外可能投影到画面的实例。展开后逐实例裁剪、排序和合批继续使用现有流程。一个批次不保证只有一次 draw call，材质、图集纹理及排序约束仍会拆批。

`BatchSprite::id` 是应用身份，`SpriteViewItem::instanceId` 和 `SpriteHit::instanceId` 返回该值，`node` 为空。身份只由应用保证唯一；跨批次可额外使用不同 layerMask 或建立映射，不能靠空 node 区分实例。批次不继承 Scene 父节点、可见性或 SortingGroup，必须直接提供世界变换和每实例排序参数。

## 瓦片地图

```cpp
#include <renderer/tilemap/tilemap.hpp>
TileMap map({.tileSize = 1, .chunkSize = 16, .origin = {-16, 0, -16}});
map.set(0, 0, grassSprite);
map.set(-1, 0, roadSprite); // 负坐标正确归入负块
auto batches = map.batches(assets);
auto snapshot = extractor.extract(scene, assets, batches);
map.set(0, 0, {});         // 删除一个格子
map.unloadChunk(-1, 0);    // 参数是块坐标，不是格子坐标
```

地图平面是 XZ，原点是格子 `(0,0)` 的角点；图片内容缩放为一个正方形格子，并补偿锚点及裁剪偏移。每个非空块缓存一个 SpriteBatch，仅 `set` 修改过的块在下一次 `batches` 时重新发布。图块坐标通过向下取整划分，例如块边长 16 时，格子 -1 属于块 -1。地图批次的排序层为 -100；Alpha 模式来自资源，连续地面建议使用 Opaque。

基础 TileMap 由应用决定何时 `set` / `unloadChunk`；新增 TileStreamer 可自动按玩家位置后台读取、预算发布与保存后卸载，详见[流式加载](STREAMING.md)。旧快照仍保留已卸载数据直到不再使用。地表花草可另建 Upright 批次，避免把竖直物体铺成水平格子。瓦片身份编码为 `(uint64_t(uint32_t(x)) << 32) | uint32_t(z)`，所以格子 `(0,0)` 的身份可以为 0。

## 连续碰撞与移动

```cpp
#include <renderer/collision/collision.hpp>
CollisionWorld collision(2); // 空间网格边长，单位与场景一致
auto tree = collision.add({.center = {2, 1}, .radius = .6f,
                            .layerMask = 1, .userData = 100});
auto wall = collision.add({.shape = ColliderShape::Box,
                            .center = {-2, 0}, .halfExtents = {.5f, 3}});
Vec2 position{0, 0}; // Vec2 的 y 表示世界 Z
auto move = collision.moveCircle(position, .25f, {3, 0}, 1);
position = move.position;
scene.setLocalTransform(playerRoot, Mat4::translation({position.x, 0, position.y}));
```

宽阶段使用空间网格，圆对圆连续扫掠可防止大位移穿过圆形障碍，接触后把剩余位移投影到切向，并尝试恢复初始穿透。圆对 AABB 使用保守的扩张矩形，转角会略提前接触。`remaining` 可能在接触迭代耗尽后非零，`contacts` 可包含重复障碍；默认迭代 6 次，允许 1～16 次。

`moveCircle` 是查询，不会自动更新世界中的自身障碍；多个动态角色需要由应用管理顺序、传入 ignore 自身并更新 ColliderDesc。句柄有 owner/index/generation，删除后复用槽位不会使旧句柄重新有效。碰撞层独立于 SpriteRenderer 层，应用按业务映射。单次障碍或查询最多覆盖 65,536 个网格格子，过大请求明确抛出异常。

collision 模块不计算刚体质量、旋转或多边形动力学，这些现由独立的 [physics2d 模块](PHYSICS2D.md)提供。导航求路仍由应用负责。碰到障碍只会停止/滑动，不能自动绕树走到远端。

## 图片粒子

```cpp
#include <renderer/particles/particles.hpp>
auto desc = particlePreset(ParticlePreset::Fire, glowSprite);
desc.origin = {1, .1f, 0};
desc.capacity = 1024;
ParticleSystem fire(desc);
fire.advance(1.0 / 60);
auto particles = fire.batch(assets);
batches.push_back(particles);
```

系统在 CPU 上用固定 1/60 秒步长推进，累积余数；`advance` 接受 0～4 秒，长暂停应由应用控制恢复策略。固定种子与相同参数/步序在本实现中可复现，不承诺跨平台位级一致。容量为 1～1,000,000，发射速率为每秒 0～100,000；超出容量的发射丢弃，不积压。`setEmitting(false)` 只停新发射，已有粒子继续老化；`clear` 清空存活粒子和时间余数。

位置、初速度和加速度使用世界单位；`startSize/endSize` 是图片原始世界尺寸的倍数，颜色和尺寸按年龄线性插值。Fire/Smoke/Rain/Snow 只是可修改参数，不加载图片，也不自动制造局部灯光。用细长雨滴图片和柔和烟雾图片可替换示例的共用光点。极端参数造成非有限运动状态时回收该粒子。

`batch` 每次发布当前状态，已有批次不受后续模拟影响。粒子默认 `FollowView`，可随视图使用排序 Alpha 或 Weighted OIT；需要固定层叠的角色仍用 Sorted。当前没有 GPU 粒子模拟、粒子碰撞、独立加色混合或曲线编辑器。

## 昼夜与天气

```cpp
#include <renderer/environment/environment.hpp>
EnvironmentController environment({.dayLength = 120, .hour = 14});
environment.setWeather(Weather::Rain);
environment.advance(dt);
environment.apply(scene, sunNode, view);
auto state = environment.state();
rain.setRate(state.weather == Weather::Rain ? 300 * state.precipitation : 0);
```

`dayLength` 是现实中一整天的秒数，hour 自动绕回 `[0,24)`。`apply` 更新方向光、太阳方向、环境光和 `view.colorMultiply`，支持太阳有可逆父变换；它会接管这些参数。昼夜调色也影响 unlit Sprite，真实局部光照仍要求对应资源启用受光。方向光阴影由 `view.shadows` 决定。

天气有 Clear/Rain/Snow，降水强度按 `weatherTransition` 线性过渡。环境不依赖 particles，应用自行把强度映射到发射速率，把 `wind` 映射到自己的表现逻辑。切换 Rain/Snow 时 weather 立即变更，已有旧粒子继续消亡；这里只平滑强度，不承诺两种降水类型自动交叉淡变。天气不会生成伤害、寒冷或 AI 规则。

## 淡出、闪白与描边

```cpp
#include <renderer/presentation/presentation.hpp>
SpritePresentation presentation({.occluderMask = 2,
                                  .excludedNodes = {playerBody, playerHat}});
presentation.flash(playerBody, {1, 1, 1, 1}, .15f);
auto snapshot = extractor.extract(scene, assets, batches);
presentation.update(scene, snapshot, view, width, height, playerHeadWorld, dt);
snapshot = extractor.extract(scene, assets, batches); // 提交更新后的表现
renderer.render(snapshot, view);
// 停用控制器前恢复它拥有的 Alpha 和覆盖色。
presentation.reset(scene);
```

淡出使用相机朝目标的射线与当前 Billboard 平面求交，并检查原图 Alpha；透明像素不会遮挡目标。只修改 Blend 的 Scene 精灵，批次、Mask/Opaque、网格不参加自动淡出。默认淡出到原 Alpha 的 25%，过渡 0.2 秒。它基于射线，不是整个人物轮廓的可见面积分析。

控制器激活期间拥有节点的 tint.w 和 colorOverlay，结束/取消会恢复原值。应用不要同时改写这些字段，也不要由两个控制器竞争同一节点；析构无法访问 Scene，停用前需调用 `reset(scene)`。flash 是应用触发的表现，不处理受击或无敌规则。

内描边不需要控制器：

```cpp
auto sprite = *scene.component<SpriteRenderer>(playerBody);
sprite.outlineColor = {1, .85f, .15f, 1};
sprite.outlineWidth = 1.5f; // 原图纹素，范围 [0,16]，0 关闭
scene.setComponent(playerBody, sprite);
```

核心在原始 UV 子区域内采样 Alpha 邻域，在图片内部混合轮廓色，不扩张四边形，也不提供穿墙描边。图集区域外视为透明，避免邻近图块污染。`colorOverlay.rgb` 是线性覆盖色，w 为混合权重，保留原图 Alpha；可直接用于受击变白。

## 接触阴影与调色

```cpp
view.contactShadows = true;
view.contactShadowStrength = .6f;  // [0,1]
view.contactShadowDistance = 1;   // 世界空间射线距离，必须为正
view.contactShadowThickness = .1f;// 深度命中容差，必须为正
view.contactShadowSteps = 12;     // [1,32]
view.colorMultiply = {1, .95f, .9f};
view.colorAdd = {0, 0, 0};
view.saturation = .9f;            // [0,4]
```

接触阴影在 ToneMap 阶段重建世界坐标，沿主方向光向光源采样已有场景深度。只使用屏幕内 Opaque/Mask 深度，Blend 既不写入该深度，也不能作为接触阴影遮挡者；画面外遮挡、厚度/步数不足会造成缺失或伪影。默认关闭，可与方向光阴影、OIT、FXAA 组合使用。图片树的稳定脚底软影可继续使用水平半透明阴影图片，示例同时展示两种方式。

它不增加独立全屏 Pass，但开启后增加深度采样成本。遮挡强度作用于已合成的 HDR 颜色，之后执行曝光/ToneMap，再在线性空间做乘加和饱和度处理，最后 FXAA（启用时）及 sRGB 输出。调色默认值保持原画面。

## 一帧的调用次序

1. 主线程 `pollEvents`，更新输入、角色位置和碰撞障碍。
2. 动画更新当前图片；环境更新太阳和调色；推进粒子模拟。
3. 取得地图缓存批次、装饰批次和当前粒子批次。
4. 提取几何快照，运行遮挡淡出/闪白，再提取用于绘制的最终快照。
5. `render` 或 `submit`：Billboard 展开、裁剪、排序、实例合批，执行可选方向光阴影、不透明/Mask、透明/OIT、ToneMap/接触阴影/调色、可选 FXAA。
6. `Renderer::present`。异步模式用不可变快照与下一帧逻辑重叠，GPU 操作仍由渲染线程执行。

完整可运行代码在 [world/main.cpp](../examples/world/main.cpp)，实际验证项目及命令见[验证说明](VALIDATION.md)。
