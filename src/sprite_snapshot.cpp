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
    detail::AabbIndex index;
    bool indexed = true;
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
std::vector<uint32_t> SpriteViewSnapshot::rayCandidates(Vec3 origin, Vec3 direction) const {
    if (!finite(origin) || !finite(direction) || dot(direction, direction) <= 0)
        throw std::invalid_argument("Invalid spatial ray");
    if (!impl_)
        return {};
    if (impl_->indexed)
        return impl_->index.query(
            [&](const AABB &b) { return detail::AabbIndex::ray(b, origin, direction); });
    std::vector<uint32_t> ids(impl_->sprites.size());
    std::iota(ids.begin(), ids.end(), 0u);
    return ids;
}
SpriteViewSnapshot RenderSnapshot::spriteView(const RenderView &view, uint32_t width,
                                              uint32_t height) const {
    if (view.transparency != TransparencyMode::SortedAlpha &&
        view.transparency != TransparencyMode::WeightedBlendedOIT)
        throw std::invalid_argument("Invalid sprite view transparency mode");
    const auto &data = detail::SnapshotAccess::get(*this);
    const auto camera = std::find_if(data.scene().cameras.begin(), data.scene().cameras.end(),
                                     [&](const auto &c) { return c.node == view.cameraNode; });
    if (camera == data.scene().cameras.end())
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
    entries.reserve(data.scene().sprites.size() + data.batchSpriteCount);
    const auto appendSprite = [&](const detail::SpriteRecord &sprite) {
        if (!(sprite.component.layerMask & camera->camera.cullingMask))
            return;
        const auto geometry = detail::spriteGeometry(sprite, camera->world);
        if (!geometry || (view.frustumCulling && !frustum.intersects(geometry->bounds)))
            return;
        const auto &texture = sprite.sprite->material->texture;
        const uint32_t layer = sprite.component.textureLayer == UINT32_MAX
                                   ? sprite.sprite->description.textureLayer
                                   : sprite.component.textureLayer;
        if (layer >= texture->description.layers)
            throw std::invalid_argument("Sprite array layer out of bounds");
        // 别名共享指针只公开公共 TextureDesc，拥有者仍是内部不可变纹理资源。
        std::shared_ptr<const TextureDesc> pixels(texture, &texture->description);
        const auto materialColor = sprite.sprite->material->description.baseColor;
        const auto tint = sprite.component.tint;
        const Vec4 combined{tint.x * materialColor.x, tint.y * materialColor.y,
                            tint.z * materialColor.z, tint.w * materialColor.w};
        entries.push_back(
            {{sprite.node, geometry->world, geometry->bounds, geometry->uvRect, combined,
              sprite.sprite->description.alphaMode, sprite.sprite->description.alphaCutoff,
              sprite.component.transparency, sprite.component.layerMask, std::move(pixels),
              sprite.instanceId, sprite.sprite->description.distanceFieldRange, layer,
              sprite.component.depthTest},
             sprite.sort,
             -inverse->transformPoint(sprite.sort.point).z});
    };
    const bool indexed = view.spatialCulling && data.scene().indexed && view.frustumCulling;
    const auto visible = [&](const AABB &b) { return frustum.intersects(b); };
    if (indexed) {
        for (auto i : data.scene().spriteIndex.query(visible))
            appendSprite(data.scene().sprites[i]);
    } else
        for (const auto &sprite : data.scene().sprites)
            appendSprite(sprite);
    for (const auto &batch : data.batches) {
        if (view.frustumCulling && !frustum.intersects(batch->bounds))
            continue;
        if (indexed) {
            for (auto i : batch->index.query(visible))
                appendSprite(batch->sprites[i]);
        } else
            for (const auto &sprite : batch->sprites)
                appendSprite(sprite);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        if (a.item.depthTest != b.item.depthTest)
            return a.item.depthTest;
        return detail::spriteSortLess(a.sort, a.depth, b.sort, b.depth);
    });
    result->sprites.reserve(entries.size());
    for (auto &entry : entries)
        result->sprites.push_back(std::move(entry.item));
    result->indexed = view.spatialCulling;
    if (result->indexed) {
        std::vector<AABB> boxes;
        boxes.reserve(result->sprites.size());
        for (const auto &r : result->sprites)
            boxes.push_back(r.bounds);
        result->index.reset(boxes);
    }
    SpriteViewSnapshot snapshot;
    snapshot.impl_ = std::move(result);
    return snapshot;
}
} // namespace renderer
