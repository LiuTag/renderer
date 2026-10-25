# 异步资源加载与发布预算

[文档导航](README.md) · [实时渲染扩展](RENDERING_EXTENSIONS.md) · [内存与性能](DIAGNOSTICS.md)

`AssetLoader` 位于 core，使用 PImpl。图片和静态模型在固定后台线程读取/解码，每个任务拥有独立 CPU 仓库，不访问 Scene、应用仓库或图形设备。应用在线程所有者处调用 publish，将完整资源包零拷贝接收进自己的 AssetStore。地图的 TileStreamer 继续负责区块驻留与持久化，两者职责独立。

## 接入

```cpp
#include <renderer/asset_loader.hpp>
using namespace renderer;
AssetStore assets;
AssetLoader loader({.workers=2, .maxRequests=128,
                    .maxReadyBytes=128ull*1024*1024});
auto request = loader.loadTexture("assets/tree.png", true, 10);
auto modelRequest = loader.loadModel("assets/house.glb");

// 每次应用更新调用；不等待后台任务完成。
for (auto &result : loader.publish(assets, {.maxRequests=4,
                                          .maxBytes=16ull*1024*1024})) {
    if (result.info.state == AssetLoadState::Published) {
        // 图片使用 result.texture；模型使用 result.model.instantiate(scene, parent)。
        // 保存 result.package，离开场景时可以完整卸载这一包。
    } else if (result.info.state == AssetLoadState::Failed) {
        // 记录 result.info.error，需要重试时发起一个新请求。
    }
    loader.forget(result.info.id);
}
```

Queued → Loading → Ready → Published。取消进入 Cancelled；读取、解码、结果容量不足或发布异常进入 Failed。Ready 仅表示 CPU 数据就绪，不能据此认为 GPU 已驻留。status 返回副本，statistics 提供队列、运行任务和 Ready 内存统计。

优先级越大，尚未开始的任务越先执行；同优先级按请求先后。它不抢占正在进行的解码，也不控制 GPU 上传顺序。取消会立即禁止旧结果发布，正在进行的磁盘读取/解码会自行结束。析构等待工作线程退出。

publish 对终态事件只交付一次。maxRequests 包含未 forget 的终态记录，消费后应及时 forget；无法 forget 尚未消费的请求。默认两个工作线程，最大十六个。所有控制接口集中在创建线程，不允许从资源回调或其他线程同时改写目标仓库。

## 两级预算

1. `AssetPublishBudget` 限制每次 publish 的资源包数量和 CPU 网格/像素载荷。为了避免大包永远无法发布，第一个包允许超过字节预算，但仍独占本次的大包发布额度。
2. `RendererConfig::assetUploadBudgetBytes` 限制每次 render/submit/renderToTexture 中新网格、纹理和材质 uniform 的上传量，默认 16 MiB，0 表示不限。未准备完整的批次暂时跳过，随后继续。应用可以在加载阶段保留占位物体，并通过 pendingResourceDraws 判断当前视图是否仍有等待批次。

GPU 上传以完整网格、纹理或材质为原子单元；单个超大资源允许独占一次上传。因此这是带前进保证的软字节预算，不是严格帧耗时上限。Shader 首次编译、mip 生成、驱动分配仍可能耗时。稳定帧的实例/uniform/覆盖层更新不属于新资源预算，单独计入 uploadedBytes。

默认将已选资源合并为异步传输批次，不逐层/mip 等待。不可变局部纹理版本在父页驻留时按实际变化区域预算，GPU 复制整页仍有额外成本；详见[优化说明](OPTIMIZATION.md)。卸载场景时可调用 RenderExtractor::clearCache 回收提取器保留的根，已发布快照继续拥有资源。

maxReadyBytes 是已解码、等待发布结果的总上限，超限任务报告失败，不无限积累。工作线程正在读取/解码的暂存、模型解析中间结构、应用持有的旧事件和快照不计入该上限。图片编码文件最大 256 MiB，每边最大 16384；GPU 还受设备限制。Unicode 路径使用 filesystem::path。

## 资源包卸载

```cpp
// 先移除引用这一包句柄的场景节点，再卸载资源。
scene.destroySubtree(instance.root);
assets.releasePackage(packageId);
```

包内句柄立即失效，已提取的快照继续持有原资源；GPU 缓存在资源不再被引用后通过原有延迟回收路径释放。Model 模板仍可被复制，但卸载包后不能继续用其旧句柄提取场景。

`AssetStore::adopt(std::move(otherStore))` 也可用于应用自己的工作线程流程，返回包身份。成功后源仓库成为一个新的空仓库，已有句柄不变。一个包可以包含嵌套接收的数据，releasePackage 卸载整包；只接受当前仓库直接接收的包身份。

CPU 解码完成并发布不等于应用应该自动替换物体、重试失败任务或卸载旧区块。这些策略仍由应用/地图层决定。[features 示例](../examples/features/README.md)演示占位物体、异步图片和风摆材质的实际接入。
