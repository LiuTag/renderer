# 内存统计、调试面板与实例增量上传

[文档导航](README.md) · [资源加载](ASSET_LOADING.md) · [精灵性能测试](SPRITE_BENCHMARK.md)

## 内存与上传统计

```cpp
const auto cpu = assets.memoryStatistics();
const auto gpu = renderer.memoryStatistics();
ui.setDebugPanel(lastStats, gpu, cpu, showDiagnostics);
```

AssetMemoryStats 区分当前有效句柄和仍被材质/精灵/快照保留的网格/纹理。meshBytes 为顶点与索引载荷；textureBytes 为当前仍存活的 pixels/data 载荷，压缩资源按块字节计算。销毁句柄或 releasePackage 后，旧快照未释放时内存不会立即归零。实时 GPU 纹理没有 CPU 像素，不在 textureBytes 中虚构一份像素副本。

RendererMemoryStats 提供缓存网格、纹理（含 mip 和数组层）、材质 uniform、帧缓冲、渲染附件和 CPU 实例镜像估算。estimatedGpuBytes 汇总受管理 GPU 载荷；这是引擎估算，不是显卡驱动实际 committed/resident 显存。分配对齐、驱动对象、Pipeline/描述符和待延迟回收的原生对象没有完整计入。memoryStatistics 按渲染队列顺序读取，不会隐式 waitIdle。

RenderStats 的增量字段：

| 字段 | 含义 |
| --- | --- |
| assetUploadedBytes | 本次新资源的 CPU 上传字节，不含生成 mip 的 GPU 内部读写 |
| instanceUploadedBytes | 实际更新到实例缓冲的字节 |
| instanceReusedBytes | 当前所需实例中已与帧槽副本一致的字节 |
| instanceUploadRanges | 更新的连续实例范围数量 |
| pendingResourceDraws | 因资源预算尚未完整准备、本次延后的场景/阴影/覆盖层命令 |
| uploadedBytes | 包括资源、变化实例、帧 uniform、灯光和覆盖层在内的本次总上传 |

pendingResourceDraws 是当前视图的绘制命令数量，不能当作独立资源数量，也不代表解码请求队列。GPU Pass 耗时仍来自完成的旧帧，不应与当前 CPU 统计硬配为同一帧。

## 调试面板

ui.setDebugPanel 创建可反复更新的只读 RML 面板，展示 CPU/GPU 耗时、实例、绘制调用、上传/复用、等待批次及内存。它使用 LatoLatin 和已加载回退，不截获鼠标，不夺取编辑焦点；可通过返回的文档身份调整位置/皮肤，visible=false 隐藏。

面板属于 renderer::ui；核心仅提供数据，无 UI 的程序可以写入日志、CSV 或自己的诊断工具。每帧更新面板本身仍会产生 UI 几何上传。调试统计和图形验证会增加开销，性能对比应保持配置一致。

## 实例缓存如何工作

默认开启 RendererConfig::incrementalInstanceUploads。每个 GPU 帧槽保存本槽实例缓冲的 CPU 镜像；复用前等待该槽完成，随后比较完整实例记录，只合并并上传变化的连续范围。没变化的记录不重复复制到镜像或 GPU。

因此，一个静态视图经过 framesInFlight 次预热后，instanceUploadedBytes 可降到 0。移动一个物体时，只更新与它相关的记录；阴影实例、角色部件也分别计算。改变颜色、UV、排序或相机 Billboard 方向同样会产生变化。

该缓存以整理后的绘制数组位置为单位。视锥进出、透明重排、批次增删、多视图交错复用同一帧槽等情况会改变数组位置，可能更新更大范围；它不会为了少上传而破坏透明顺序。CPU 的场景检查、快照提取、裁剪和合批仍会执行，不代表静态场景零 CPU 成本。

内置材质效果的时间在帧 uniform 中，树木随风摆动不要求应用逐棵修改模型矩阵。可以关闭 incrementalInstanceUploads 做对照；判断收益时同时看整帧耗时和上传字节，不能仅看 Draw Call。

验证代码见 asset_loader_tests、render_upgrade_tests、ui_text_upgrade_tests，以及[功能综合示例](../examples/features/README.md)。

压缩纹理的 textureBytes 按实际格式、mip 与数组层计算；compressedTextureBytes 是其 BC 子集，textureArrayLayers 是缓存数组对象的总层数。设备不支持或强制降级时统计 RGBA8 实际上传/存储字节，不能把 DDS 文件大小直接当显存。字形缓存的命中/未命中、淘汰字形/页面可通过 TextStats 查询；统计口径与旧快照保留见[文字文档](TEXT.md)。

RenderStats 新增 uploadCalls/uploadSubmissions/uploadWaits/stagingAllocations/stagingReuses，记录本帧资源传输；普通绘制提交、帧槽等待和主动回读不计入。RendererMemoryStats::pooledUploadBytes 是受管理暂存池载荷，并计入 estimatedGpuBytes，可能位于系统或设备可见内存，不包含独立/待回收暂存峰值。Device::uploadStatistics 可读累计统计。详见[优化文档](OPTIMIZATION.md)。

## Vulkan 提交缓存与分配器

`rhi::Device::statistics()` 返回不含原生类型的 DeviceStatistics，字段及缓存上限见[优化说明](OPTIMIZATION.md)。它不等待 GPU；pendingSubmissions 表示尚未回收的提交记录，created/reused 是设备生命周期累计值。当前 OpenGL 的 available=false，零值表示该诊断不可用。

高层 `Renderer::memoryStatistics()` 提供 nativeMemoryDetailsAvailable、nativeMemoryBlocks/Allocations 和 nativeMemoryBlockBytes/AllocationBytes。它们分别是 VMA 管理的存储块数量/存活分配数、总块字节/存活分配字节，包含仍被在途工作保留的资源和分配对齐。空闲块、暂存保留也计入块字节，因此释放资源后块字节不保证归零。它不是完整驱动驻留显存，不与 estimatedGpuBytes 相加，避免重复计算。

VMA 实现编入 Vulkan 库，查询无需使用者安装 VMA 或链接其他库；升级 SDK 后需使用同套头文件与库重新编译应用。
