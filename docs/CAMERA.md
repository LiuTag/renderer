# 相机控制模块

[文档导航](README.md) · [模块边界](MODULES.md) · [SDK 安装](SDK.md) · [CPU 示例](../examples/camera/README.md) · [综合游戏](../examples/game/README.md)

`renderer::camera` 只依赖 core 公共接口，提供 PImpl `CameraController`。它不创建窗口、不订阅键鼠、不持有 Scene 引用。应用提供世界目标、输入增量和 dt，控制器计算姿态并可写回相机节点。

## 模式和坐标

| 模式 | 初始数据与行为 |
| --- | --- |
| Orbit | 根据 target、yaw、pitch、distance 围绕观察点旋转，支持平移/缩放 |
| Follow | 由 followOffset 推导初始方向与距离，观察点为 target+targetOffset；目标变化时平滑跟随，也支持轨道旋转/平移 |
| Free | 从 position、yaw、pitch 自由移动，groundMovement 可将前进限制到 XZ |

右手系，世界 +Y 向上，相机局部 -Z 为前。yaw=0 时轨道眼睛在目标 +Z 一侧，正 yaw 把眼睛移向 +X；Free 相同姿态下视线转向 -X。pitch 正值表示俯视，应用按操作习惯选择鼠标增量的符号。

## 构建与接入

默认 `RENDERER_BUILD_CAMERA=ON`；关闭后不构建相机库、示例或测试，不安装专用头文件与库。通用 SDK 安装规则仍附带示例源码，缺少组件时无法构建相机示例。其他模块不反向依赖它，game 综合示例需要 camera，因此缺失时根工程会跳过 game。

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS camera opengl)
target_link_libraries(my_app PRIVATE renderer::camera renderer::opengl)
```

CPU 工具只需请求 camera。SDK 安装 `renderer/camera/camera_controller.hpp` 和 renderer_camera 静态库，无额外第三方依赖。VS 中库属于 modules，测试属于 tests，示例属于 examples。

## 跟随角色的正交相机

```cpp
#include <renderer/camera/camera_controller.hpp>
using namespace renderer;
CameraControllerConfig config;
config.mode = CameraControlMode::Follow;
config.projection = Camera::Projection::Orthographic;
config.followOffset = {0, 10, 15};
config.targetOffset = {0, 1, 0};
config.orthographicHeight = 14;
config.minOrthographicHeight = 6;
config.maxOrthographicHeight = 24;
config.smoothing = 8;
CameraController controller(config);

// camera 是 scene 中已有 Camera 组件的非根节点。
controller.setTarget(playerWorldPosition);
controller.update(dt, {.rotation = {yawDelta, pitchDelta}, .zoom = -wheelDelta});
controller.apply(scene, camera);
auto cameraWorld = controller.state().world;
// 在写回之后提取本帧快照，RenderView.cameraNode 指向 camera。
```

传送时调用 `setTarget(newPosition, true)` 立即跟到新位置；默认 false 等待 update。Free 保存 target 但不以它驱动运动。

## 输入与缩放单位

| 输入 | 单位和计时 |
| --- | --- |
| rotation | yaw/pitch 弧度增量，已包含灵敏度，不再乘 dt |
| pan | 本次旋转后的目标相机右/上方向世界位移，Orbit/Follow 累积，持续 setTarget 不清除此偏移 |
| movement | Free 右/上/前方向，每分量 [-1,1]；长度大于 1 时归一化，再乘 moveSpeed、加速倍率和 dt |
| zoom | 正值拉远、负值拉近，指数缩放并限幅；滚轮通常映射为 -yOffset |
| boost | Free 是否使用 boostMultiplier |

正交缩放只改高度；透视 Orbit/Follow 改距离，透视 Free 改 verticalFov。缩放在 log 空间限幅，极大有限输入不会先 exp 溢出。初始角度和镜头值限制到有效区间；负尺寸、无效模式、非有限数据和跨俯仰极点的限制会拒绝。

按键持续旋转由应用生成 `radiansPerSecond*dt`；鼠标位移直接换算弧度。Free 只输入方向，不重复乘 dt。groundMovement=false 时上下使用相机局部轴，true 时使用世界 Y。

## 平滑、模式和投影

`smoothing=0` 立即应用；正值使用 `1-exp(-smoothing*dt)` 插值观察点、角度和镜头。固定目标下效果与时间分段基本无关，yaw 沿最短路径插值，输出由正交基重建而非直接插值矩阵。

dt 必须有限且在 [0,4]。update(0) 不推进移动/平滑，立即模式仍处理设备增量。setMode 保留当前视点和朝向并清除待完成平滑；切到 Follow 时以当前眼睛到观察点的偏移继续。setProjection 切换投影类型并保留数值，reset 完全重新初始化。无效 reset/update 不改变原状态。

```cpp
controller.setMode(CameraControlMode::Free);
controller.update(dt, {.rotation = mouseRadians, .movement = {right, up, forward}, .boost = fast});
controller.apply(scene, camera);
```

同一控制器不要并发 update 和 state/apply；state 返回独立副本，可由应用发布到其他线程。控制器没有内部线程。

## Scene 契约与范围

state().world 是相机到世界的矩阵。apply 用 `local=inverse(parentWorld)*world` 处理父节点，支持旋转、非均匀缩放和剪切，拒绝奇异父矩阵。

控制器拥有 projection、verticalFov 和 orthographicHeight；近远面、aspectRatio、cullingMask 保留。写 Scene 前先验证投影与既有近远面兼容，失败不写入姿态或组件。相机必须属于给定 Scene，含 Camera 组件且不是根节点。

模块固定 Y 向上，没有 roll、自动相机碰撞/避障、死区、震动或镜头轨道。物理查询、目标选择、屏幕边缘滚动和输入绑定由应用组合。

综合游戏使用 Follow 平滑跟随、Q/Z 轨道旋转、滚轮正交缩放和传送立即更新；上层不再手写 lookAt/父矩阵处理。旧 world/森林保留原写法作为对照。

`renderer.camera` 验证坐标、缩放/角度限幅、平滑、传送、连续切换、父缩放/剪切、镜头保留、失败原子性及移动生命周期。`renderer.camera.example` 仅使用公开 SDK，无需窗口。
