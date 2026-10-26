# 上传、场景与文字的性能优化

[文档导航](README.md) · [新版性能基准](../examples/performance/README.md) · [内存统计](DIAGNOSTICS.md) · [文字](TEXT.md)

优化包括性能矩阵、批量异步上传、CPU 缓存与空间索引、字形图集更新，以及 Vulkan 提交对象复用和 VMA 显存子分配。两种后端使用同一套公共接口，服务类保持 PImpl；VMA 是仅 Vulkan 使用的私有依赖。

## 上传批次与暂存池

RendererConfig::batchResourceUploads 默认开启。每帧在资源预算检查后，将新网格、纹理各层/mip、材质与字形页面更新组合为传输批次，然后提交绘制。Vulkan 在同一队列记录初始化屏障、复制与 mip 生成，一次发布多项传输，CPU 不逐层等待。提交持有所有源/目标原生资源，GPU 完成后才回收；渲染线程和帧槽的同步规则不变。

Vulkan 批次使用持久映射暂存块，按 256 字节对齐分配不重叠范围；只有 GPU 已完成且没有待提交引用的块才能重置游标。池容量最多 64 MiB，大于 8 MiB 的块及超过池容量的传输使用独立暂存资源。资源的底层设备内存现由 VMA 子分配；暂存范围复用与设备内存块管理是两个层次。关闭批次时恢复逐项提交/等待和独立暂存，底层仍由 VMA 管理。

OpenGL 已有持久映射 Upload 缓冲，继续使用该路径。只有实际 GPU 资源传输需要批次完成标记；仅更新帧槽映射数据时复用后续绘制的完成标记。更新仍在设备线程进行，应用不能同时覆盖 GPU 使用中的映射范围。

```cpp
device.beginUploadBatch();
auto texture = device.createTexture(description);
for (uint32_t layer = 0; layer < layers; ++layer)
    device.upload(texture, layerPixels[layer], 0, layer);
auto ready = device.endUploadBatch();
// 后续同设备绘制有序，无需立刻 wait；确需 CPU 等待时再调用 device.wait(ready)。
```

批次不可嵌套，必须配对结束。提交命令、waitIdle 或同步回读会先发布待提交传输，但不自动结束批次作用域。上传返回前已复制 CPU 字节，调用者可复用原数组。跨设备/跨队列依赖不由此接口解决。高层异常路径也会结束已记录的有效传输，下一帧可继续使用设备；不承诺事务式回滚已经上传的资源。

Device::uploadStatistics 返回累计 calls/bytes/submissions/waits、stagingAllocations/stagingReuses/pooledStagingBytes。RenderStats 提供当前帧的 uploadCalls/uploadSubmissions/uploadWaits/stagingAllocations/stagingReuses；不包含普通绘制的提交次数、帧槽等待或主动截图回读。GPU 时间戳仍来自完成的旧帧。

## 场景缓存与空间索引

RenderExtractor 将场景节点根数据与外部 SpriteBatch 分开保存。场景版本、场景身份和资源句柄有效性版本未变化时，复用解析后的节点数组；动态文字/粒子换批次不复制全部场景节点。单个节点的固定朝向精灵几何和保守界限按句柄、世界版本及几何参数复用，改变颜色/排序不会重新求逆几何矩阵。

创建新的不可变资源不影响旧句柄，因此不使场景缓存失效。删除句柄、卸载/接收资源包会更新有效性版本。缓存失效时先释放旧根，再重新解析；即使解析发现失效句柄抛出异常，也不额外保留已卸载包。clearCache() 可在场景切换时显式回收提取器持有的根及派生数据；已经返回的快照继续拥有自己的资源。

场景根和批次建立相机无关 AABB BVH。Fixed 使用精确几何界限，Billboard 使用旋转不变保守界限，并计入材质位移余量。帧准备先做 BVH 粗裁剪，再为候选精灵计算实际相机几何和精确裁剪。查询结果恢复原始次序，后续透明排序、排序组和相邻合批规则不变。开启阴影时保留视锥外投影者的完整收集，不用主相机视锥错误地剔除它们。

SpriteViewSnapshot::rayCandidates 返回射线可能命中的排序后下标，SceneQuery 再执行原有平面/Alpha 检查。只读查询不修改共享索引，可并发使用；没有命中的对象也可能出现在候选列表，调用者不能把粗筛选直接当最终命中。

对照开关：RenderExtractor::setCachingEnabled(false) 关闭根复用，setSpatialIndexEnabled(false) 关闭场景索引构建，RenderView::spatialCulling=false 关闭该视图的空间查询优化。少量动态更新会共享未变化的固定精灵几何；节点顺序不变时复制并自底向上重拟合 BVH，保留旧快照的树，每 64 次提取或拓扑改变时重建。大量动态对象仍需构建新节点数组并更新空间界限；本轮没有把场景更新、透明排序迁移到 GPU，也没有通用工作线程池。

## Alpha8、R8 与不可变局部更新

TextSystemConfig::compactAtlas 默认开启。字形 CPU 页面仅保存 Alpha8 覆盖率，GPU 使用 R8；采样时还原白色 RGB 和覆盖率，SDF 阈值、颜色/描边及排序不变。相同页面尺寸下像素载荷为 RGBA8 的四分之一，实际分配仍受驱动粒度影响。compactAtlas=false 保留 RGBA8 参考路径；UI 的 RmlUi 字体缓存独立管理。

TextureDesc::channels=TextureChannels::Alpha8 要求线性、未压缩，pixels/data 每像素一字节。CPU decode 返回白 RGB/原 Alpha 的 RGBA8；DDS 使用 DX10 A8_UNORM 保存。高层基色采样处理覆盖率，RHI 的 Format::R8 则保持普通红通道语义，回读为 R/0/0/1。UiContext 导入此格式时仍转为自身的线性预乘 RGBA。

AssetStore::updateTexture(source, region, pixels) 发布独立的新版本，目前要求单层单 mip、关闭 mip 生成、未压缩的 CPU 纹理。源句柄和旧快照保持有效。区域坐标遵循源 origin，载荷紧密排列。TextSystem 合并新字形的脏矩形并使用此接口；incrementalAtlas=false 可对照完整页面发布。

新版本包含完整 CPU 数据和轻量父版本身份，不持有无限增长的父版本链。渲染器在父 GPU 页面仍驻留时复制整个旧页面，再上传变化区域；父页面不在缓存、发生跳版本或第一次显示时完整上传。它没有原地覆盖旧纹理，因此旧快照在更新、LRU 淘汰或 TextSystem 销毁后仍可绘制。局部上传预算按实际 CPU 区域字节计算，GPU 页面复制仍有成本。

R8 页面的变化减少存储/传输；新字符首次进行 FreeType SDF 栅格化的 CPU 成本仍存在。应在加载阶段通过 prewarm 预热常用字符，并结合 LRU 容量管理。大量每帧首次出现的生僻字是压力测试，不应把图集传输改善等同于消除全部排版/栅格化开销。

## 检查与边界

renderer.optimization.cpu 验证根复用、批次隔离、资源/跨场景失效、BVH/扫描与阴影结果等价、候选顺序、Alpha8/DDS 和局部更新。renderer.optimization.opengl/vulkan 验证奇数 R8 行、数组区域复制、批次无逐项等待、暂存复用、实际局部上传字节，以及新旧页面逐像素保留。完整回归仍覆盖 OIT、FXAA、材质/阴影、UI、线程和 SDK。

使用[性能基准](../examples/performance/README.md)的同版本 --reference 对照，以及保存的优化前 CSV，判断本机场景收益。reference 开关覆盖早期上传/场景/字形优化，不关闭当前 VMA 或提交上下文复用；历史性能报告也早于这两项，不作为其 FPS 改善证明。跨进程 Shader/Pipeline 缓存、高层 GPU 裁剪/间接绘制和 MSDF 仍是后续选项。

## Vulkan 提交对象复用

每个提交上下文持有一个独立命令池、一个主命令缓冲、一个 Fence，以及按需增长的描述符池列表。查询到 Fence 完成后，立即释放该提交持有的资源引用，回收获取图像用的 Semaphore；空闲缓存最多保留 8 个上下文，每个最多保留 4 个描述符池，超出部分销毁。此限制不限制低层 Device 可提交的在途工作数量。

复用时重置整个命令池、Fence 和保留的描述符池。每个描述符池容纳最多 256 个集合，四种描述符类型各预留 4096 个条目（保留原有单次布局的类型容量）；空间不足时选择下一个已重置的池或新建池。本次提交内相同资源组合继续缓存，跨提交重新写入描述符，不让空闲缓存保留已经卸载的资源。异常录制会销毁未提交上下文，后续提交可继续使用设备。

`rhi::Device::statistics()` 查询 `submissionContextsCreated/Reused`、`descriptorPoolsCreated/Reused`、`cachedSubmissionContexts`、`cachedDescriptorPools` 和 `pendingSubmissions`。累计 created/reused 不清零；pending 包含尚未通过 collect 回收的提交，可能已经执行完成，查询本身不等待 GPU。OpenGL 返回 available=false。

## Vulkan 显存子分配

固定版本 VMA 3.4.0 负责普通缓冲、纹理及暂存/回读缓冲的内存选择、子分配和映射。优选大堆块大小为 32 MiB，小堆和专用分配按实际要求处理；这个值不是分配器容量限制。共享块内的对齐、缓冲/图像粒度由 VMA 维护；CPU 映射地址已经定位到子分配起点，释放一个缓冲不会手工解除整个共享块的映射。

原有引用与提交完成规则不变：视图保留父图像，destroy 立即使句柄失效，实际子分配等待最后 GPU 用途结束后释放。Context 在全部原生资源释放后销毁 VMA，再销毁逻辑设备；不会在其他设备的函数表上执行释放。

设备统计还提供 `memoryBlocks/Allocations` 与 `memoryBlockBytes/AllocationBytes`。块字节包含空闲范围，分配字节包含资源对齐；包括仍在提交引用中的分配、暂存和回读资源。它们不包含所有驱动对象，也不等同于驱动驻留显存。查询基于 VMA 管理的堆统计，不要求 VK_EXT_memory_budget。当前没有自动整理/搬迁已绑定资源。

`renderer.vulkan.allocation` 专项验证共享块的 Buffer/Image 数据、相邻映射释放、父图像销毁后的视图、描述符池溢出复用、稳态上下文不再创建、错误录制恢复、在途资源删除及缓存上限；它不依赖 text/UI 或应用窗口。
