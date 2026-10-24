/** @file @brief 按块保存稀疏瓦片，仅重建脏块，并通过公共 SpriteBatch 保留资源。 */
#include <cmath>
#include <map>
#include <renderer/tilemap/tilemap.hpp>
#include <stdexcept>
namespace renderer {
namespace {
int32_t divide(int32_t x, int32_t n) {
    const auto q = x / n, r = x % n;
    return q - (r < 0 ? 1 : 0);
}
} // namespace
struct TileMap::Impl {
    struct Chunk {
        std::map<std::pair<int32_t, int32_t>, SpriteHandle> tiles;
        SpriteBatch batch;
        bool dirty = true;
    };
    TileMapConfig config;
    void bake(Chunk &chunk, const AssetStore &assets);
    std::map<std::pair<int32_t, int32_t>, Chunk> chunks;
};
void TileMap::Impl::bake(Chunk &chunk, const AssetStore &assets) {
    std::vector<BatchSprite> sprites;
    sprites.reserve(chunk.tiles.size());
    for (const auto &[cell, handle] : chunk.tiles) {
        const auto d = assets.spriteDescription(handle);
        const auto &c = config;
        SpriteRenderer renderer;
        renderer.sprite = handle;
        renderer.billboard = BillboardMode::Fixed;
        renderer.sortingLayer = -100;
        renderer.sizeMultiplier = {c.tileSize * d.pixelsPerUnit / float(d.rect.width),
                                   c.tileSize * d.pixelsPerUnit / float(d.rect.height)};
        const Vec3 correction{
            (d.pivot.x * d.originalSize.x - d.trimOffset.x - float(d.rect.width) * .5f) /
                d.pixelsPerUnit * renderer.sizeMultiplier.x,
            (d.pivot.y * d.originalSize.y - d.trimOffset.y - float(d.rect.height) * .5f) /
                d.pixelsPerUnit * renderer.sizeMultiplier.y,
            0};
        const Vec3 p = c.origin + Vec3{(float(cell.first) + .5f) * c.tileSize, 0,
                                       (float(cell.second) + .5f) * c.tileSize};
        const auto id = (uint64_t(uint32_t(cell.first)) << 32) | uint32_t(cell.second);
        sprites.push_back(
            {renderer,
             Mat4::translation(p) * Mat4::rotationX(radians(-90)) * Mat4::translation(correction),
             id});
    }
    chunk.batch.replace(assets, sprites);
    chunk.dirty = false;
}
TileMap::TileMap(const TileMapConfig &c) : impl_(std::make_unique<Impl>()) {
    if (!std::isfinite(c.tileSize) || c.tileSize <= 0 || !finite(c.origin) || !c.chunkSize ||
        c.chunkSize > 128)
        throw std::invalid_argument("Invalid tile map config");
    impl_->config = c;
}
TileMap::~TileMap() = default;
TileMap::TileMap(TileMap &&) noexcept = default;
TileMap &TileMap::operator=(TileMap &&) noexcept = default;
void TileMap::set(int32_t x, int32_t z, SpriteHandle sprite) {
    const auto key = std::pair{divide(x, int32_t(impl_->config.chunkSize)),
                               divide(z, int32_t(impl_->config.chunkSize))};
    if (!sprite) {
        auto i = impl_->chunks.find(key);
        if (i == impl_->chunks.end())
            return;
        i->second.tiles.erase({x, z});
        i->second.dirty = true;
        if (i->second.tiles.empty())
            impl_->chunks.erase(i);
        return;
    }
    auto &c = impl_->chunks[key];
    c.tiles[{x, z}] = sprite;
    c.dirty = true;
}
SpriteHandle TileMap::get(int32_t x, int32_t z) const {
    const auto n = int32_t(impl_->config.chunkSize);
    const auto i = impl_->chunks.find({divide(x, n), divide(z, n)});
    if (i == impl_->chunks.end())
        return {};
    auto t = i->second.tiles.find({x, z});
    return t == i->second.tiles.end() ? SpriteHandle{} : t->second;
}
void TileMap::unloadChunk(int32_t x, int32_t z) {
    impl_->chunks.erase({x, z});
}
void TileMap::replaceChunk(int32_t x, int32_t z, const AssetStore &assets,
                           std::span<const TileChunkCell> cells) {
    const auto n = impl_->config.chunkSize;
    if (cells.size() > size_t(n) * n)
        throw std::invalid_argument("Too many chunk cells");
    Impl::Chunk next;
    for (const auto &cell : cells) {
        const int64_t wx = int64_t(x) * n + cell.x, wz = int64_t(z) * n + cell.z;
        if (cell.x >= n || cell.z >= n || !cell.sprite || wx < INT32_MIN || wx > INT32_MAX ||
            wz < INT32_MIN || wz > INT32_MAX)
            throw std::invalid_argument("Invalid chunk cell");
        if (!next.tiles.emplace(std::pair{int32_t(wx), int32_t(wz)}, cell.sprite).second)
            throw std::invalid_argument("Duplicate chunk cell");
    }
    if (cells.empty()) {
        unloadChunk(x, z);
        return;
    }
    impl_->bake(next, assets);
    impl_->chunks.insert_or_assign(std::pair{x, z}, std::move(next));
}
size_t TileMap::tileCount() const {
    size_t n = 0;
    for (const auto &[key, c] : impl_->chunks) {
        (void)key;
        n += c.tiles.size();
    }
    return n;
}
size_t TileMap::chunkCount() const {
    return impl_->chunks.size();
}
std::vector<SpriteBatch> TileMap::batches(const AssetStore &assets) {
    std::vector<SpriteBatch> result;
    for (auto &[key, chunk] : impl_->chunks) {
        (void)key;
        if (chunk.dirty) {
            impl_->bake(chunk, assets);
        }
        result.push_back(chunk.batch);
    }
    return result;
}
} // namespace renderer
