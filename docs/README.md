# 中文文档导航

当前版本为 C++20 渲染引擎，支持 OpenGL 4.5 Core 和可选 Vulkan 1.3。对外服务类采用 PImpl。

| 文档 | 适合解决的问题 |
| --- | --- |
| [项目概览](../README.md) | 已实现什么、有哪些功能边界 |
| [版本变更](../CHANGELOG.md) | 当前源码版本相对于初始提交增加了哪些能力 |
| [快速开始](GETTING_STARTED.md) | 如何构建、运行、接入工程并绘制第一帧 |
| [相机控制](CAMERA.md) | 轨道/跟随/自由模式、平滑、限幅、父变换、设备无关输入及 SDK |
| [游戏 UI](UI.md) | RmlUi 文档/皮肤、基础控件、文字/图片、事件/DPI/捕获、双后端及 SDK |
| [异步资源加载](ASSET_LOADING.md) | 图片/模型后台读取、优先级/取消、CPU 发布与 GPU 上传预算、资源包卸载 |
| [材质与实时纹理](RENDERING_EXTENSIONS.md) | 共用 GLSL 效果、加色/预乘混合、多相机 GPU 输出、文字/UI 增强 |
| [内存与性能诊断](DIAGNOSTICS.md) | CPU/GPU 载荷估算、调试面板、实例增量上传及统计口径 |
| [上传、场景与文字优化](OPTIMIZATION.md) | 合并传输/暂存池、场景根复用、BVH/拾取、R8 与不可变局部更新 |
| [新版性能矩阵](../examples/performance/README.md) | 六种场景、双后端/线程、参考开关、P95/P99 与传输/内存指标 |
| [本机优化对照](PERFORMANCE_RESULTS.md) | 同版本参考/优化实测、上传等待与图集载荷、截图一致性及边界 |
| [五项能力综合示例](../examples/features/README.md) | 同时运行异步树木、风摆/光点、小地图/预览、中文与性能面板 |
| [世界文字](TEXT.md) | 字体/回退、排版、字形图集、节点绑定、Billboard、固定像素字号及遮挡 |
| [压缩纹理与数组](TEXTURES.md) | BC/DDS、离线 mip、逐实例数组层、设备降级及 RHI/UI 边界 |
| [纹理与文字对照示例](../examples/texture_text/README.md) | DDS 打包、数组合批、位图/SDF 缩放与外描边、双后端及 SDK |
| [森林营地综合应用](../examples/game/README.md) | 十二个模块的音画事件联动、完整应用调度、交互玩法、持久化及窗口/离屏输出 |
| [离屏渲染与 PNG](OFFSCREEN.md) | 无应用窗口的双后端纹理渲染、直接保存 PNG、尺寸/线程/平台约束及独立 SDK 示例 |
| [Vulkan 后端](VULKAN.md) | 如何开启 Vulkan、创建窗口/设备、复用前端与验证交换链和双后端一致性 |
| [SDK 安装与二次开发](SDK.md) | 如何指定安装目录、使用 find_package 和验证独立外部工程 |
| [模块划分与按需接入](MODULES.md) | 核心、双后端与十二个可选模块的边界、依赖、构建开关和 SDK 迁移 |
| [Box2D 二维物理](PHYSICS2D.md) | 刚体、常用形状、固定步、受力、事件、查询、距离关节及 Scene/流式同步 |
| [音乐与音效](AUDIO.md) | 音乐流、并发音效、混音分组、空间定位、播放事件和线程约定 |
| [SDK 二次开发指南](DEVELOPMENT_GUIDE.md) | 安装后独立接入、完整窗口/离屏程序、十二模块组合、资源/线程、运行素材和交付排错 |
| [接口与使用约定](API_GUIDE.md) | 如何构建场景树、管理资源、提交帧及使用 RHI / RenderGraph |
| [可选加权 OIT](OIT.md) | 如何开启无序透明、理解合成方式及画质/显存取舍 |
| [可选抗锯齿 FXAA](ANTI_ALIASING.md) | 如何按视图开启抗锯齿、理解颜色空间、成本与兼容方式 |
| [精灵数量与性能对比](SPRITE_BENCHMARK.md) | 如何测试森林精灵数量、对比双后端及线程，并读取 CSV 和帧耗时 |
| [图片角色与三维世界](SPRITES.md) | Sprite/图集、Billboard、方向动画、排序组、拾取和森林示例 |
| [图片世界模块](WORLD_MODULES.md) | 批量精灵、地图分块、连续碰撞、粒子、昼夜天气、淡出/闪白/描边/接触阴影 |
| [地图流式加载](STREAMING.md) | 后台读取、发布预算、滞回卸载、编辑保存、物件/碰撞生命周期和物理库选择 |
| [窗口与输入事件](WINDOW_EVENTS.md) | 如何订阅键鼠、窗口状态和拖放，以及安全取消订阅 |
| [窗口与全屏模式](FULLSCREEN.md) | 如何创建和切换无边框/独占全屏，并恢复窗口位置、尺寸和最大化状态 |
| [架构与扩展](ARCHITECTURE.md) | 数据如何流动、PImpl 如何划界、同步和资源复用如何实现 |
| [从 OpenGL 到 Vulkan 的源码导读](RENDERING_WALKTHROUGH.md) | 从场景到原生 Draw 的封装调用链、资源/绑定/同步对照、交换链与可运行离屏程序 |
| [验证与排错](VALIDATION.md) | 如何运行测试、理解统计、定位常见问题，以及本机验证范围 |
| [源码示例](../examples/README.md) | 无需安装 SDK 构建全部示例，以及独立 SDK 示例的用法 |
| [离屏森林示例](../examples/offscreen/README.md) | 无应用窗口直接保存森林 PNG，以及独立 SDK 工程的构建方式 |
| [源码测试](../tests/README.md) | 测试总开关、OpenGL 开关及只编库/示例/测试的组合 |
| [实现清单](../IMPLEMENTATION.md) | 当前实现状态及明确保留的扩展点 |
| [第三方依赖](../THIRD_PARTY.md) | 依赖版本、源码来源、再生成方式及许可文件 |

使用安装包时先读 SDK 二次开发指南第 1–3 节，编译自己的窗口/离屏程序，再按模块选择示例；从源码构建则可先运行快速开始。查契约时阅读接口说明，修改引擎前先读架构中的生命周期与线程约定。

## 源码阅读入口

| 关注点 | 文件 |
| --- | --- |
| 场景树、组件与变换版本 | `include/renderer/scene.hpp`、`src/scene.cpp` |
| CPU 资源与模型导入 | `include/renderer/assets.hpp`、`src/assets.cpp`、`src/model.cpp` |
| 图片与视图渲染数据 | `include/renderer/sprite.hpp`、`sprite_snapshot.hpp` 及对应 cpp |
| 动画和交互扩展 | `modules/animation`、`modules/interaction`，仅依赖核心公共接口 |
| 图片世界扩展 | `modules/tilemap`、`collision`、`particles`、`environment`、`presentation`，仅依赖核心公共接口 |
| 音频扩展 | `modules/audio`，独立 PImpl，miniaudio 不进入公共头文件 |
| 不可变快照与提取缓存 | `include/renderer/render_snapshot.hpp`、`src/renderer.cpp` |
| 裁剪、排序、实例合批与阴影投影 | `src/frame_builder.cpp` |
| Shader 编译、反射与绑定映射 | `include/renderer/shader.hpp`、`src/shader.cpp` |
| 图资源依赖、屏障与物理复用 | `include/renderer/render_graph.hpp`、`src/render_graph.cpp` |
| 渲染队列与上下文交接 | `src/internal/backend.hpp` |
| 内置渲染阶段及可选 OIT 路径 | `src/backends/common/renderer_backend.cpp` |
| 原生 OpenGL 实现与延迟销毁 | `src/backends/opengl/rhi_device.cpp` |
| 原生 Vulkan 资源、同步及交换链 | `src/backends/vulkan/rhi_device.cpp` |
| 窗口、事件与主线程约束 | `include/renderer/window.hpp`、`src/platform/window.cpp` |
| 完整交互示例 | `examples/demo.cpp` |
| 示例与测试构建入口 | `examples/CMakeLists.txt`、`tests/CMakeLists.txt` |
| 二次开发配套工程 | `examples/development`，支持根工程直接构建或独立 SDK 工程 |

自有头文件统一使用 include guard，并在文件、类与函数前提供中文说明；函数注释包含参数与返回值语义。中文源码注释重点说明接口契约、算法原因和生命周期。第三方头文件、生成的 GLAD 和许可证原文保持上游内容。
