# 验证与排错

[返回文档导航](README.md)

## 验证环境与范围

最近记录日期：2026-10-24。环境为 Windows、Visual Studio 2022 / MSVC 19.44、NVIDIA GeForce RTX 2080 Ti、驱动 610.62、OpenGL 4.5 Core；Vulkan 开发环境为 SDK 1.4.350.0，后端要求 Vulkan 1.3，并启用 Khronos 验证层与同步校验。

| 配置 | 结果 |
| --- | --- |
| Debug + GPU 测试 | 两种后端、全部扩展与示例开启，三十项 CTest 全部通过，包含真实 Vulkan 呈现及跨后端图像对比 |
| CPU 用例 | 16 个数学/场景/资源、15 个 Shader/图/导入/前端、8 个事件、7 个精灵、2 个动画、4 个交互、10 个音频，共 62 个 |
| 关闭 OpenGL | 只启用音频及 CPU 示例时八项检查；关闭全部扩展并保留 CPU 示例时六项检查，均通过 |
| Release | 全部十二个示例构建成功，三十项 CTest 全部通过 |
| Vulkan-only | 关闭 OpenGL、动画、交互和音频，独立构建并通过十三项 CTest，覆盖 CPU、五组 Vulkan 套件及四个示例检查 |
| 精灵性能矩阵 | Release 在 1,000/10,000 精灵 × 25/100/400 图块 × 双后端 × 两种线程下重复三轮，共 72 组；计数、CSV、GPU 样本和图形错误检查通过 |
| 跨后端一致性 | 同一快照、两种线程模式、缩放及透明/FXAA 组合共十二次比较；统计一致，每次平均 RGB 色值差小于 0.0001，阈值外像素为零 |
| 构建开关 | 仅库、仅示例、仅测试、示例与测试一起构建均通过；测试从 ON 改为 OFF 后 CTest 清单为空 |
| 格式 | 项目 C++ 源码与公共头文件通过 clang-format 检查 |
| 接口隔离 | 公共头文件不包含 OpenGL、Vulkan、GLFW、glslang 或 SPIRV-Cross 实现类型 |
| SDK 安装 | 完整 SDK 包内十二个示例独立构建，Release 十一项检查通过；性能工程另在双后端及 Vulkan-only SDK 中单独构建并通过两项/一项检查，无需 VULKAN_SDK |
| 核心 SDK | 关闭 OpenGL、自定义 include/lib 目录后外部工程构建与运行通过，必需 OpenGL 组件查询正确失败 |
| 头文件规范 | 36 个唯一 include guard，公共接口提供用途、参数和返回值中文说明 |

基础中文注释与文档更新后，四个套件及文档示例语法编译、UTF-8/本地链接和 C++ 格式检查通过。新增可选加权 OIT 后，四个套件再次全部通过；新增一个 CPU 合批用例，当前 CPU 用例总数为 30。

OIT GPU 覆盖交叉表面的实际绘制顺序反转、两层覆盖率、零/全 Alpha、不透明深度遮挡、两种透明模式来回切换、64 层高亮 HDR 累积及曝光、实例化、六个 Pass 的异步计时、缩放、空帧清除和专用渲染线程。像素断言基于实际回读尺寸，避免系统最小窗口尺寸或 DPI 导致固定坐标采样到物体外。

OIT 更新同时通过 Release 和 CPU-only 构建及各自两个 CPU 套件；Release 示例分别在单线程与专用渲染线程运行成功。30 帧导入 GLB 的 OIT 截图为 914 个可见实例、29 次绘制、15,687 个场景三角形、零 OpenGL 错误；比对应默认模式多一次 Resolve。文档示例经 MSVC C++20 语法检查通过。

Linux、其他 GPU 与驱动未在本次验证中实测；以上结果不能替代跨设备兼容性或性能测试。

2026-10-24 的事件订阅更新通过上述六个 Debug 套件、Release 和 CPU-only 构建。窗口测试确定性调用已安装的原生回调验证 16 种事件，并使用真实 resize；额外覆盖 Esc 关闭取消后长按不重复请求、窗口在回调中销毁，以及 Renderer 保留上下文时订阅立即失效。事件版 Release demo 运行 12 帧、113 个可见实例、27 次绘制，OpenGL 错误为零。窗口文档示例通过 MSVC C++20 语法检查，45 个 C++ 文件格式检查及 10 份文档的 42 个本地链接检查通过。

同日的头文件与 SDK 更新再次通过六个 Debug 套件、三个 Release CPU 套件及三个 CPU-only 套件。完整 SDK 安装后复制到带空格的新目录，直接使用包内独立示例，以 Debug 和 Release 分别验证场景、Shader 编译/转译、模型与图接口、真实 OpenGL 帧及回读、RHI 工厂和事件订阅；导出配置不引用原源码或构建目录。核心 SDK 使用 `include-sdk` / `lib-sdk` 自定义路径并独立验证，未携带 GLFW/GLAD/OpenGL 库。当前 46 个 C++ 文件的格式检查及 11 份文档本地链接检查通过。复现命令见 [SDK 安装与二次开发](SDK.md)。

详细二次开发手册更新后，20 段 C++ 片段在补齐文中约定的外部上下文后全部通过 MSVC C++20 编译检查。新增的三个独立示例从源码目录和重新安装 SDK 内的示例目录分别构建，并通过 Release 的三个真实 OpenGL CTest；额外运行模型导入场景并生成有效的 960×640 PPM 截图。49 个 C++ 文件格式检查、12 份文档 UTF-8 与本地链接检查通过。示例操作和预期结果见[二次开发手册](DEVELOPMENT_GUIDE.md)。

全屏更新通过七个 Debug 套件、三个 Release CPU 套件，以及重新安装 SDK 后的独立外部示例。全屏测试实际切换无边框/独占模式，覆盖窗口与最大化恢复、隐藏限制、参数/线程校验、平台解除独占后恢复，以及有待执行帧时的上下文保留和渲染回读。Release demo 分别以无边框和独占模式开启独立渲染线程运行四帧，OpenGL 错误均为零；全屏文档四段 C++ 示例通过编译。不同分辨率/刷新率和真实多显示器热插拔未在本次自动测试中执行，详见[全屏说明](FULLSCREEN.md)。

可选 FXAA 更新通过八个 Debug 套件、三个 Release CPU 套件及三个 CPU-only 套件。新测试在单线程/专用渲染线程分别核对斜边与独立 8×8 几何覆盖参考、平坦颜色编码、开关后图像精确恢复、排序 Alpha/OIT、HDR/ToneMap、七个 Pass 的旧帧计时与缩放。256×192 三角形用例的感知颜色 MSE 从 55.0931 降为 11.5096，仅用于该场景的边缘验证。Release 示例以 OIT + FXAA + 渲染线程运行 12 帧，77 个可见实例、28 次绘制、零 OpenGL 错误。安装后的 SDK 使用者开启两者组合，三个开发示例也构建运行通过。接口和成本见[抗锯齿说明](ANTI_ALIASING.md)。

## 测试入口与覆盖

Sprite 更新通过十个 Debug 套件、四个 Release CPU 套件和四个 CPU-only 套件。新增 CPU 验证图集元数据、UV/tint 合批、裁剪翻转、视图独立的 Billboard、排序组、OIT 队列、4/8 方向动画与跨循环事件、透视/正交地面投影、Mask/Alpha 拾取及资源独立快照。GPU 测试在单线程和专用渲染线程核对实际像素、落地点排序、OIT 保序/FollowView、FXAA、相机旋转、Mask/阴影、资源销毁后提交及缩放。

Release 森林示例以 `--hidden --frames 60 --threaded --oit --self-test` 运行，快照中有 142 个精灵，139 个可见场景实例，2 次场景绘制，4 次脚步事件，OpenGL 错误为零；截图人工检查图片轮廓、地面阴影、角色与帽子。该绘制数只对应这个共用图集与材质的示例，不能替代复杂素材的性能测试。

安装到 `build/sprite-sdk` 后，完全从包内源码构建四个开发示例和 SDK 使用者并运行 CTest，全部通过；新专章的五段 C++ 代码在补齐文中外部上下文后编译通过。引擎使用 VS 2022 / MSVC 19.44，此次独立 SDK 工程使用本机默认 VS 2026 / MSVC 19.51 构建。功能边界与复现命令见 [Sprite 说明](SPRITES.md)。

Sprite 首次接入时通过 61 个自有 C++ 文件的 clang-format、28 个唯一头文件保护宏、15 份 UTF-8 文档及 105 个本地文档链接。

模块拆分后，通过十二个 Debug 套件、六个 Release CPU 套件；核心、仅动画、仅交互三个 CPU-only 配置分别通过四/五/五个套件。各配置独立安装 SDK，再仅使用包内公共接口构建 CPU 使用者并运行通过。关闭的扩展不安装头文件和库，REQUIRED 请求缺失的 animation/interaction 均正确配置失败。符号检查确认 renderer_core 与 renderer_opengl 库没有 SpriteAnimator、SpriteAnimationClip 或 SceneQuery 实现。

完整 SDK 安装到 `build/modules-sdk-full`，包内四个开发示例、原 SDK 使用者及新增模块使用者均独立构建并通过 CTest。新增只读视图保留快照与纹理引用；交互筛选测试确认不会改变渲染结果。此次 SDK 验证使用 VS 2022 / MSVC 19.44；模块边界与复现命令见[模块说明](MODULES.md)。

模块和精灵文档的六段 C++ 示例及旧头文件兼容转发均编译通过。最终 69 个 C++ 文件格式、32 个唯一头文件保护宏、16 份 UTF-8 文档和 116 个本地链接检查通过。

示例与测试目录整理后，根工程直接构建全部八个示例，无需先安装 SDK；Debug 的十八项 CTest、Release 的七项 CTest 全部通过。在关闭 OpenGL、动画和交互的配置下，分别验证仅库、仅示例、仅测试、两者同时开启：对应零个、两个、零个、两个示例目标，以及零个、零个、四个、四个单元测试目标。仅测试的四项和同时开启的六项 CTest 均通过；随后关闭测试，即使 GL 测试缓存为 ON，也没有测试目标或残留 CTest 条目。

完整 Release SDK 安装到 `build/examples-sdk` 后，从包内聚合 examples 入口独立构建八个示例并通过六项 CTest；development 子目录独立构建通过四项 CTest。将 sdk_consumer 单独复制到不含测试脚本的外部目录后，通过 SDK 配置定位脚本，构建及一项 CTest 通过；关闭测试或关闭示例后清单为空。开关与目录说明见[示例](../examples/README.md)和[测试](../tests/README.md)。

此次更新的 69 个 C++ 文件格式、32 个唯一头文件保护宏、18 份 UTF-8 文档及 130 个本地链接检查通过。

音频模块更新新增十个无声卡的真实混音用例，检查 PCM/立体声与重采样、叠加和分组增益、静音恢复、暂停后的进度、淡变、左右空间方向、监听器与距离、优先级/并发限制、资源与旧句柄、移动与线程归属、中文路径 WAV 解码、流式补页和循环、交叉淡变及源文件消失。首次九个用例及示例加入后完整 Debug 的二十项检查通过；补充长流补页/循环用例后音频测试再次通过。Release 九项检查通过，默认系统设备和中文路径音乐流示例正常启动并退出。音频 API 的 WAV 路径已经实测；MP3/FLAC 通过内置解码器提供，本次未另行准备格式样本。

无 OpenGL、无动画/交互但启用音频时，八项 CPU 检查通过；关闭音频后六项检查通过，旧音频测试不再注册。完整 Release SDK 安装到 `build/audio-sdk`，无 OpenGL 的 Debug 音频 SDK 安装到 `build/audio-sdk-cpu`；仅使用各自包内 `examples/audio` 独立构建及运行通过，无需第三方开发头文件或单独音频库。`build/audio-disabled-sdk` 确认没有音频头文件、库与导出目标，请求 REQUIRED audio 正确配置失败。详情见[音频说明](AUDIO.md)。

音频更新后的 73 个 C++ 文件格式、33 个唯一头文件保护宏、19 份 UTF-8 文档及 147 个本地链接检查通过；固定版本 miniaudio 文件哈希与许可原文也已核对。

Vulkan 更新将高层渲染管线和窗口平台提取为内部共用库，对外新增 PImpl 工厂，未暴露 Vulkan/GLFW 类型。Debug/Release 各二十八项真实检查通过，Vulkan 验证错误为零。跨后端十二次比较覆盖专用线程、缩放、排序 Alpha/OIT 及 FXAA，同一快照的统计相同，最大平均 RGB 色值差为 0.0000813802，阈值外像素为零；这些结果只代表本机 GPU 和测试场景。

完整 SDK 的 Debug/Release 各安装十五个静态库；Vulkan-only 且关闭扩展的 Debug 包为十个库，没有 OpenGL/GLAD。清除 `VULKAN_SDK` 后，包内聚合示例独立构建，完整包九项、精简包三项检查通过；导出文件未携带引擎源码或 Vulkan SDK 的绝对路径。新后端及共用平台/管线、GLAD/Volk 的七个内部目标均位于 VS 的 core 文件夹。最后检查八十个 C++ 文件格式、三十五个唯一包含保护宏、二十份 UTF-8 文档和一百六十四个本地链接，以及 Volk 固定哈希和许可副本。

精灵性能示例更新后，Debug/Release 各三十项 CTest 全部通过。新增两项示例检查各覆盖两个精灵数量、两种图集规模和两种线程，启用校验并核对真实回读图像。共享图集提取后，原 OpenGL/Vulkan 森林示例继续通过。非方形的 26 块图集、1024 块上限、单精灵、零预热/单帧采样、静态及关闭 FXAA 共十六组边界检查通过；两种 OIT 策略各四组检查通过，九种非法参数及 Vulkan-only 包请求 OpenGL 均正确拒绝。

完整 Release SDK 和关闭 OpenGL/扩展的 Debug SDK 均安装共用图集头文件和独立性能工程。清除 VULKAN_SDK 后，从各自包内构建并运行：双后端包两项、Vulkan-only 包一项检查通过。VS 的性能示例目标位于 examples 文件夹。格式和文档检查覆盖八十二个 C++ 文件、三十六个唯一保护宏、二十二份 UTF-8 文档及一百七十九个本地链接。测试场景和计时口径见[精灵性能测试](SPRITE_BENCHMARK.md)。

| CTest 名称 | 源码 | 主要内容 |
| --- | --- | --- |
| `renderer.core` | `tests/core_tests.cpp` | 数学、句柄归属/代际、层级、变换、组件、资源与快照生命周期 |
| `renderer.engine` | `tests/engine_tests.cpp` | Shader 编译/反射/缓存、图编译校验、模型图片导入、裁剪、透明排序、阴影投影者、正交相机 |
| `renderer.events` | `tests/event_tests.cpp` | 订阅顺序、数据副本、RAII/显式取消、移动、新增订阅、异常、重入和跨线程取消 |
| `renderer.sprites` | `tests/sprite_tests.cpp` | 元数据、合批、裁剪/翻转、Billboard、排序组和只读视图快照 |
| `renderer.animation` | `tests/animation_tests.cpp` | 方向序列帧、跨循环事件、速度、暂停、结束状态和无事件追帧 |
| `renderer.interaction` | `tests/interaction_tests.cpp` | Alpha/Mask 拾取、筛选、透视/正交地面求交和资源生命周期 |
| `renderer.audio` | `tests/audio_tests.cpp` | 无声卡实际混音、音乐流补页/循环、包络、空间定位、实例与资源生命周期 |
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

根工程开启示例和测试后，还注册以下运行检查。启用 OpenGL 时前五项需要 `RENDERER_GL_TESTS=ON`，模块使用者和音频示例为 CPU 检查；关闭 OpenGL 后，SDK 使用者也作为 CPU 检查注册。它们也可以通过独立配置对应示例目录运行，注册逻辑统一位于 `tests/example_tests.cmake`：

| CTest 名称 | 验证内容 |
| --- | --- |
| `development.scene` | 隐藏窗口运行四帧，覆盖纹理/PBR、透明、相机姿态和专用渲染线程 |
| `development.compute` | Compute 写入 16 个整数，显式屏障、GPU 拷贝、完成等待及 CPU 数值核对 |
| `development.graph` | 自定义图形 Pipeline、三阶段图、一个无用 Pass 剔除、重复执行、GPU 时间戳和图像中心像素 |
| `development.sprite` | 森林示例运行 24 帧，OIT/FXAA/专用渲染线程、方向动画、事件及地面投影自检 |
| `renderer.sdk.consumer` | 公共 SDK 接口、场景与资源、图形环境可用时的真实 OpenGL 提交和回读 |
| `renderer.sdk.modules` | CPU 公共接口及当前启用的动画/交互模块 |
| `renderer.audio.example` | 程序生成音频、离线实际混音、空间音效和两首音乐交叉切换 |
| `renderer.benchmark.opengl` / `renderer.benchmark.vulkan` | 精灵数量、图集种类与线程矩阵的功能检查，不设 FPS 门槛 |
| `renderer.vulkan.example` | 共用三维示例，以 Vulkan 隐藏窗口、专用线程、OIT 和 FXAA 运行 |
| `renderer.vulkan.forest` | 共用森林示例，以 Vulkan 运行方向动画和交互自检 |

高级 GPU 测试进一步覆盖：工作线程独立录制命令、逻辑组绑定映射、GPU 生成间接参数、图分配后 Device 移动、纹理视图底层存储存活、透明线性混合、Alpha Mask、HDR ToneMap、异步 Pass 计时、方向光接收阴影和 PBR 参数变化。

阴影回归使用同一 RenderView，仅切换 shadows，比较无遮挡接收面的输出以捕获自阴影条纹。线程测试覆盖有界 submit、工作线程呈现、窗口缩放，以及 Window 包装对象先释放时的上下文存活。

## 复现命令

GPU 测试通过 GLFW 窗口和独立 FBO 调用真实 OpenGL，需要可创建 4.5 Core 上下文的桌面环境。大多数使用隐藏窗口，全屏测试会短暂显示窗口并以串行方式切换当前桌面模式。标准 `windows` 预设在完整模块及示例开启时注册九项 CPU 检查，GPU 测试使用单独预设：

```powershell
cmake --preset windows-gpu-tests
cmake --build --preset windows-gpu-tests --parallel 6
ctest --preset windows-gpu-tests --verbose
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

- `sceneInstances` 是快照中的网格实例数量；`visibleInstances` 是相机层过滤与视锥测试后保留的数量，不是 submesh 数。
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
