# 验证与排错

[返回文档导航](README.md) · [版本变更](../CHANGELOG.md)

## 当前版本与环境

记录日期：2026-10-24；当前 CMake/SDK 包版本为 0.2.0。完整配置开启 OpenGL、Vulkan、九个扩展、示例和两个后端的 GPU 检查。

实测环境为 Windows、Visual Studio 2022 / MSVC 19.44、NVIDIA GeForce RTX 2080 Ti、驱动 610.62、OpenGL 4.5 Core；Vulkan 开发环境为 SDK 1.4.350.0，后端要求 Vulkan 1.3，并启用可用的 Khronos 验证层与同步校验。已安装 SDK 的示例验证不依赖 VULKAN_SDK 环境变量。其他操作系统及显卡尚未实机验证。

| 配置或检查 | 当前结果 |
| --- | --- |
| 完整 Debug | 48 项 CTest 全部通过，包含 32 个测试本体和 16 项示例运行检查 |
| 完整 Release | 15 个示例目标构建成功，48 项 CTest 全部通过 |
| CPU 核心 | 关闭两个后端、九个扩展和示例后，6 项核心检查通过；PNG 编码无需图形设备 |
| SDK 安装 | Debug/Release 每配置 22 个静态库，按组件安装头文件、依赖、许可证、文档和示例 |
| 独立 SDK 示例 | 清除 VULKAN_SDK 后，聚合示例的 16 项 Release 检查全部通过；单独配置 offscreen 目录的双后端检查也通过 |
| 跨后端一致性 | 同一快照、两种线程模式、缩放及透明/FXAA 组合共 12 次比较；统计一致，本机测试场景中每次平均 RGB 色值差小于 0.0001，阈值外像素为零 |
| 文件规范 | 自有 C++ 文件通过 clang-format；头文件采用唯一 include guard，公共接口包含中文用途、参数及返回值说明 |
| 文档与依赖 | UTF-8、本地链接、固定第三方文件哈希/许可证和 VS core/modules/examples/tests 分组检查通过 |

CTests 数量随构建开关变化，以 `ctest -N` 的当前配置清单为准。此前还验证过 Vulkan-only、精简 SDK、缺失组件的 REQUIRED 查询失败，以及测试从 ON 改为 OFF 后清单清空；这些配置的历史测试数量不作为完整配置的当前统计。

功能回归重点如下：

- 图片世界：不可变批次的旧版本/资源保留、Billboard 保守界限、负坐标瓦片、连续移动、图片粒子、昼夜、淡出/闪白/描边及接触阴影。world 示例覆盖双后端、流式地图和 Box2D；关闭 physics2d 后的 collision 回退路径也已验证。
- 流式加载：后台读取与主线程发布、距离优先与滞回、预算/容量背压、协作取消、Unicode 区块文件、损坏校验、编辑保存、保存期间再次编辑、失败保存不卸载，以及加载/卸载时的物件与碰撞生命周期。
- Box2D：1/2 个内部工作线程、常用/复合形状、固定步与过载、力/冲量、接触/传感器/命中事件、查询过滤、距离关节、句柄代际、删除后的退出事件及父变换/插值同步。
- 离屏输出：不初始化 GLFW 的独立进程，验证 RHI 纹理渲染、奇数尺寸、行序/显示编码、单线程/渲染线程一致性、排队帧、缩放、非法操作恢复、OIT/FXAA、GPU 计时和 PNG/Unicode 路径。Vulkan 检查无新增进程顶层 HWND；OpenGL 检查无可见窗口和残留应用/引导窗口，允许驱动内部隐藏对象。已查看实际输出的 1280×720 森林 PNG。

此前的精灵性能矩阵在 1,000/10,000 精灵、25/100/400 图块、双后端和两种线程下重复三轮，共 72 组。该记录早于图片世界效果扩展，只作为当时版本的功能/统计验证；当前检查不设置 FPS 门槛，重新测量使用[性能示例](SPRITE_BENCHMARK.md)。

## 测试入口与覆盖

源码测试全部位于 tests；示例检查由 tests/example_tests.cmake 统一注册。以下表格列出各测试入口的职责，细分断言位于对应源码。

| CTest 名称 | 源码 | 主要内容 |
| --- | --- | --- |
| `renderer.core` | `tests/core_tests.cpp` | 数学、句柄归属/代际、层级、变换、组件、资源与快照生命周期 |
| `renderer.engine` | `tests/engine_tests.cpp` | Shader 编译/反射/缓存、图编译校验、模型图片导入、裁剪、透明排序、阴影投影者、正交相机 |
| `renderer.events` | `tests/event_tests.cpp` | 订阅顺序、数据副本、RAII/显式取消、移动、新增订阅、异常、重入和跨线程取消 |
| `renderer.sprites` | `tests/sprite_tests.cpp` | 元数据、合批、裁剪/翻转、Billboard、排序组和只读视图快照 |
| `renderer.animation` | `tests/animation_tests.cpp` | 方向序列帧、跨循环事件、速度、暂停、结束状态和无事件追帧 |
| `renderer.interaction` | `tests/interaction_tests.cpp` | Alpha/Mask 拾取、筛选、透视/正交地面求交和资源生命周期 |
| `renderer.audio` | `tests/audio_tests.cpp` | 无声卡实际混音、音乐流补页/循环、包络、空间定位、实例与资源生命周期 |
| `renderer.physics2d` | `tests/physics2d_tests.cpp` | 真实刚体/CCD/命中、事件、查询、距离约束、句柄和场景同步 |
| `renderer.streaming` | `tests/streaming_tests.cpp` | 后台读写、主线程解析、预算、滞回、取消、保存/重试、版本和区块文件 |
| `renderer.batch` | `tests/batch_tests.cpp` | 不可变批次、资源保留、整块裁剪与几何包围盒 |
| `renderer.tilemap` | `tests/tilemap_tests.cpp` | 负坐标、脏块缓存、卸载及旧快照 |
| `renderer.collision` | `tests/collision_tests.cpp` | 连续圆形移动、滑动、层过滤、穿透及句柄代际 |
| `renderer.particles` | `tests/particles_tests.cpp` | 固定步、确定性种子、容量、寿命和四种预设 |
| `renderer.environment` | `tests/environment_tests.cpp` | 昼夜绕回、太阳父变换、调色和天气过渡 |
| `renderer.presentation` | `tests/presentation_tests.cpp` | Alpha 遮挡、淡出/恢复、闪白及节点删除 |
| `renderer.opengl.effects` / `renderer.vulkan.effects` | `tests/effects_gpu_tests.cpp` | 两种线程的真实效果像素及关闭恢复 |
| `renderer.window` | `tests/window_tests.cpp` | 原生事件适配、真实窗口缩放、关闭取消、窗口移动、线程归属和销毁生命周期 |
| `renderer.fullscreen` | `tests/fullscreen_tests.cpp` | 创建/切换两种全屏、位置尺寸与最大化恢复、隐藏/非法参数/错误线程、上下文及帧缓冲 |
| `renderer.opengl` | `tests/gl_tests.cpp` | 深度遮挡、实例偏移、帧槽复用和扩容、镜像绕序、纹理行序/sRGB、资源回收 |
| `renderer.opengl.advanced` | `tests/advanced_gl_tests.cpp` | Compute、间接绘制、资源同步、图执行与物理复用、纹理视图/拷贝、完整渲染链路及线程生命周期 |
| `renderer.opengl.aa` | `tests/aa_gl_tests.cpp` | FXAA 斜边参考误差、平坦色、开关、两种透明、HDR、计时、缩放及线程 |
| `renderer.opengl.sprites` | `tests/sprite_gl_tests.cpp` | 图集实例 UV/tint、落地点排序、OIT 队列、FXAA、相机旋转、Alpha/Mask、阴影、快照及缩放 |
| `renderer.vulkan.core` | `tests/gl_tests.cpp` | 用相同断言验证 Vulkan 深度、实例、纹理、绕序及资源生命周期 |
| `renderer.vulkan.advanced` | `tests/advanced_gl_tests.cpp` | Vulkan Compute/Indirect、同步、资源/图复用、完整管线及线程 |
| `renderer.vulkan.aa` | `tests/aa_gl_tests.cpp` | Vulkan FXAA 像素质量、透明组合、HDR、计时和缩放 |
| `renderer.vulkan.sprite` | `tests/sprite_gl_tests.cpp` | Vulkan Billboard、图集、排序、OIT、Alpha/阴影及快照 |
| `renderer.vulkan.window` | `tests/vulkan_window_tests.cpp` | sRGB/UNORM/float 呈现、交换链重建、两种全屏、事件和渲染线程 |
| `renderer.vulkan.parity` | `tests/backend_parity_tests.cpp` | 同一快照的 OpenGL/Vulkan 图像、统计、线程和视图组合对比 |
| `renderer.image` | `tests/image_tests.cpp` | 无图形设备的 PNG RGBA/Alpha/行序、Unicode、错误输入和 IO |
| `renderer.opengl.offscreen` / `renderer.vulkan.offscreen` | `tests/offscreen_tests.cpp` | 无 GLFW 初始化的纹理渲染/回读、精确尺寸、线程/队列、缩放、后处理和窗口约束 |

根工程开启示例和测试后，还注册以下运行检查。启用 OpenGL 时前五项需要 `RENDERER_GL_TESTS=ON`，模块使用者和音频示例为 CPU 检查；关闭 OpenGL 后，SDK 使用者也作为 CPU 检查注册。它们也可以通过独立配置对应示例目录运行，注册逻辑统一位于 `tests/example_tests.cmake`：

| CTest 名称 | 验证内容 |
| --- | --- |
| `development.scene` | 隐藏窗口运行四帧，覆盖纹理/PBR、透明、相机姿态和专用渲染线程 |
| `development.compute` | Compute 写入 16 个整数，显式屏障、GPU 拷贝、完成等待及 CPU 数值核对 |
| `development.graph` | 自定义图形 Pipeline、三阶段图、一个无用 Pass 剔除、重复执行、GPU 时间戳和图像中心像素 |
| `development.sprite` | 森林示例运行 24 帧，OIT/FXAA/专用渲染线程、方向动画、事件及地面投影自检 |
| `renderer.sdk.consumer` | 公共 SDK 接口、场景与资源、图形环境可用时的真实 OpenGL 提交和回读 |
| `renderer.sdk.modules` | CPU 公共接口及当前启用的动画/交互模块 |
| `renderer.physics2d.example` | 公开 SDK 的无窗口推挤/传感器/接触/查询及 Scene 高度检查 |
| `renderer.audio.example` | 程序生成音频、离线实际混音、空间音效和两首音乐交叉切换 |
| `renderer.benchmark.opengl` / `renderer.benchmark.vulkan` | 精灵数量、图集种类与线程矩阵的功能检查，不设 FPS 门槛 |
| `renderer.vulkan.example` | 共用三维示例，以 Vulkan 隐藏窗口、专用线程、OIT 和 FXAA 运行 |
| `renderer.vulkan.forest` | 共用森林示例，以 Vulkan 运行方向动画和交互自检 |
| `renderer.world.opengl` / `renderer.world.vulkan` | 60 帧流式地图/碰撞/粒子/环境场景，包含编辑保存、传送后的卸载/重载及 OIT/昼夜/天气切换 |
| `renderer.offscreen.opengl.example` / `renderer.offscreen.vulkan.example` | 无应用窗口直接输出森林 PNG，支持独立 SDK 构建 |

高级 GPU 测试进一步覆盖：工作线程独立录制命令、逻辑组绑定映射、GPU 生成间接参数、图分配后 Device 移动、纹理视图底层存储存活、透明线性混合、Alpha Mask、HDR ToneMap、异步 Pass 计时、方向光接收阴影和 PBR 参数变化。

阴影回归使用同一 RenderView，仅切换 shadows，比较无遮挡接收面的输出以捕获自阴影条纹。线程测试覆盖有界 submit、工作线程呈现、窗口缩放，以及 Window 包装对象先释放时的上下文存活。

## 复现命令

窗口 GPU 测试通过 GLFW 和独立渲染纹理调用真实后端。大多数使用隐藏窗口，全屏测试会短暂显示窗口并以串行方式切换当前桌面模式；新增离屏测试不初始化 GLFW，Vulkan 离屏无需显示服务，Windows OpenGL 使用 WGL pbuffer。标准 `windows` 预设在完整模块及示例开启、GPU 检查关闭时注册十九项 CPU 检查，GPU 测试使用单独预设：

```powershell
cmake --preset windows-gpu-tests
cmake --build --preset windows-gpu-tests --parallel 6
ctest --preset windows-gpu-tests --verbose
```

完整双后端配置使用以下命令；九个扩展保持开启，已有缓存中关闭过的扩展需要重新设置对应开关：

```powershell
cmake --preset windows -DRENDERER_BUILD_OPENGL=ON -DRENDERER_BUILD_VULKAN=ON `
  -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_TESTS=ON `
  -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/windows --config Debug --parallel 6
ctest --test-dir build/windows -C Debug --output-on-failure
cmake --build build/windows --config Release --parallel 6
ctest --test-dir build/windows -C Release --output-on-failure
```

运行指定测试或失败后只重跑失败项：

```powershell
ctest --test-dir build/windows-gpu-tests -C Debug -R renderer.engine --output-on-failure
ctest --test-dir build/windows-gpu-tests -C Debug --rerun-failed --output-on-failure
```

Release 集成与截图：

```powershell
cmake --preset windows
cmake --build --preset windows-release --parallel 6
ctest --test-dir build/windows -C Release --output-on-failure
.\build\windows\Release\renderer_demo.exe --threaded --hidden --frames 30 --model tests/assets/triangle.glb --capture build/demo-complete.ppm
.\build\windows\Release\renderer_demo.exe --threaded --oit --hidden --frames 30 --model tests/assets/triangle.glb --capture build/demo-oit.ppm
```

此前记录的截图场景有 914 个可见实例、28 次总绘制（场景 14、阴影 13、ToneMap 1）、15,687 个场景三角形、零 OpenGL 错误。它是固定场景的功能集成结果；PNG 预览由实际帧缓冲 PPM 回读转换而来。

## 理解统计数据

- `sceneInstances` 是快照中的网格与精灵实例总数；`visibleInstances` 是相机层过滤与视锥测试后保留的数量，不是 submesh 数。
- `drawCalls` 包含阴影、场景、ToneMap 和可选的 OIT Resolve / FXAA；`transparentDrawCalls` 属于场景绘制的子集，不能再次叠加到总数。`transparencyResolveDrawCalls` 在 OIT 为 1，`antiAliasingDrawCalls` 在 FXAA 开启时为 1。
- `triangles` 为场景绘制三角形，包含实例数，不含阴影阶段及全屏三角形。
- `prepareMilliseconds` 是前端 CPU 准备耗时；`submitMilliseconds` 是后端 CPU 耗时，可能包含等待帧槽。
- `gpuMilliseconds` 和 `passes` 来自已完成旧帧，必须检查 `gpuTimingValid`；前几帧无数据是正常情况。
- `retiredResources` 是等待回收的原生对象数量，不等于活跃资源数，也不是字节数。

## 常见问题

| 现象 | 检查方法 |
| --- | --- |
| CMake 找不到 VS 编译器 | 安装 VS2022 桌面 C++ 工作负载，确认使用 x64 预设 |
| 首次配置下载失败 | 检查网络，或按[快速开始](GETTING_STARTED.md)准备精确版本源码并设置 FetchContent 覆盖路径 |
| GPU 测试无法创建上下文 | 检查驱动是否支持 OpenGL 4.5 Core、当前会话是否有可用桌面；无显示环境可先运行 CPU 套件 |
| `Camera is absent or hidden in snapshot` | 检查 cameraNode 属于当前场景、存在 Camera 组件且祖先可见，并在提取后传入同一快照 |
| 物体不可见 | 检查层掩码、相机姿态及裁剪面、材质槽、节点可见性；零缩放导致不可逆变换会跳过实例 |
| `Stale handle or handle belongs to another owner` | 检查资源是否已销毁，以及是否跨 Scene/AssetStore/Device 混用句柄；bool 非空不代表有效 |
| 线程归属错误 | 窗口事件/缩放在创建线程执行；专用渲染线程模式用 Renderer::present()；低层 Device 只在上下文线程调用 |
| RenderGraph Pass 没有执行 | 检查输出是否 export，或 Pass 是否声明 sideEffect，以及依赖链是否连接到保留输出 |
| `Uninitialized read` 或重叠访问错误 | 检查 mip/字节范围的首个访问是否写入；读改写使用一条 StorageReadWrite；导入时声明真实初始状态 |
| 图片上下颠倒或颜色偏暗/过亮 | 区分纹理/截图行序，确认 glTF 没有额外翻转；数据贴图关闭 sRGB，避免对输出再次编码 |
| 异步提交仍会等待 | 检查队列是否已满、是否立即调用 get/present、帧槽是否待 GPU 完成，以及垂直同步设置 |
| 中文注释显示乱码 | 源码和文档使用 UTF-8；MSVC 目标已设置 /utf-8，编辑器也应使用 UTF-8 |

`readback()` 和 `waitIdle()` 都是同步操作，不应放入希望衡量无阻塞吞吐的每帧路径。Shader 编译异常包含 sourceName 和编译日志，建议保留这些信息定位布局或源码问题。
