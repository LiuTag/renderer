/** @file @brief 稀疏 XZ 瓦片地图、脏块缓存与批量精灵发布接口。 */
#ifndef RENDERER_MODULES_TILEMAP_TILEMAP_HPP_INCLUDED
#define RENDERER_MODULES_TILEMAP_TILEMAP_HPP_INCLUDED
#include <renderer/sprite_batch.hpp>
#include <vector>
namespace renderer {
/** @brief 地图坐标配置；瓦片显示内容自动缩放到一个格子，原点为格子左下角。 */
struct TileMapConfig {
    float tileSize = 1;
    uint32_t chunkSize = 16;
    Vec3 origin{};
};
/** @brief 区块内的一个图片格子，x/z 是 [0,chunkSize) 的局部坐标。 */
struct TileChunkCell {
    uint32_t x = 0, z = 0;
    SpriteHandle sprite;
};
/** @brief 以 PImpl 管理稀疏瓦片和可独立卸载的分块；按单写线程调用。 */
class TileMap {
  public:
    /** @brief 创建空地图。@param config 正的格子尺寸及 1～128 的块边长。@return 无。 */
    explicit TileMap(const TileMapConfig &config = {});
    /** @brief 释放地图。@return 无。 */
    ~TileMap();
    /** @brief 转移地图。@param other 源对象。@return 无。 */
    TileMap(TileMap &&other) noexcept;
    /** @brief 转移赋值。@param other 源对象。@return 当前对象。 */
    TileMap &operator=(TileMap &&other) noexcept;
    /** @brief 设置或删除瓦片，支持负坐标。@param x 格子 X。@param z 格子 Z。
     * @param sprite 非空则设置，为空则删除。@return 无。 */
    void set(int32_t x, int32_t z, SpriteHandle sprite);
    /** @brief 查询瓦片。@param x 格子 X。@param z 格子 Z。@return 未设置时为空。 */
    SpriteHandle get(int32_t x, int32_t z) const;
    /** @brief 卸载一个逻辑块，既有快照仍有效。@param x 块 X。@param z 块 Z。@return 无。 */
    void unloadChunk(int32_t x, int32_t z);
    /** @brief 校验并一次发布整个区块，失败保留旧块；空数组删除该块。
     * @param x 块 X。@param z 块 Z。@param assets 解析图片资源的仓库。
     * @param cells 无重复局部坐标的非空图片格子。@return 无。
     */
    void replaceChunk(int32_t x, int32_t z, const AssetStore &assets,
                      std::span<const TileChunkCell> cells);
    /** @brief 重建脏块并取得不可变批次。@param assets 解析瓦片资源。@return 每块一个批次。 */
    std::vector<SpriteBatch> batches(const AssetStore &assets);
    /** @brief 读取瓦片数。@return 已设置的格子数。 */
    size_t tileCount() const;
    /** @brief 读取块数。@return 当前驻留的非空块数。 */
    size_t chunkCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif
