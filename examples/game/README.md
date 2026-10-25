# 森林营地：完整应用示例

[示例总览](../README.md) · [模块边界](../../docs/MODULES.md) · [音频](../../docs/AUDIO.md) · [离屏渲染](../../docs/OFFSCREEN.md)

`renderer_game_demo` 把当前引擎的应用层能力组合为一个可操作的森林营地。它使用十二个可选模块和一个图形后端，展示游戏逻辑如何同时驱动画面和声音，玩法代码全部位于示例中。

界面布局位于 [game.rml](game.rml)，复用 [UI 示例皮肤](../ui/theme.rcss)。HUD 显示采集状态，按钮实际控制暂停/攻击/天气，音乐/音量及 FXAA/OIT 控件同步业务状态。输入先经过 UI，空白区域继续操作世界；文字、图片和控件随最终 PNG 一起保存，接口和限制见[UI 文档](../../docs/UI.md)。

## 构建与运行

从源码根目录执行；十二个扩展默认开启，已有缓存中关闭过的模块需重新开启对应 `RENDERER_BUILD_*` 开关：

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON -DRENDERER_BUILD_EXAMPLES=ON
cmake --build build/windows --config Release --target renderer_game_demo --parallel 6
.\build\windows\Release\renderer_game_demo.exe --backend vulkan
.\build\windows\Release\renderer_game_demo.exe --backend opengl
```

默认开启独立渲染线程、FXAA、方向光阴影、接触阴影和 ACES。图集、地图、音乐、音效和导入用模型全部由程序生成，无需下载素材。默认通过真实音频设备低音量播放；设备初始化失败会打印原因并回退到离线混音。

## 操作

| 输入 | 行为 |
| --- | --- |
| W/A/S/D、Q/Z、滚轮 | 沿世界 XZ 移动、绕角色旋转相机、调整正交视野 |
| 左键、右键 | 按图片 Alpha 选中树石并描边；空地/右键设置移动目标和地面标记 |
| E、空格 | 采集附近选中物体并保存、受击闪白/爆发粒子/音效 |
| N、R | 昼夜切换、晴/雨/雪，联动音乐与环境声 |
| O、F | 排序透明/加权 OIT、FXAA |
| H、C、G | 方向光阴影、接触阴影、暖色调色 |
| M、B、P | 音乐静音、手动切换配乐、暂停模拟但保持音频播放 |
| T、K、F12 | 修改脚下瓦片、请求保存、PNG 截图 |
| F11、Alt+Enter、Esc | 普通/无边框切换、普通/独占全屏切换、退出并保存 |

点击移动采用直接朝目标移动与物理阻挡，没有自动寻路。窗口标题显示时刻、可见实例和本次采集数量。

## 组合的功能

| 模块或机制 | 实际用途 |
| --- | --- |
| Scene / AssetStore / Model | 角色部件层级、资源复用、程序 OBJ 的真实导入、金属 PBR 球体；`--model` 可替换导入模型 |
| Sprite / Atlas / Batch | 森林图集、锚点、三种 Billboard、脚底/部件排序、SortingGroup；地图、装饰和粒子批次 |
| animation / interaction | 四方向待机/行走、脚步事件；Alpha 拾取、投影与地面求交 |
| camera | Follow 平滑跟随、Q/Z 轨道旋转、滚轮限幅缩放和传送重置，上层只做输入映射 |
| ui | HUD、采集计数、暂停/攻击/天气按钮、音乐/主音量、FXAA/OIT；先捕获输入再驱动世界 |
| text | 随角色移动的像素字号名字牌、火堆世界文字，以及攻击时上浮/淡出的无深度伤害数字 |
| physics2d | 两个内部工作线程、固定步、推箱子、四种形状、传感器/接触事件、距离关节及 Scene 同步 |
| collision | 为附近树石交互提供轻量圆形查询；角色运动由 Box2D 负责 |
| tilemap | 8×8 区块、后台读写、预算发布/滞回卸载、瓦片/采集状态保存、物件生命周期 |
| particles / environment | 火烟雨雪和受击爆发；太阳、环境光、昼夜调色和天气过渡 |
| presentation | 遮挡角色的树木淡出、闪白恢复；核心描边用于选中提示 |
| audio | 流式音乐与交叉淡变、脚步/碰撞/交互声、火堆空间声、随区块驻留的环境声、雨雪声音、分组音量和结束事件 |
| 共用渲染管线 | 双后端、不透明/Mask/排序透明/OIT、方向光/点光源、阴影、HDR/ACES、调色、FXAA 和统计 |
| 平台与输出 | 键鼠/焦点订阅、DPI 换算、全屏、窗口/离屏、PNG 及实际离线混音 WAV |

距离关节通过地面细绳连接运动石头与固定点。火堆粒子、灯光、传感器和声音使用同一个应用位置。监听器跟随玩家，相机缩放不改变听觉距离。

低层 RHI/RenderGraph、Compute 和间接命令的专用用法由 `examples/development` 演示。本示例集中展示应用层组合；没有新增三维蒙皮、自动导航、任务或 UI 字体系统。

## 代码与更新顺序

- `main.cpp`：命令行、Unicode 路径和应用生命周期。
- `application.hpp/.cpp`：输入、世界/物件、模块调度、渲染和自动验证。
- `audio_system.hpp/.cpp`：音频资源缓存、事件映射、空间声源、流式音乐、混音/WAV。

一帧为：输入 → Box2D/场景同步 → 动画事件/脚步声 → 相机/环境/粒子 → 流式发布/物件生命周期 → 音频控制 → 快照 → 遮挡淡出 → 最终快照 → 渲染/呈现。

音频设备按独立采样时钟混音；离线模式按本帧现实时间生成采样。P 暂停物理、动画、粒子和环境，音频仍继续。渲染线程开关不改变音频线程或 Box2D 内部工作线程。流式回调只处理文件和纯数据，场景、物理及播放控制集中在应用线程。

## 缓存、截图与持久化

`--cache` 默认为工作目录下 `build/game-demo`。使用可写的专用目录，并保证同一路径只有一个实例写入。

| 路径 | 内容 |
| --- | --- |
| `audio/generated-day.wav`、`generated-night.wav` | 首次运行生成的演示配乐，按 Stream 模式播放 |
| `chunks/*.rtile` | 瓦片及物件采集状态，下次启动继续读取 |
| `generated-statue.obj` | 通过 Model 导入器加载的程序模型 |
| `last-frame.png`、`captures/frame-*.png` | 退出画面、F12 截图 |
| `session.json` | 实际渲染、物理、交互、流式和音频计数 |

`--capture` 指定退出 PNG；`--audio-capture` 将实际离线混音写为 48 kHz、16-bit、双声道 WAV，并自动使用离线音频。

## 无窗口验证

```powershell
.\build\windows\Release\renderer_game_demo.exe --backend vulkan --offscreen `
  --frames 240 --self-test --cache build/windows/game-preview `
  --capture build/windows/game-preview.png --audio-capture build/windows/game-preview.wav
```

`--self-test` 至少 240 帧，以固定 1/60 秒自动推箱子、触发动画/音效/传感器、拾取、切换 OIT/FXAA/昼夜/雨雪、编辑保存、传送触发卸载/重载、暂停模拟但继续混音，以及离屏尺寸调整。检查实际像素、物理移动、保存文件、音频事件和 PCM 能量，图形错误必须为零。不会自动切换桌面全屏。

`--hidden` 和 `--offscreen` 要求正数 `--frames`，并自动使用离线音频。前者仍创建隐藏窗口，后者不创建应用 Window；Vulkan 全程跳过窗口系统。Windows OpenGL 使用 pbuffer，驱动可能保留隐藏 HWND；其他平台暂不支持 OpenGL 离屏。

还支持 `--backend auto|opengl|vulkan`、`--threaded`、`--single-thread`、`--offline-audio`、`--width`、`--height`、`--fullscreen`（无边框）、`--exclusive`、`--oit`、`--no-fxaa`。auto 优先选择存在的 Vulkan 后端。

## 独立 SDK 工程

SDK 需包含十二个扩展和至少一个后端，保留相邻的 `examples/common`：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/game -B build/game-sdk `
  -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/game-sdk --config Release --parallel 6
ctest --test-dir build/game-sdk -C Release --output-on-failure
```

根工程和 SDK 聚合示例只在所需模块齐全时创建目标；单独配置本目录会明确请求组件。示例受 `RENDERER_BUILD_EXAMPLES` 控制，测试受 `RENDERER_BUILD_TESTS` 和对应 GPU 开关控制，VS 中位于 examples 文件夹。

新增 `renderer.game.{opengl,vulkan}.{window,offscreen}` 四项 CTest。窗口检查使用独立渲染线程，离屏检查使用当前线程；全部使用离线混音和各自独立缓存，OpenGL 离屏检查目前仅 Windows 注册。
