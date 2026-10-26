# 测试

单元与集成测试源码、测试资产及 CMake 入口全部位于源码的 `tests/`。`CMakeLists.txt` 定义测试可执行程序，`example_tests.cmake` 统一注册示例运行检查。

生成 Visual Studio 解决方案时，测试可执行项目统一显示在 `tests` 解决方案文件夹中；用于运行检查的示例项目仍位于 `examples`。此分组不改变可执行文件的输出位置。

新增 `image_tests.cpp` 验证无需图形设备的 PNG 编码；`offscreen_tests.cpp` 分别链接 OpenGL/Vulkan，在不初始化 GLFW 的独立进程验证纹理渲染、行序/颜色、奇数尺寸、队列/线程、缩放、OIT/FXAA 和原生窗口约束。森林离屏示例也注册输出 PNG 的自检；命令与平台边界见[离屏说明](../docs/OFFSCREEN.md)。

`camera_tests.cpp` 随 `RENDERER_BUILD_CAMERA` 注册 `renderer.camera`，覆盖三种模式、坐标轴、平滑和时间分段、缩放/俯仰限制、传送和模式切换、父节点缩放/剪切、镜头保留及无效输入原子性。`renderer.camera.example` 通过公开 SDK 运行，无需窗口或图形驱动，见[相机控制](../docs/CAMERA.md)。

`overlay_tests.cpp` 随 core 验证通用覆盖层数据与资源引用。`ui_tests.cpp` 随 ui 验证真实控件、文本、焦点/拖动、DPI、事件/取消/异常和文档/线程身份；`ui_render_tests.cpp` 分别使用两种后端回读，检查透明顺序、线性预乘 Alpha/sRGB、矩形裁剪、非对称图片方向、字体、渲染线程、快照存活和后处理合成。四项独立 UI 示例检查覆盖窗口/离屏与两个后端，详见[UI 文档](../docs/UI.md)。

| 根工程开关 | 默认值 | 含义 |
| --- | --- | --- |
| `RENDERER_BUILD_TESTS` | ON | 所有测试的总开关；OFF 时不编译测试程序、不注册 CTest |
| `RENDERER_GL_TESTS` | OFF | 总开关开启时，增加窗口/OpenGL 测试及需要图形环境的示例检查 |
| `RENDERER_VULKAN_TESTS` | OFF | 总开关开启且 Vulkan 后端启用时，增加 Vulkan GPU/窗口/示例检查；两种后端都有时增加图像对比 |
| `RENDERER_BUILD_EXAMPLES` | ON | 示例编译开关；关闭时只保留单元/集成测试，不注册示例检查 |

## 编译测试

世界文字的 renderer.text CPU 套件覆盖 UTF-8/Unicode 字体、回退、排版、图集/容量、绑定/隐藏/销毁、屏幕字号与资源生命周期；renderer.text.opengl / renderer.text.vulkan 真实回读遮挡、深度开关、OIT/FXAA、字号、视口缩放与旧帧。四项文字示例检查覆盖窗口/离屏和两个后端，见[世界文字](../docs/TEXT.md)。interaction 同时验证无深度精灵的拾取优先级。

从源码根目录执行：

```powershell
# 只编译 CPU 测试，不编译示例。
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_TESTS=ON -DRENDERER_GL_TESTS=OFF -DRENDERER_VULKAN_TESTS=OFF
cmake --build --preset windows --parallel 6
ctest --preset windows

# 示例、单元测试及真实 OpenGL 检查一起构建。
cmake --preset windows-gpu-tests -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_TESTS=ON -DRENDERER_GL_TESTS=ON
cmake --build --preset windows-gpu-tests --parallel 6
ctest --preset windows-gpu-tests
```

仅构建库：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=OFF -DRENDERER_BUILD_TESTS=OFF
cmake --build --preset windows-release --parallel 6
```

同时检查两种后端：

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build --preset windows --parallel 6
ctest --preset windows --output-on-failure
```

`RENDERER_BUILD_TESTS=OFF` 优先于两个 GPU 开关，即使旧缓存中 GPU 测试为 ON，也不会编译或注册任何测试；重新配置会清空旧测试清单。关闭测试不会删除已经生成的可执行文件，它们不再属于当前构建目标。

完整双后端配置包含 25 个 CPU 套件、14 个 OpenGL/窗口套件、14 个 Vulkan 套件及 1 个跨后端对比，共 54 个测试本体。开启全部示例后再注册 40 项示例运行检查，总计 94 项 CTest。仅核心且关闭所有扩展/后端/示例时有 10 项 CPU 检查。可用数量随构建开关变化，以 `ctest -N` 为准。

`renderer.game.{opengl,vulkan}.{window,offscreen}` 四项检查运行综合应用 240 帧，验证图像、推箱子与事件、方向动画/脚步、流式编辑保存/传送、音频采样和 PNG/WAV 输出。全部使用离线音频与独立缓存，窗口模式采用渲染线程、离屏模式采用当前线程，详见[综合示例](../examples/game/README.md)。

窗口图形检查需要驱动和桌面环境，全屏和性能示例检查串行执行。离屏检查也需要 GPU 驱动，其中 Vulkan 不需要显示服务，Windows OpenGL 使用 WGL pbuffer。

`graphics_fixture.hpp` 让四组底层/高级/FXAA/Sprite 测试复用同一份断言，按编译宏创建不同后端。`vulkan_window_tests.cpp` 验证交换链、回读、缩放、全屏和专用线程；`backend_parity_tests.cpp` 对同一快照的绘制统计和像素进行比较。Vulkan-only 预设及验证层行为见 [Vulkan 说明](../docs/VULKAN.md)。

音频模块关闭时不注册 `renderer.audio` 或 `renderer.audio.example`；启用后它们不依赖 `RENDERER_GL_TESTS`，运行示例时显式传入 `--offline`。覆盖与线程约定见[音频说明](../docs/AUDIO.md)。

性能示例检查为 `renderer.benchmark.opengl` / `renderer.benchmark.vulkan`，每项覆盖两个数量、两种图集规模和两种线程，核对真实图像、可见精灵数量及错误。它们串行运行且不设置 FPS 门槛，见[性能说明](../docs/SPRITE_BENCHMARK.md)。

SDK 安装示例及本目录的 README、示例注册脚本，不安装私有测试源码、测试资产或测试可执行文件。独立 SDK 示例支持同样的测试开关。详细范围见[验证与排错](../docs/VALIDATION.md)。

## 图片世界新增覆盖

`renderer.batch` 检查批次版本、资源保留、Fixed 精确界限及 Billboard 保守界限；tilemap/collision/particles/environment/presentation 各有独立 CPU 套件，随模块开关注册。interaction 增加无节点批量身份与排序/OIT 混合拾取检查。

`renderer.opengl.effects` / `renderer.vulkan.effects` 在单线程和专用线程真实回读颜色覆盖、内描边、调色及接触阴影；`renderer.world.opengl` / `renderer.world.vulkan` 运行完整模块场景，自动移动、切换 OIT/昼夜/天气并检查图形错误。四个 GPU 检查仍受相应后端测试开关控制。接口范围见[图片世界模块](../docs/WORLD_MODULES.md)。

## 流式加载

新增 `renderer.streaming` CPU 套件，随 tilemap 开关启用，检查后台读写/前台解析、发布和卸载预算、负坐标、滞回、快照存活、取消/析构、保存失败背压、重试、保存期间编辑、空块与 Unicode 区块文件完整性。world 双后端自检改用 60 帧流式模式，同步验证物件和碰撞体生命周期。

`renderer.physics2d` 验证真实 Box2D 响应、CCD/命中、事件、查询、关节、安全句柄和场景同步；`renderer.physics2d.example` 为无 GPU 的公开接口示例。world 检查会使用 Box2D，并随流式区块移除刚体。

## 渲染基础增强检查

renderer.asset_loader 覆盖 Unicode 后台图片/模型、取消/错误、CPU 发布预算和包卸载后的资源保留；renderer.ui_text.upgrade 覆盖中文组合/剪贴板、调试面板焦点和字形缓存。renderer.{opengl,vulkan}.upgrade 实际检查上传预算、静态/动态实例字节、材质/混合、实时纹理/跨设备/反馈。窗口测试包含 Windows 原生组合生命周期排队，UI GPU 测试包含存活于 UI 销毁之后的实时相机图片。features 四项示例检查两后端窗口/离屏。

压缩/数组与文字质量检查：renderer.texture 验证 BC 编解码、DDS/层/mip/Unicode/长度、颜色空间与 Alpha 正确过滤；renderer.{opengl,vulkan}.texture 验证真实层采样、视图/拷贝/Storage Image、合批、强制 RGBA 降级与内存。renderer.text.quality 检查规范字号复用、LRU/保护/严格容量和旧批次；renderer.{opengl,vulkan}.text_quality 检查 SDF 边缘/外描边及回收后的旧帧像素。interaction 与 ui_text.upgrade 包含压缩数组拾取/选层与 UI 字体回收。texture.pack 与 texture_text 的四项示例检查使用公开 SDK 接口。

优化专项：renderer.optimization.cpu 验证缓存身份/失效/清理、跨场景隔离、BVH/扫描及阴影结果一致、移动后重拟合、Alpha8/DDS 与不可变更新；renderer.optimization.opengl/vulkan 检查真实批量传输/无逐项等待、暂存复用、奇数 R8 区域与层、提交前销毁父句柄、图集局部上传及旧快照逐像素保留。两项 performance 示例检查运行双线程和六负载，不设机器相关 FPS 门槛。

`renderer.vulkan.allocation` 无窗口验证 VMA 共享块/映射/图像视图、在途子分配释放，以及命令缓冲/Fence/描述符池的稳态和溢出复用、错误恢复与缓存上限；仅在 Vulkan 测试开启时构建，VS 归入 tests。
