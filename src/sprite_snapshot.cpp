#include "renderer/sprite_snapshot.hpp"
#include "internal/frustum.hpp"
#include "internal/snapshot.hpp"
#include "renderer/renderer.hpp"
#include <algorithm>
#include <stdexcept>

namespace renderer {
struct SpriteViewSnapshot::Impl {
    RenderSnapshot source;
    Mat4 vp, inverse;
    uint32_t width = 0, height = 0;
    std::vector<SpriteViewItem> sprites;
};
SpriteViewSnapshot::SpriteViewSnapshot() = default;
SpriteViewSnapshot::~SpriteViewSnapshot() = default;
SpriteViewSnapshot::SpriteViewSnapshot(const SpriteViewSnapshot &) = default;
SpriteViewSnapshot &SpriteViewSnapshot::operator=(const SpriteViewSnapshot &) = default;
SpriteViewSnapshot::SpriteViewSnapshot(SpriteViewSnapshot &&) noexcept = default;
SpriteViewSnapshot &SpriteViewSnapshot::operator=(SpriteViewSnapshot &&) noexcept = default;
std::span<const SpriteViewItem> SpriteViewSnapshot::sprites() const {
    return impl_ ? std::span<const SpriteViewItem>(impl_->sprites)
                 : std::span<const SpriteViewItem>{};
}
Mat4 SpriteViewSnapshot::viewProjection() const {
    return impl_ ? impl_->vp : Mat4::identity();
}
Mat4 SpriteViewSnapshot::inverseViewProjection() const {
    return impl_ ? impl_->inverse : Mat4::identity();
}
std::pair<uint32_t, uint32_t> SpriteViewSnapshot::viewportSize() const {
    return impl_ ? std::pair{impl_->width, impl_->height} : std::pair{0u, 0u};
}
SpriteViewSnapshot RenderSnapshot::spriteView(const RenderView &view, uint32_t width,
                                              uint32_t height) const {
    if (view.transparency != TransparencyMode::SortedAlpha &&
        view.transparency != TransparencyMode::WeightedBlendedOIT)
        throw std::invalid_argument("Invalid sprite view transparency mode");
    const auto &data = detail::SnapshotAccess::get(*this);
    const auto camera = std::find_if(data.cameras.begin(), data.cameras.end(),
                                     [&](const auto &c) { return c.node == view.cameraNode; });
    if (camera == data.cameras.end())
        throw std::invalid_argument("Sprite view camera is absent or hidden");
    const auto inverse = camera->world.inverse();
    if (!inverse)
        throw std::invalid_argument("Sprite view camera is singular");
    auto result = std::make_shared<SpriteViewSnapshot::Impl>();
    result->source = *this;
    result->width = width;
    result->height = height;
    result->vp = detail::cameraProjection(camera->camera, width, height) * *inverse;
    const auto inverseVP = result->vp.inverse();
    if (!inverseVP)
        throw std::invalid_argument("Sprite view projection is singular");
    result->inverse = *inverseVP;
    const detail::Frustum frustum(result->vp);
    struct Entry {
        SpriteViewItem item;
        detail::SpriteSort sort;
        float depth;
    };
    std::vector<Entry> entries;
    entries.reserve(data.sprites.size() + data.batchSpriteCount);
    const auto appendSprite = [&](const detail::SpriteRecord &sprite) {
        if (!(sprite.component.layerMask & camera->camera.cullingMask))
            return;
        const auto geometry = detail::spriteGeometry(sprite, camera->world);
        if (!geometry || (view.frustumCulling && !frustum.intersects(geometry->bounds)))
            return;
        const auto &texture = sprite.sprite->material->texture;
        // 别名共享指针只公开公共 TextureDesc，拥有者仍是内部不可变纹理资源。
        std::shared_ptr<const TextureDesc> pixels(texture, &texture->description);
        entries.push_back({{sprite.node, geometry->world, geometry->bounds, geometry->uvRect,
                            sprite.component.tint, sprite.sprite->description.alphaMode,
                            sprite.sprite->description.alphaCutoff, sprite.component.transparency,
                            sprite.component.layerMask, std::move(pixels), sprite.instanceId},
                           sprite.sort,
                           -inverse->transformPoint(sprite.sort.point).z});
    };
    for (const auto &sprite : data.sprites)
        appendSprite(sprite);
    for (const auto &batch : data.batches) {
        if (view.frustumCulling && !frustum.intersects(batch->bounds))
            continue;
        for (const auto &sprite : batch->sprites)
            appendSprite(sprite);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return detail::spriteSortLess(a.sort, a.depth, b.sort, b.depth);
    });
    result->sprites.reserve(entries.size());
    for (auto &entry : entries)
        result->sprites.push_back(std::move(entry.item));
    SpriteViewSnapshot snapshot;
    snapshot.impl_ = std::move(result);
    return snapshot;
}
} // namespace renderer
