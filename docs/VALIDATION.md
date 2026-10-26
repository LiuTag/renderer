# 验证与排错

[返回文档导航](README.md) · [版本变更](../CHANGELOG.md)

## 当前版本与环境

记录日期：2026-10-26；当前 CMake/SDK 包版本为 0.2.0。完整配置开启 OpenGL、Vulkan、十二个扩展、示例和两个后端的 GPU 检查。

实测环境为 Windows、Visual Studio 2022 / MSVC 19.44、NVIDIA GeForce RTX 2080 Ti、驱动 610.62、OpenGL 4.5 Core；Vulkan 开发环境为 SDK 1.4.350.0，后端要求 Vulkan 1.3，并启用可用的 Khronos 验证层与同步校验。已安装 SDK 的示例验证不依赖 VULKAN_SDK 环境变量。其他操作系统及显卡尚未实机验证。

| 配置或检查 | 当前结果 |
| --- | --- |
| 完整 Debug | 94 项 CTest 全部通过，包含 54 个测试本体和 40 项示例运行检查 |
| 完整 Release | 23 个示例目标构建成功，94 项 CTest 全部通过 |
| CPU 核心 | 关闭两个后端、十二个扩展和示例后，10 项核心检查通过；PNG 编码无需图形设备 |
| CPU 相机（此前） | 只启用 core + camera、关闭两个后端及示例，8 项检查通过；安装后的 camera 示例无需 Vulkan SDK/窗口，独立构建运行通过 |
| CPU UI（此前） | 只启用 core + ui，8 项检查通过；从该精简 SDK 独立构建并生成 UI 几何成功，无需窗口/Vulkan SDK |
| CPU 文字 | 只启用 core + text、关闭 UI 和两个后端，12 项检查通过；独立字体文件下载/安装，无需 RmlUi |
| 文字 SDK | 精简 text SDK 无窗口生成字形批次通过；关闭 text 后不安装其头文件/库，REQUIRED COMPONENTS text 查询按预期失败 |
| UI 组件与示例 | 关闭 UI 后不安装其头文件/库，REQUIRED 查询按预期失败；完整 SDK 的 UI 示例可独立配置并运行四种窗口/离屏组合 |
| 关闭相机 | 精简 SDK 不含相机头文件/库；REQUIRED COMPONENTS camera 在配置时按预期失败 |
| SDK 安装 | Debug/Release 每配置 27 个静态库，按组件安装头文件、依赖、许可证、文档和示例 |
| 独立 SDK 示例 | 清除 VULKAN_SDK 后，聚合示例的 40 项 Release 检查全部通过；game 综合示例支持独立配置并运行四种后端/输出组合 |
| 跨后端一致性 | 同一快照、两种线程模式、缩放及透明/FXAA 组合共 12 次比较；统计一致，本机测试场景中每次平均 RGB 色值差小于 0.0001，阈值外像素为零 |
| 文件规范 | 自有 C++ 文件通过 clang-format；头文件采用唯一 include guard，公共接口包含中文用途、参数及返回值说明 |
| 文档与依赖 | UTF-8、本地链接、固定第三方文件哈希/许可证和 VS core/modules/examples/tests 分组检查通过 |

CTests 数量随构建开关变化，以 `ctest -N` 的当前配置清单为准。此前还验证过 Vulkan-only、精简 SDK、缺失组件的 REQUIRED 查询失败，以及测试从 ON 改为 OFF 后清单清空；这些配置的历史测试数量不作为完整配置的当前统计。

功能回归重点如下：

- 图片世界：不可变批次的旧版本/资源保留、Billboard 保守界限、负坐标瓦片、连续移动、图片粒子、昼夜、淡出/闪白/描边及接触阴影。world 示例覆盖双后端、流式地图和 Box2D；关闭 physics2d 后的 collision 回退路径也已验证。
- 综合应用：十二模块真实联动，动画触发脚步音效、碰撞与传感器、推箱子及距离约束、拾取/地面投影、流式传送/编辑保存、昼夜/雨雪/配乐/环境声、暂停时独立混音及离屏尺寸调整。每项 240 帧实际生成 192,000 个立体声音频帧、PNG、WAV 和会话 JSON。
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
| `renderer.overlay` | `tests/overlay_tests.cpp` | 通用覆盖层尺寸/颜色/索引、资源引用、纹理副本及快照生命周期 |
| `renderer.ui` | `tests/ui_tests.cpp` | RmlUi 控件/布局、事件/取消/异常、文本、DPI、拖动捕获、文档与线程身份 |
| `renderer.text` | `tests/text_tests.cpp` | Unicode 字体/回退、严格 UTF-8、排版、图集缓存/容量、绑定/朝向、屏幕字号及批次存活 |
| `renderer.text.opengl` / `renderer.text.vulkan` | `tests/text_render_tests.cpp` | 真实遮挡/深度开关、OIT/FXAA、固定屏幕字号/缩放、图集旧版本及字体销毁 |
| `renderer.ui.opengl` / `renderer.ui.vulkan` | `tests/ui_render_tests.cpp` | GPU 回读验证预乘 Alpha/sRGB、透明顺序、裁剪、纹理原点、字体、线程和快照保留 |
| `renderer.camera` | `tests/camera_tests.cpp` | 三种模式、输入/坐标、平滑、限幅、传送/切换、父变换、镜头保留和失败原子性 |
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
| `renderer.game.{opengl,vulkan}.{window,offscreen}` | 综合应用音画联动、流式物件/保存、物理/动画/拾取、PNG 与实际 WAV 混音；四项均不依赖声卡 |

高级 GPU 测试进一步覆盖：工作线程独立录制命令、逻辑组绑定映射、GPU 生成间接参数、图分配后 Device 移动、纹理视图底层存储存活、透明线性混合、Alpha Mask、HDR ToneMap、异步 Pass 计时、方向光接收阴影和 PBR 参数变化。

阴影回归使用同一 RenderView，仅切换 shadows，比较无遮挡接收面的输出以捕获自阴影条纹。线程测试覆盖有界 submit、工作线程呈现、窗口缩放，以及 Window 包装对象先释放时的上下文存活。

## 复现命令

窗口 GPU 测试通过 GLFW 和独立渲染纹理调用真实后端。大多数使用隐藏窗口，全屏测试会短暂显示窗口并以串行方式切换当前桌面模式；新增离屏测试不初始化 GLFW，Vulkan 离屏无需显示服务，Windows OpenGL 使用 WGL pbuffer。标准 `windows` 预设在 GPU 检查关闭时只注册 CPU 检查，具体数量随模块及示例开关变化。GPU 测试使用单独预设：

```powershell
cmake --preset windows-gpu-tests
cmake --build --preset windows-gpu-tests --parallel 6
ctest --preset windows-gpu-tests --verbose
```

完整双后端配置使用以下命令；十二个扩展保持开启，已有缓存中关闭过的扩展需要重新设置对应开关：

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

## 五项渲染基础增强验证（2026-10-25）

- 后台资源：真实 PNG/OBJ/MTL 解码、Unicode 及补充平面字符路径、发布预算、取消/失败、包卸载和旧快照/材质内存保留。
- 双后端真实像素：新资源上传预算、静态实例预热后零上传、单实例范围更新、顶点/表面函数、Alpha Mask/阴影 Shader、加色与预乘、相机纹理方向/实时更新，以及反馈/跨设备/设备销毁后的拒绝。
- UI/文字：预编辑更新、选区恢复、中文提交/取消/失焦、剪贴板服务、调试面板不抢焦点、字体预热/缓存回收/卸载；Windows 原生组合生命周期通过消息注入检查延迟排队，UI GPU 图片在上下文销毁后继续保留实时纹理。
- features 四组合：Windows 窗口/离屏、OpenGL/Vulkan、两种线程模式，90 帧并保存 PNG；已查看小地图、角色预览、中文名字牌/输入与统计面板的实际图像。
- SDK：无 VULKAN_SDK 环境变量，安装后的全部 33 项示例检查通过；API 和源示例不包含原生图形/第三方实现头文件。

原生 IME 的自动验证覆盖消息桥接及 UI 状态机，不冒充对所有输入法版本、候选窗口外观或其他操作系统的人工验证。GPU 内存统计为载荷估算，测试不将其当作驱动实际显存。

## 压缩/数组与 SDF 回归（2026-10-25）

该阶段完整配置为 88 项检查：50 个测试本体、38 项示例运行检查，22 个示例目标。新增 BC1/2/3/4/5/7 GPU 上传/回读、数组层/mip 视图及存储图像/拷贝、逐实例层合批、强制 RGBA 降级、sRGB/Alpha 离线 mip 和 DDS Unicode/截断检查。SDF 覆盖规范字号复用、平滑边缘/外描边、LRU 保护与旧快照像素保留；UI 与拾取验证压缩数组选层。

texture_text 在两后端窗口/离屏四种组合运行，并实际检查 PNG。对照图显示独立图片、RGBA8 数组、BC3 数组+mip 以及位图/SDF 放大文字。GPU 格式检查以本机设备能力为准，不代表所有驱动支持所有 BC 格式；降级路径另用 forceTextureDecompression 真实执行。文档见[纹理说明](TEXTURES.md)和[示例](../examples/texture_text/README.md)。

该阶段 Debug/Release 的 88 项均通过（修正新增拾取测试的尺寸夹具后重跑该项）；安装后的 38 项聚合检查、texture_text 独立 SDK 工程的 5 项检查通过，均不依赖 VULKAN_SDK 环境变量。仅 core 的 9 项、core+text 的 11 项 CPU 检查通过；仅 core 的精简 SDK 可独立构建和运行 DDS 打包器。新增目标的 VS 分组、头文件保护、格式、本地文档链接、两个编码库的哈希/原始许可及可搬移导出均已检查。

## 上传、场景与字形优化（2026-10-25）

最终 Debug/Release 各 93 项 CTest 全部通过；53 个测试本体与 40 项示例检查，23 个示例目标。资源包卸载后失效提取的缓存引用已修复，新增检查覆盖显式清理、移动后 BVH 重拟合、极端位移余量的非有限界限拒绝。后续边界检查通过 Debug/Release 的核心/引擎/优化三项定向重跑。

关闭后端的 core+text 12 项、仅 core 10 项检查通过；仅 core SDK 的 DDS 工具独立构建运行通过。Debug/Release SDK 每配置仍 27 个静态库；移除 VULKAN_SDK 后，聚合示例 40 项检查通过，performance 独立 SDK 的两项六负载/双线程检查通过。独立工程关闭测试或示例后，CTest 清单均为 0，恢复开关后重新生成。

性能采样为两后端、两线程、六负载、每档两轮，各 48 行参考/优化数据，5000 精灵、16 帧预热、80 帧采样；分块原有粗裁剪快路径恢复后单独按同参数补采。48 对终帧 PNG 字节完全一致，已查看实际中文字形压力图。上传统计验证 Vulkan 数组流入 80 帧由 580 次提交/等待变为 10 次提交、0 次上传等待；字形像素载荷为原来的四分之一。均值/P95、CPU 字形首次栅格化瓶颈和小批次边界见[性能报告](PERFORMANCE_RESULTS.md)，复现入口见[基准](../examples/performance/README.md)。

## 渲染封装与双后端导读（2026-10-26）

[源码导读](RENDERING_WALKTHROUGH.md)按当前实现核对场景到原生 Draw 的调用链、CPU/设备提交完成语义、描述符分配、上传批次、统一 GENERAL 布局和独立窗口呈现流程，并在相关段落链接实际查阅的 Khronos 官方资料。

验证直接从导读中提取第一个完整 C++ 和 CMake 代码块，在 `build/windows/walkthrough-check` 建立独立 SDK 消费工程，以 Release 编译，并分别运行 OpenGL/Vulkan 离屏路径；两次均通过调试错误计数检查并输出 960×640 橙色立方体 PNG，已查看两张实际图片。两个文件均为 26,302 字节，SHA256 均为 `1bc7e26ba083f25418bb1eb6babfe1703767343878312ac7d35e14f9a10939c9`。这是当前设备上的示例一致性结果，不推广为任意 Shader/硬件的逐字节一致保证。

本次只增加和校准文档，不改变渲染实现；验证范围为文档示例的独立构建、双后端运行、图片与导航/安装检查，未重新执行整套 93 项回归。完整回归记录保留在前述对应阶段。

## Vulkan 提交复用与 VMA（2026-10-26）

Debug/Release 全量构建成功，各 94 项 CTest 全部通过，包括新增 renderer.vulkan.allocation，以及已有的双后端像素、纹理数组/压缩/图像视图、在途销毁、COW 字形、窗口/全屏/缩放、渲染线程及综合应用检查。没有修改原有参考图片或降低像素断言。

专项在本机观察到新增 160 个小资源分配复用了预热时已申请的设备内存块，没有新增块；资源释放后存活分配计数回到含暂存池的基线。300 个不同绑定组触发第二个描述符池；预热后 12 轮提交的上下文/描述符池 created 计数不再增长，实际 Compute 累加结果正确。另检查相邻映射释放、原始纹理销毁后的视图、异常录制后的新提交、GPU 复制期间立即删除源句柄、完成标记和空闲缓存上限。当前单次采样是对象复用/子分配证据，不宣称新的 FPS 提升。

安装 Debug/Release SDK 后，40 项 Release 聚合示例检查和仅链接 renderer::vulkan 的独立分配测试消费者通过。独立消费者移除 VULKAN_SDK 环境变量后配置和编译成功，未使用 Vulkan/VMA 实现头文件；实现对象合入 Vulkan 库，SDK 仍每配置 27 个静态库。VS 中新实现目标属于 core、新专项属于 tests，VMA 头文件/许可哈希与私有可迁移导出已检查。

最后保留原有每类描述符 4096 条目的类型容量，集合容量为每池 256；改动后重建全部 Debug/Release 目标，并在两个配置分别重跑 allocation/core/advanced/texture/offscreen/window 六项定向检查，全部通过。

## 已安装 SDK 二次开发指南交付（2026-10-26）

指南整理为 24 节，从独立 CMake 接入和完整 main.cpp 开始，覆盖场景/资源/线程、RHI/RenderGraph、十二个扩展、材质/实时纹理、应用调度、运行素材、升级和发布。SDK 文档首页及导航指向 share/renderer/docs/DEVELOPMENT_GUIDE.md；包配置的 renderer_DOCUMENTATION_DIR / renderer_DEVELOPMENT_GUIDE 随安装前缀解析。

直接提取指南第 2.2 节完整 CMake 和第 3.4 节程序，使用全新 Release 安装目录 `build/windows/sdk-delivery/Renderer SDK`（路径含空格），移除 VULKAN_SDK 后分别配置和编译 OpenGL/Vulkan。两个后端均运行隐藏窗口/离屏各 4 帧并保存 960×640 PNG，已查看两个后端的实际棋盘地面/立方体画面；本机同后端窗口/离屏 PNG 字节相同，不要求跨后端逐字节相等。

同一安装包中的聚合 examples 在另一个构建目录独立配置/编译成功，全部 40 项 Release 检查通过，过程中不访问引擎私有头文件或下载第三方开发库。SDK 包配置、示例字体、RML/RCSS 及相对文档导航均使用安装目录内容。本轮应用代码只存在于文档和验证临时目录，未增加引擎示例/测试目标数；完整源码仍为 94 项检查、23 个示例目标。
