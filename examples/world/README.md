# 模块化图片世界示例

一个可分别使用 OpenGL 和 Vulkan 的交互场景：1024 个地面格子、16 个地图分块、图片树木/石头/角色、批量花草与脚底阴影、圆形障碍、火焰/烟雾/雨/雪、昼夜调色、遮挡淡出、闪白和内描边。

需要 tilemap/particles/environment/presentation，以及 physics2d 或 collision 和至少一个图形后端。启用 physics2d 时自动使用 Box2D；关闭后回退到原轻量碰撞。根工程开启 `RENDERER_BUILD_EXAMPLES` 后自动构建，目标在 VS 的 examples 文件夹。所有素材由源码生成，不需要外部游戏资源。接口说明见[图片世界开发文档](../../docs/WORLD_MODULES.md)。

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON
cmake --build build/windows --config Release --target renderer_world_demo --parallel 6
.\build\windows\Release\renderer_world_demo.exe --backend opengl
.\build\windows\Release\renderer_world_demo.exe --backend vulkan
```

| 输入 | 作用 |
| --- | --- |
| WASD | 在 XZ 平面移动，连续碰撞与滑动 |
| Space | 角色短时闪白 |
| N | 白天/夜晚切换 |
| R | 雨/雪/晴循环 |
| O | 排序 Alpha / Weighted OIT 切换 |
| F | FXAA 开关 |
| C | 屏幕空间接触阴影开关 |
| T | 流式模式中切换脚下瓦片并保存 |
| Esc | 退出 |

默认开启独立渲染线程、FXAA 和接触阴影。`--single-thread` 关闭独立渲染线程；`--threaded` 显式开启。`--backend auto` 默认优先使用已编入的 OpenGL，否则使用 Vulkan，请求未编入的后端会报错。

可复现的隐藏窗口自检与截图：

```powershell
.\build\windows\Release\renderer_world_demo.exe --backend opengl --hidden --frames 60 --self-test --capture build/windows/world-opengl.ppm
.\build\windows\Release\renderer_world_demo.exe --backend vulkan --hidden --frames 60 --self-test --capture build/windows/world-vulkan.ppm
```

`--hidden` 必须指定正数 `--frames`。隐藏或自检模式固定步长为 1/60 秒；自检自动移动、闪白、切换 OIT/昼夜/天气，并检查分块、碰撞、可见实例、粒子及图形错误。它是功能集成示例，性能对比请使用[精灵性能测试](../sprite_benchmark/README.md)。

安装 SDK 后可完全独立构建：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/world -B build/world-sdk -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/world-sdk --config Release --parallel 6
ctest --test-dir build/world-sdk -C Release --output-on-failure
```

图形测试开关必须与 SDK 提供的后端匹配。测试总开关为 `RENDERER_BUILD_TESTS`，示例开关为 `RENDERER_BUILD_EXAMPLES`。应用不需要第三方头文件或 Vulkan SDK；运行仍需要对应驱动和桌面。

接触阴影只读取 Opaque/Mask 深度，Blend 图片树另用水平软影图片；描边是原图片内部的 Alpha 轮廓。碰撞没有导航，基础地图由应用显式加载/卸载，可用 --streaming 启用自动调度，粒子在 CPU 固定步模拟，天气不带玩法规则。

## 自动流式地图

运行时加 `--streaming`，相机跟随玩家，周围区块后台生成并按预算发布；远处区块卸载时同步删除其演示树木和碰撞体。加载半径为 2、卸载半径为 3，每次最多发布/卸载两个块。T 修改脚下瓦片并请求保存。

`--streaming` 的演示保存器只在本进程内保留修改；需要跨运行恢复时加 `--stream-cache build/windows/world-save`，已有文件使用 RTile 格式读取。游戏退出前先完成脏块保存。隐藏自检需至少 60 帧，自动跨区块调度、编辑、保存、卸载和重新加载；不加 --streaming 时仍是原来的固定 1024 格场景。完整接口与边界见[流式加载文档](../../docs/STREAMING.md)。

启用 physics2d 后，玩家采用锁定旋转的动态圆形，场景增加两块可推动物件和触发区域；区块树木创建/移除时同步管理 Box2D 刚体，退出日志显示刚体和事件计数。模块接口见[Box2D 文档](../../docs/PHYSICS2D.md)。
