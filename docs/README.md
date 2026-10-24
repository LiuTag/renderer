# 中文文档导航

当前版本为 C++20 渲染引擎，支持 OpenGL 4.5 Core 和可选 Vulkan 1.3。对外服务类采用 PImpl。

| 文档 | 适合解决的问题 |
| --- | --- |
| [项目概览](../README.md) | 已实现什么、有哪些功能边界 |
| [快速开始](GETTING_STARTED.md) | 如何构建、运行、接入工程并绘制第一帧 |
| [Vulkan 后端](VULKAN.md) | 如何开启 Vulkan、创建窗口/设备、复用前端与验证交换链和双后端一致性 |
| [SDK 安装与二次开发](SDK.md) | 如何指定安装目录、使用 find_package 和验证独立外部工程 |
| [模块划分与按需接入](MODULES.md) | core/opengl/animation/interaction/audio 的边界、依赖、构建开关和 SDK 迁移 |
| [音乐与音效](AUDIO.md) | 音乐流、并发音效、混音分组、空间定位、播放事件和线程约定 |
| [二次开发手册](DEVELOPMENT_GUIDE.md) | 从 SDK 接入到交互场景、Compute 和自定义 RenderGraph 的完整开发路径 |
| [接口与使用约定](API_GUIDE.md) | 如何构建场景树、管理资源、提交帧及使用 RHI / RenderGraph |
| [可选加权 OIT](OIT.md) | 如何开启无序透明、理解合成方式及画质/显存取舍 |
| [可选抗锯齿 FXAA](ANTI_ALIASING.md) | 如何按视图开启抗锯齿、理解颜色空间、成本与兼容方式 |
| [精灵数量与性能对比](SPRITE_BENCHMARK.md) | 如何测试森林精灵数量、对比双后端及线程，并读取 CSV 和帧耗时 |
| [图片角色与三维世界](SPRITES.md) | Sprite/图集、Billboard、方向动画、排序组、拾取和森林示例 |
| [窗口与输入事件](WINDOW_EVENTS.md) | 如何订阅键鼠、窗口状态和拖放，以及安全取消订阅 |
| [窗口与全屏模式](FULLSCREEN.md) | 如何创建和切换无边框/独占全屏，并恢复窗口位置、尺寸和最大化状态 |
| [架构与扩展](ARCHITECTURE.md) | 数据如何流动、PImpl 如何划界、同步和资源复用如何实现 |
| [验证与排错](VALIDATION.md) | 如何运行测试、理解统计、定位常见问题，以及本机验证范围 |
| [源码示例](../examples/README.md) | 无需安装 SDK 构建全部示例，以及独立 SDK 示例的用法 |
| [源码测试](../tests/README.md) | 测试总开关、OpenGL 开关及只编库/示例/测试的组合 |
| [实现清单](../IMPLEMENTATION.md) | 当前实现状态及明确保留的扩展点 |
| [第三方依赖](../THIRD_PARTY.md) | 依赖版本、源码来源、再生成方式及许可文件 |

第一次使用建议先运行快速开始，再沿二次开发手册完成四个配套示例；查具体契约时阅读接口说明。修改引擎前先阅读架构文档中的生命周期与线程约定。

## 源码阅读入口

| 关注点 | 文件 |
| --- | --- |
| 场景树、组件与变换版本 | `include/renderer/scene.hpp`、`src/scene.cpp` |
| CPU 资源与模型导入 | `include/renderer/assets.hpp`、`src/assets.cpp`、`src/model.cpp` |
| 图片与视图渲染数据 | `include/renderer/sprite.hpp`、`sprite_snapshot.hpp` 及对应 cpp |
| 动画和交互扩展 | `modules/animation`、`modules/interaction`，仅依赖核心公共接口 |
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
