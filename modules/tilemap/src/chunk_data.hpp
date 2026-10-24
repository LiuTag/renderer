/** @file @brief 区块纯数据校验及协作取消帮助函数，仅供 tilemap 模块内部使用。 */
#ifndef RENDERER_MODULES_TILEMAP_SRC_CHUNK_DATA_HPP_INCLUDED
#define RENDERER_MODULES_TILEMAP_SRC_CHUNK_DATA_HPP_INCLUDED
#include <renderer/tilemap/streaming.hpp>
#include <stdexcept>
namespace renderer::tile_detail {
inline constexpr uint32_t maxPayload = 16 * 1024 * 1024;
inline void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Tile chunk request cancelled");
}
inline void validate(const TileChunkData &data) {
    if (!data.chunkSize || data.chunkSize > 128 ||
        data.tiles.size() > size_t(data.chunkSize) * data.chunkSize ||
        data.payload.size() > maxPayload)
        throw std::invalid_argument("Invalid tile chunk dimensions or data size");
    std::vector<bool> occupied(size_t(data.chunkSize) * data.chunkSize);
    for (const auto &tile : data.tiles) {
        if (tile.x >= data.chunkSize || tile.z >= data.chunkSize || !tile.type)
            throw std::invalid_argument("Invalid tile chunk cell/type");
        const auto index = size_t(tile.z) * data.chunkSize + tile.x;
        if (occupied[index])
            throw std::invalid_argument("Duplicate tile chunk cell");
        occupied[index] = true;
    }
}
} // namespace renderer::tile_detail
#endif // RENDERER_MODULES_TILEMAP_SRC_CHUNK_DATA_HPP_INCLUDED
