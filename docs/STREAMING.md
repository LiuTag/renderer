# 地图流式加载与持久化

[文档导航](README.md) · [图片世界](WORLD_MODULES.md) · [集成示例](../examples/world/README.md)

流式加载已加入可选的 `renderer::tilemap`，不增加第三方依赖或图形后端接口。`TileStreamer` 根据玩家世界位置，后台读取附近区块，再由调用 `update` 的逻辑线程解析图片、发布完整区块；超过卸载半径的区块按预算移除。支持生成器、自定义磁盘/网络读取和内置 RTile 文件帮助函数。

## 最小接入

链接 `renderer::tilemap` 并包含 `<renderer/tilemap/streaming.hpp>`。下面约定 assets、grass、road、scene、extractor 已由应用创建；两个图片句柄在逻辑线程解析，不能在读取回调中操作 Scene/AssetStore。

```cpp
#include <renderer/tilemap/streaming.hpp>
using namespace renderer;

TileStreamConfig config;
config.map = {.tileSize = 1, .chunkSize = 16};
config.loadRadius = 2;           // 周围 5×5 块是加载目标
config.unloadRadius = 3;         // 离开 7×7 范围才卸载
config.workerCount = 2;
config.maxPendingRequests = 8;
config.maxResidentChunks = 128;
config.maxCommitsPerUpdate = 2;
config.maxUnloadsPerUpdate = 2;

const std::filesystem::path directory = "world/chunks";
auto file = [directory](TileChunkCoord c) {
    return directory / (std::to_string(c.x) + "_" + std::to_string(c.z) + ".rtile");
};
TileStreamer stream(config, {
    .load = [file](TileChunkCoord c, std::stop_token stop) {
        if (std::filesystem::exists(file(c)))
            return readTileChunk(file(c), stop);
        TileChunkData data;
        data.chunkSize = 16;
        for (uint32_t z = 0; z < 16; ++z)
            for (uint32_t x = 0; x < 16; ++x)
                data.tiles.push_back({x, z, 1});
        return data;
    },
    .save = [file](TileChunkCoord c, const TileChunkData &data, std::stop_token stop) {
        writeTileChunk(file(c), data, stop);
    },
    .resolve = [grass, road](uint32_t type, AssetStore &) {
        if (type == 1) return grass;
        if (type == 2) return road;
        throw std::runtime_error("Unknown tile type");
    }
});

// 每帧在逻辑线程调用，不等待后台 I/O。
auto statistics = stream.update(playerWorldPosition, assets);
auto batches = stream.batches(assets);
auto snapshot = extractor.extract(scene, assets, batches);
```

`load`/`save` 可以被不同工作线程并发调用，捕获的共享缓存必须加锁。`resolve` 只在前台发布/编辑时调用，可使用 AssetStore 创建或查找缓存资源。它接收稳定的非零逻辑类型编号，文件不保存 owner/generation 句柄。多个类型可以映射到同一图集中的不同 Sprite。

区块是稀疏数组，局部坐标必须在 `[0,chunkSize)`、不得重复；空格从数组省略。`chunkSize` 必须与配置一致，范围 1～128；空数组是合法的空区块，仍计入驻留数量，避免每帧反复请求空地。

## 驻留、优先级与预算

- 观察点按 `(worldXZ - originXZ) / (tileSize * chunkSize)` 向下取整，正确处理负坐标。
- 加载目标使用切比雪夫半径，也就是以观察点区块为中心的正方形；优先调度/发布最近的块。
- `unloadRadius >= loadRadius` 形成滞回区，穿过边界后不会立刻卸载刚加载的相邻块。
- 前台每次最多发布 `maxCommitsPerUpdate` 个完整区块，最多卸载 `maxUnloadsPerUpdate` 个块。这是区块数量预算，不是严格的毫秒或 GPU 上传字节预算；块越大，单次发布成本越高。
- `maxPendingRequests` 限制队列与运行中的读取/保存任务总数。取消后尚未结束的请求仍占名额，快速传送不会产生无限后台队列。
- `maxResidentChunks` 同时计算驻留、读取中的预留位置和等待发布的数据，至少要容纳 `(2*unloadRadius+1)^2` 个块。保存失败的脏块会保留并施加容量背压，统计中的 `capacityLimited` 可用于诊断。

当观察点远离仍在读取的区块时，调度器请求取消并丢弃过期结果。即使回调没有及时响应，稍后返回的数据也不会覆盖新请求。数据源异常、校验失败和资源解析失败均产生 LoadFailed/SaveFailed 事件，不会每帧自动重试；修复原因后调用 `retry(coord)`。

文件读取、数据解析在后台；TileMap、SpriteBatch、场景节点和资源仓库的修改在逻辑线程；GPU 资源上传继续由现有渲染线程处理。这里没有把原生 OpenGL/Vulkan 调用移入读取线程。

## 编辑与保存

```cpp
stream.setTile(globalTileX, globalTileZ, 2, assets); // 修改道路
stream.setTile(globalTileX, globalTileZ, 0, assets); // 删除格子
stream.setPayload(chunkCoord, serializedObjectState);
stream.requestSave();
```

编辑仅接受已经驻留的区块，并要求存在 save 回调；只读源不能进行会在卸载时丢失的编辑。`setTile` 和 `setPayload` 使用不可变版本，旧快照、外部 chunk 引用及正在执行的保存保留旧内容。资源解析失败时不会发布半个区块。

远处脏块必须后台保存成功后才进入卸载队列。保存期间继续编辑会生成新版本，旧保存完成不能把新版本错误标记为干净。`requestSave` 允许仍在附近的区块主动保存；修改物件状态时应同步调用 `setPayload`，模块不会从 Scene 自动推断被砍掉的树、背包或动态刚体状态。

停止游戏前调用 `requestSave`，继续 update 和消费事件，直到 `dirtyChunks == 0 && pendingSaves == 0`。保存失败必须由应用处理或重试。析构只负责协作取消和等待回调退出，不隐式补做保存；读取/保存回调应响应 stop_token，阻塞系统调用无法由模块强行终止。示例在退出前有带超时的保存收尾。

## 物件与碰撞生命周期

```cpp
for (const auto &event : stream.takeEvents()) {
    switch (event.kind) {
    case TileStreamEventKind::Loaded:
        // 在逻辑线程解读 event.data->payload，创建树木、物件和碰撞体；按 event.chunk 建立映射。
        break;
    case TileStreamEventKind::Unloaded:
        // 按映射移除场景子树与碰撞体。已经提交的渲染快照仍保持有效。
        break;
    case TileStreamEventKind::LoadFailed:
    case TileStreamEventKind::SaveFailed:
        // 记录 event.message，修复后 stream.retry(event.chunk)。
        break;
    default:
        break;
    }
}
```

空块也有 Loaded/Unloaded 事件。事件数据是共享不可变引用，应用要及时消费；长期保留事件、chunk 引用或旧渲染快照会延长内存/资源生命周期，因此调度器容量限制不等于整个应用的内存上限。共享图集也不会随单个区块卸载立即释放。

当前 world 示例在区块事件中创建/移除一棵图片树及其圆形碰撞体，原中心森林作为固定演示区保留。实际项目可用 payload 保存完整物件列表，当前 Box2D 模块已使用相同事件创建/移除刚体。应用还应限制角色进入尚未就绪的区块，或采用更大的预取范围；流式调度器不自动创建封锁墙或修改玩家移动规则。

## 区块文件

`readTileChunk` / `writeTileChunk` 支持带 Unicode 的路径，格式包含 8 字节魔数/版本、little-endian 的块边长、格子数、payload 长度、每格 x/z/type、payload 和 32 位 FNV-1a 校验和。上限为 128×128 个格子、16 MiB payload；拒绝重复坐标、未知版本、错误长度、损坏/截断与尾随数据。校验和用于检测意外损坏，不是安全签名。

写入使用同目录临时文件，再重命名替换目标；失败清理临时文件，保留原文件。应用应确保同一路径只有一个写入者。该机制不提供数据库事务、跨区块原子提交或断电后的持久性保证；大世界存档版本迁移和跨区块对象归属仍由应用定义。

## 示例与验证

```powershell
& "C:/projects/chatgpt/renderer/build/windows/Release/renderer_world_demo.exe" --backend opengl --streaming
& "C:/projects/chatgpt/renderer/build/windows/Release/renderer_world_demo.exe" --backend vulkan --stream-cache build/windows/world-save
```

流式模式相机跟随角色，取消原演示区边界；T 切换脚下草地/道路并请求保存。`--streaming` 使用进程内演示保存器；加 `--stream-cache` 则将修改写入磁盘，下次从同一目录恢复。只有编辑过的区块需要写盘，其他块由确定性生成器重建。

CPU 测试 `renderer.streaming` 覆盖线程边界、预算、负坐标、滞回、旧快照、取消/析构、保存失败与背压、重试、编辑版本、空块、Unicode 文件与损坏校验。两种后端的 world 检查启用 `--streaming --self-test --frames 60`，覆盖移动、传送后卸载/重新加载、编辑保存和场景/碰撞生命周期。

## 这个游戏的物理库选择

目前设想是图片角色在地面移动，主要交互是树木阻挡、推挤、击退与触发区域，因此建议选 Box2D，将其二维平面映射到引擎 XZ，图片 Y 高度和 Billboard 朝向独立管理。Box2D 提供二维刚体、碰撞查询、接触事件和关节，见[官方文档](https://box2d.org/documentation/)。

若玩法改为真实三维坡地/台阶、桥上桥下或立体堆叠，则再评估 [Jolt Physics](https://github.com/jrouwe/JoltPhysics)。Box2D 已通过独立的 [physics2d 模块](PHYSICS2D.md)接入，以 PImpl 隐藏第三方类型，固定步更新后同步 Scene；world 示例自动使用该模块，关闭后可回退到 collision。
