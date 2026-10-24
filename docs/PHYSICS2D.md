# Box2D 二维物理模块

[文档导航](README.md) · [SDK 安装](SDK.md) · [流式世界](STREAMING.md) · [无窗口示例](../examples/physics2d/README.md)

`renderer::physics2d` 使用固定版本 Box2D 3.2.0。公共服务类 Physics2D 采用 PImpl，头文件不包含 Box2D 类型。模块只依赖 core 公共接口，场景和两种图形后端不反向依赖它；无需窗口即可运行。

## 构建与 SDK

模块默认开启，设置 `RENDERER_BUILD_PHYSICS2D=OFF` 可关闭，同时移除对应库、头文件、示例和测试。第一次源码配置通过 CMake 下载并校验固定 SHA256；离线构建可指定 `-DFETCHCONTENT_SOURCE_DIR_BOX2D=C:/deps/box2d-3.2.0`。源码构建需要 C17/C++20 编译器。

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS physics2d)
target_link_libraries(my_game PRIVATE renderer::physics2d)
```

安装包包含 renderer_physics2d 和私有 Box2D 静态库，Debug/Release 分目录存放；依赖自动传递，应用无需下载 Box2D、包含其头文件或手动列出依赖库。上游独立安装规则被排除，SDK 不安装 box2d 开发头文件、pkg-config 或第二套 CMake 包。MIT 许可保存在 third_party/licenses/box2d.txt，并随 SDK 安装。

## 创建俯视游戏世界

```cpp
#include <renderer/physics2d/physics2d.hpp>
using namespace renderer;
Physics2D physics({.workerCount = 2}); // 默认零重力，固定 1/60 秒，4 个求解子步

Body2DDesc tree;
tree.type = Body2DType::Static;
tree.position = {3, 2}; // 对应三维世界 X=3、Z=2
tree.userData = 1001;
tree.shapes[0].radius = .5f;
auto treeBody = physics.createBody(tree);

Body2DDesc player;
player.lockRotation = true;
player.shapes[0].radius = .25f;
player.shapes[0].friction = 0;
auto playerBody = physics.createBody(player);

// 每帧按输入设置目标速度；推进物理后读取最终位置。
physics.setVelocity(playerBody, {2, 0});
auto step = physics.advance(dt);
auto position = physics.state(playerBody).position;
```

Static 用于树木、墙壁和地面障碍；Dynamic 用于角色、可推动物件；Kinematic 按速度移动但不受力。Box2D 的运动学刚体不与静态/其他运动学刚体产生普通碰撞响应，因此角色示例采用锁定旋转的 Dynamic 圆形，而不是把角色设为 Kinematic。默认零重力适合俯视游戏；非零重力作用在物理平面，内置场景同步仍映射到世界 XZ。

一个刚体可有 0～64 个形状，用于复合碰撞和附属触发器。Circle 使用 radius/offset，Box 使用 halfExtents/offset/angle，Capsule 使用 pointA/pointB/radius，再应用 offset/angle；Polygon 接收 3～8 个点并计算凸包，圆角为零。退化凸包、零半径、重复导致的无效几何在创建前拒绝。形状密度参与自动质量计算；sensor 仍会按密度贡献质量，需要无质量触发形状时设 density=0。

`categoryBits`/`maskBits` 是 64 位双向过滤；两形状必须同时通过双方的 category/mask 才相互作用。`sensor=true` 只报告重叠，不提供阻挡。`bullet=true` 开启更积极的连续碰撞，但不保证所有高速物体之间的精确命中时刻，高速弹丸可使用射线查询。

## 固定步与受力

`advance(seconds)` 接受 0～4 秒，保留小于一个固定步的余数；最多推进 maxStepsPerAdvance 个固定步。超过 `fixedTimeStep * maxStepsPerAdvance` 的积压显式计入 droppedSeconds，避免长暂停引起无限追帧。程序应据此处理暂停/过载策略。

`step()` 精确推进一个固定步，不消费 advance 的时间余数；外部已有固定步循环时用 step，不要再用 advance 重复推进。fixedTimeStep 范围为 0.001～0.1 秒，subSteps 为 1～16，maxStepsPerAdvance 为 1～64。

```cpp
physics.applyImpulse(playerBody, {3, 0}); // 受击、爆炸或瞬时推力
physics.applyForce(playerBody, {10, 0});  // 作用于下一次固定步
physics.step();
```

空施力点表示质心，传入平面世界坐标则可产生旋转。持续作用的力应在每个固定步之前施加；advance 连续补多个固定步时，一次 applyForce 不会自动重复应用到所有步。setVelocity 适合受控角色；setTransform 是显式瞬移，会重置插值前态，不应逐帧拿它替代动力学移动。

对外按单写线程使用；workerCount=1 为串行，2～16 使用 Box2D 原生任务系统。advance/step 返回时内部任务已完成，之后才可以消费事件和修改场景。它与 OpenGL/Vulkan 的渲染线程独立，不允许同时从多个外部线程修改同一个 Physics2D。

## 接触、触发与身份

```cpp
for (const auto &event : physics.takeEvents()) {
    if (event.kind == Physics2DEventKind::SensorBegin) {
        // a 是传感器，b 是进入者；userDataA/B 是应用身份。
    }
    if (event.kind == Physics2DEventKind::Hit) {
        // 只有对应形状启用 hitEvents 时才报告；读取 point/normal/approachSpeed。
    }
}
```

支持 ContactBegin/ContactEnd、SensorBegin/SensorEnd 和 Hit。事件在物理步之后复制为值，跨多个固定步累积；应每帧 takeEvents，避免长期保留事件。Contact/Sensor 事件只提供双方身份和形状索引，命中点/法线/接近速度由 Hit 提供。

Body2DHandle、Joint2DHandle 带 owner/index/generation；删除后复用槽位不会使旧句柄生效，移动 Physics2D 包装对象后句柄保持有效。删除刚体也会删除其关节。Box2D 可以在下一步报告已删除形状的退出事件，模块保留其应用身份供事件使用；处理事件后操作刚体前仍需 valid(handle)。不要把运行时句柄写入存档，使用 userData 或应用对象 ID。

## 查询与距离关节

```cpp
auto hit = physics.rayCast({0, 0}, {10, 0}, {.maskBits = 1, .ignore = playerBody});
auto nearby = physics.overlapCircle(position, 2, {.includeSensors = true});
auto rope = physics.createDistanceJoint({.a = treeBody, .b = playerBody, .length = 2});
```

射线返回最近的命中、沿位移的 fraction、命中点/法线及形状索引；默认跳过传感器。圆形查询做实际几何重叠并按刚体去重。查询发生在物理平面，不依赖相机，也不会检查图片透明像素；屏幕 Alpha 拾取仍使用 interaction 模块。

距离关节使用两个刚体各自的局部锚点，可启用 spring、hertz、dampingRatio 及长度限制。遵循 Box2D 的语义：spring=false 时为刚性定长约束，长度限制由刚性长度覆盖；启用 spring 后才使用 minLength/maxLength 限制。当前包装没有开放转轴、轮轴等其他关节类型、链条形状和自定义求解回调。

## 同步场景

```cpp
physics.bindNode(playerBody, scene, playerRoot); // 默认不把物理角度用于图片朝向
physics.advance(dt);
physics.synchronize(scene, false);              // 使用当前物理状态
auto snapshot = extractor.extract(scene, assets, batches);
```

绑定保留初始视觉偏移、世界 Y 高度、缩放和朝向。平面物理坐标 `(x,y)` 映射为世界 `(x,0,z=y)`，物理逆时针角度映射为负的世界 Y 旋转；bindNode 最后一个参数为 true 时同步该旋转。Billboard 和方向动画可独立更新子节点，避免让平面物理旋转改变人物纸片朝向。

支持带父变换的节点，按父子层级顺序同步。删除节点后自动解除绑定；删除节点不会自动删除刚体，应用应管理二者生命周期。synchronize(scene,true) 使用上一固定步到当前步的余数插值，会引入一个固定步的视觉滞后；显式 step 后使用当前状态。该模块只从物理向 Scene 同步，节点变换不会反向驱动物理。

## 流式世界

world 示例在 physics2d 存在时自动采用 Box2D：静态树木与石头作为障碍，玩家为动态圆形，两块可推动图片物件绑定 Scene，初始传感器用于验证触发事件。流式区块 Loaded/Unloaded 事件负责创建/销毁对应刚体。关闭 physics2d 后仍可使用原 collision 模块运行该示例；两种求解器不会同时驱动同一物体。

流式 payload 应保存应用描述或状态，例如物件身份、平面位置、速度与形状参数，重载后创建新句柄。离开区块前的动态物件状态采集、跨区块归属迁移由应用负责，模块不会自动扫描整个 Scene 或为移动物件转移存档归属。

## 数值范围与边界

建议使用米/秒/千克，物件尺寸保持合理范围。接口校验有限数值：位置/查询位移不超过 ±1e6；局部几何坐标、半径、半尺寸最大 1e4，半径/半尺寸至少 .001；线速度/角速度最大绝对值 1e5；力/冲量最大绝对分量 1e12。密度和摩擦在 [0,1e4]，弹性在 [0,1]，重力倍率在 [-1000,1000]。这些上限避免无效输入直接触发原生断言，不表示极端尺度能保持同样精度。

物理使用单精度平面坐标，图片 Y 高度不参与碰撞；有桥上桥下、真实三维坡地/台阶需求时仍需要三维物理方案。固定步测试覆盖 1/2 个原生工作线程，不承诺跨平台的位级确定性。测试命令和实际验证见[验证记录](VALIDATION.md)。
