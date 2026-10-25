# 渲染优化性能矩阵

[示例索引](../README.md) · [优化实现](../../docs/OPTIMIZATION.md) · [原精灵性能示例](../../docs/SPRITE_BENCHMARK.md)

本机两轮对照、上传量和 48 对终帧截图的检查记录见[结果报告](../../docs/PERFORMANCE_RESULTS.md)。

renderer_performance 使用公开 core/text/interaction 和已启用的后端，位于 VS examples 分组；RENDERER_BUILD_EXAMPLES 控制构建。默认不创建应用窗口，Windows 测试 OpenGL/Vulkan 两后端及两种渲染线程模式。

## 六种工作负载

| 名称 | 固定场景 |
| --- | --- |
| static | 节点精灵不变化，视口只覆盖世界的一部分 |
| dynamic | 每帧更新约 5% 节点变换 |
| batch | 相同布局通过每组 128 个精灵的不可变批次发布 |
| stream | 每八帧用新的八层 BC3 数组及完整 mip 替换资源，保留最近三份快照 |
| text | 24 个 SDF 标签，每帧改变数字和从 1600 个 CJK 字符中选择的字符 |
| oit | 相同布局改为半透明 FollowView 加权 OIT |

所有场景每八帧还会建立 SceneQuery 并执行 32 次拾取。默认 5000 精灵、960×640、关闭阴影/FXAA/图形调试，随机性由固定布局/字符公式消除。stream 模拟资源发布和 GPU 流入，不包含磁盘/网络 IO；text 有意持续产生缓存未命中字形，体现最坏情况下 SDF 栅格化与图集更新。字体优先 --font，Windows 可找到 msyh.ttc 时使用，否则使用 SDK 示例字体，后者不含完整中文；环境记录会注明实际字体，不随 SDK 分发系统字体。

## 构建和对照

```powershell
cmake --build build/windows --config Release --target renderer_performance
build/windows/Release/renderer_performance.exe --count 5000 --warmup 16 --frames 80 --repeats 2 --reference --csv build/windows/benchmarks/reference.csv
build/windows/Release/renderer_performance.exe --count 5000 --warmup 16 --frames 80 --repeats 2 --csv build/windows/benchmarks/optimized.csv --capture build/windows/benchmarks/optimized
```

--reference 关闭上传批次/暂存池、场景根复用/空间索引、R8 和局部图集更新。其余公共渲染功能和当前版本保持一致，因此它是同版本功能开关对照，不冒充旧版本二进制。优化前在改代码前采样的原始 CSV 可作为额外历史记录。

参数：--backend all/opengl/vulkan，--threads all/single/render，--workload all/static/dynamic/batch/stream/text/oit，--count，--warmup，--frames，--repeats，--font，--csv，--capture 和 --self-test。每个组合独立创建渲染器/场景/缓存，最多 20 轮；正式性能采样应使用 Release 且避免同时跑其他 GPU 工作。OpenGL 离屏沿用 Windows WGL pbuffer 限制，其他平台应选择可用后端；详见[离屏文档](../../docs/OFFSCREEN.md)。

## CSV 口径

- frame_mean_ms / frame_p95_ms / frame_p99_ms：更新、文字批次、提取、抽样拾取和 render 调用的 CPU 帧耗时；没有在每帧截图或等待 GPU 排空。
- cold_frame_ms：第一次帧调用，包含其资源/管线首次使用；不包含渲染器构造与初始场景构建。
- update_ms / extract_ms / prepare_ms / submit_ms / pick_32_queries_ms：阶段均值，渲染线程模式存在重叠，不能相加作为帧耗时。
- gpu_mean_ms / gpu_samples：完成旧帧的图内 GPU 时间；不是跨后端完整呈现时间。本示例无交换链呈现。
- sample_wall_ms / fps / gpu_drain_ms：正式采样墙钟、由该墙钟计算的吞吐，以及末尾 GPU 排空时间；墙钟还包括内存统计和快照保留管理。
- asset_uploaded_bytes / upload_calls / upload_submissions / upload_waits：正式采样累计资源字节及传输操作，提交/等待不包含绘制和帧槽等待。
- staging_allocations / staging_reuses / peak_staging_bytes：暂存分配、复用及受管理池峰值，可能位于系统内存或设备可见内存。
- peak_gpu_payload_bytes：受管理载荷估算（包含暂存池），不是驱动实际显存；text_atlas_bytes / text_dirty_bytes / text_page_revisions / text_misses 记录字形缓存。

同名 .environment.txt 保存设备、驱动/API、字体、预热帧、构建类型和参考开关。--capture 在采样结束后保存每个组合 PNG。--self-test 检查实例、非空输出和图形错误，没有机器相关的 FPS 合格线。

## SDK 与自动检查

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/performance -B build/performance-sdk -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/performance-sdk --config Release
ctest --test-dir build/performance-sdk -C Release --output-on-failure
```

源码和 SDK 共用 tests/example_tests.cmake。两项 renderer.performance.opengl/vulkan 每项运行两种线程及全部六场景，500 精灵、3 帧预热和 12 帧检查，串行执行并写 CSV；缺少 text/interaction/后端时根工程跳过本示例。SDK 无需 Vulkan SDK、第三方头文件或下载素材。
