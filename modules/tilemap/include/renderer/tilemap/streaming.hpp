/** @file @brief 地图区块的后台读取、预算发布、滞回卸载、编辑保存和文件数据源接口。 */
#ifndef RENDERER_MODULES_TILEMAP_STREAMING_HPP_INCLUDED
#define RENDERER_MODULES_TILEMAP_STREAMING_HPP_INCLUDED
#include <filesystem>
#include <functional>
#include <renderer/tilemap/tilemap.hpp>
#include <stop_token>
#include <string>
namespace renderer {
/** @brief 区块坐标，与 TileMap::unloadChunk 的坐标约定相同。 */
struct TileChunkCoord {
    int32_t x = 0, z = 0;
    /** @brief 比较块坐标。@param other 另一坐标。@return X/Z 均相等时为真。 */
    bool operator==(const TileChunkCoord &other) const = default;
};
/** @brief 可在后台读写的纯数据格子；type 是应用定义的非零资源编号，空格从稀疏数组省略。 */
struct StreamTile {
    uint32_t x = 0, z = 0, type = 0;
};
/** @brief 区块文件/生成器的纯数据；payload 可保存树木、碰撞、玩法等应用自定义信息。 */
struct TileChunkData {
    uint32_t chunkSize = 16;
    std::vector<StreamTile> tiles;
    std::vector<uint8_t> payload; ///< 最大 16 MiB；模块不解释此数据。
};
/** @brief 不含第三方类型的数据源回调；load/save 在工作线程，resolve 在调用 update 的线程。 */
struct TileStreamSource {
    std::function<TileChunkData(TileChunkCoord, std::stop_token)> load;
    std::function<void(TileChunkCoord, const TileChunkData &, std::stop_token)> save;
    std::function<SpriteHandle(uint32_t, AssetStore &)> resolve;
};
/** @brief 流式调度参数；半径使用区块的切比雪夫距离，发布预算以完整区块为单位。 */
struct TileStreamConfig {
    TileMapConfig map;
    uint32_t loadRadius = 2, unloadRadius = 3; ///< 0 <= load <= unload <= 32，形成滞回区域。
    uint32_t workerCount = 2,
             maxPendingRequests = 8;  ///< 1～16 个线程，请求总数限制含已取消但未结束的任务。
    uint32_t maxResidentChunks = 128; ///< 含待发布区块的容量，至少容纳整个卸载半径，最大 16384。
    uint32_t maxCommitsPerUpdate = 2, maxUnloadsPerUpdate = 2; ///< 每次 update 的正整数预算。
};
/** @brief 主线程可消费的区块生命周期和错误事件。 */
enum class TileStreamEventKind { Loaded, Unloaded, Saved, LoadFailed, SaveFailed };
/** @brief 生命周期事件；data 保留对应不可变版本，应用应及时消费事件，避免延长驻留。 */
struct TileStreamEvent {
    TileStreamEventKind kind;
    TileChunkCoord chunk;
    std::shared_ptr<const TileChunkData> data;
    std::string message;
};
/** @brief 当前流式状态与累计计数；空区块也计入 residentChunks。 */
struct TileStreamStats {
    uint32_t residentChunks = 0, readyChunks = 0, pendingLoads = 0, pendingSaves = 0;
    uint32_t dirtyChunks = 0, failedChunks = 0;
    uint64_t loaded = 0, unloaded = 0, failures = 0;
    bool capacityLimited = false; ///< 未完成保存/失败区块占满容量时，对新请求施加背压。
};
/** @brief PImpl 地图流式控制器；对外按单写线程使用，内部后台任务只读写纯数据。
 * 析构协作取消并等待回调退出，不隐式保存脏数据。停用前 requestSave 并 update 至 dirty/pendingSaves
 * 为零。
 */
class TileStreamer {
  public:
    /** @brief 创建后台调度器。@param config 驻留/线程/发布预算。@param source 必需
     * load/resolve，可选 save。
     * @return 无；回调必须线程安全，阻塞读取应响应 stop_token，不能访问可变 Scene/AssetStore。
     */
    TileStreamer(const TileStreamConfig &config, TileStreamSource source);
    /** @brief 取消剩余任务并等待工作线程，回调捕获的数据在此之前必须有效。@return 无。 */
    ~TileStreamer();
    /** @brief 转移控制器和后台状态。@param other 源对象。@return 无。 */
    TileStreamer(TileStreamer &&other) noexcept;
    /** @brief 停止当前后台状态后转移赋值。@param other 源对象。@return 当前对象。 */
    TileStreamer &operator=(TileStreamer &&other) noexcept;
    /** @brief 根据世界位置安排读取、发布、保存与卸载，不等待 I/O。
     * @param focus 玩家或流式观察点，世界 XZ 坐标。@param assets 仅在本线程解析图片。
     * @return 调度后的统计；数据源错误转为事件，不自动重复重试。
     */
    TileStreamStats update(Vec3 focus, AssetStore &assets);
    /** @brief 取得驻留地图批次。@param assets 图片仓库。@return 不可变批次，旧快照在卸载后仍有效。
     */
    std::vector<SpriteBatch> batches(const AssetStore &assets);
    /** @brief 查询驻留区块。@param coord 块坐标。@return 不可变数据；未发布时为空。 */
    std::shared_ptr<const TileChunkData> chunk(TileChunkCoord coord) const;
    /** @brief 编辑已驻留格子并标记待保存，失败保留旧块；需要 save 回调。
     * @param x 全局格子 X。@param z 全局格子 Z。@param type 资源编号，0 删除。
     * @param assets 在本线程解析图片。@return 无；未驻留/无保存器时抛出异常。
     */
    void setTile(int32_t x, int32_t z, uint32_t type, AssetStore &assets);
    /** @brief 替换驻留区块的应用状态并标记待保存，不改变瓦片几何；需要 save 回调。
     * @param coord 块坐标。@param payload 最大 16 MiB 的序列化物件/玩法状态。
     * @return 无；复制输入，旧快照和正在进行的保存仍保留原版本。
     */
    void setPayload(TileChunkCoord coord, std::span<const uint8_t> payload);
    /** @brief 请求后台保存当前脏块，后续 update 安排任务并接收结果。@return 无。 */
    void requestSave();
    /** @brief 允许失败操作重试。@param coord 块坐标。@return 有失败操作被恢复时为真。 */
    bool retry(TileChunkCoord coord);
    /** @brief 读取当前统计。@return 不推进调度的状态副本。 */
    TileStreamStats statistics() const;
    /** @brief 取出并清空生命周期事件。@return 事件数组，应用可据此创建/删除场景节点和碰撞体。 */
    std::vector<TileStreamEvent> takeEvents();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
/** @brief 读取带版本、长度限制和校验和的 RTile 区块文件。
 * @param path 文件路径。@param stop 协作取消令牌。@return 纯数据，损坏/截断/取消时抛出异常。
 */
TileChunkData readTileChunk(const std::filesystem::path &path, std::stop_token stop = {});
/** @brief 写临时文件并重命名替换目标，失败不覆盖已有文件；同一路径由应用保证单写者。
 * @param path 文件路径，自动创建父目录。@param data 合法纯数据区块。
 * @param stop 协作取消令牌。@return 无；不保存 SpriteHandle 或原生图形资源。
 */
void writeTileChunk(const std::filesystem::path &path, const TileChunkData &data,
                    std::stop_token stop = {});
} // namespace renderer
#endif // RENDERER_MODULES_TILEMAP_STREAMING_HPP_INCLUDED
