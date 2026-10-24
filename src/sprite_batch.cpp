/** @file @brief 批量精灵资源解析、不可变发布与不依赖相机的保守分块包围盒。 */
#include "internal/sprite_batch.hpp"
#include "internal/pool.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace renderer {
struct SpriteBatch::Impl {
    detail::SpriteBatchData data;
};
SpriteBatch::SpriteBatch() = default;
SpriteBatch::~SpriteBatch() = default;
SpriteBatch::SpriteBatch(const SpriteBatch &) = default;
SpriteBatch &SpriteBatch::operator=(const SpriteBatch &) = default;
SpriteBatch::SpriteBatch(SpriteBatch &&) noexcept = default;
SpriteBatch &SpriteBatch::operator=(SpriteBatch &&) noexcept = default;
size_t SpriteBatch::size() const {
    return impl_ ? impl_->data.sprites.size() : 0;
}
AABB SpriteBatch::bounds() const {
    return impl_ ? impl_->data.bounds : AABB{};
}
void SpriteBatch::replace(const AssetStore &assets, std::span<const BatchSprite> sprites) {
    if (sprites.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Too many batch sprites");
    auto next = std::make_shared<Impl>();
    const auto owner = detail::uniqueId();
    next->data.sprites.reserve(sprites.size());
    for (size_t index = 0; index < sprites.size(); ++index) {
        const auto &s = sprites[index];
        detail::validateSprite(s.renderer);
        if (!s.world.isFinite() || !s.world.inverse())
            throw std::invalid_argument("Batch sprite transform must be finite and invertible");
        auto asset = detail::AssetAccess::sprite(assets, s.renderer.sprite);
        const auto &d = asset->description;
        const auto anchor = s.world.transformPoint({0, 0, 0});
        const auto &c = s.renderer;
        detail::SpriteSort sort{
            c.sortingLayer,  c.sortingOrder, 0, s.world.transformPoint(c.sortOffset),
            uint32_t(index), owner};
        detail::SpriteRecord record{{}, asset, c, s.world, sort, s.id};
        // 固定图片使用精确几何包围盒；Billboard 的朝向随相机改变，使用旋转不变的球界。
        const float x = std::max(std::abs(d.trimOffset.x - d.pivot.x * d.originalSize.x),
                                 std::abs(d.trimOffset.x + float(d.rect.width) -
                                          d.pivot.x * d.originalSize.x)) *
                        c.sizeMultiplier.x / d.pixelsPerUnit;
        const float y = std::max(std::abs(d.trimOffset.y - d.pivot.y * d.originalSize.y),
                                 std::abs(d.trimOffset.y + float(d.rect.height) -
                                          d.pivot.y * d.originalSize.y)) *
                        c.sizeMultiplier.y / d.pixelsPerUnit;
        float scale = 0;
        for (int column = 0; column < 2; ++column)
            scale = std::max(
                scale, std::hypot(s.world(0, column), s.world(1, column), s.world(2, column)));
        const float radius = scale * std::hypot(x, y);
        if (!std::isfinite(radius))
            throw std::invalid_argument("Batch bounds overflow");
        AABB bound{anchor - Vec3{radius, radius, radius}, anchor + Vec3{radius, radius, radius}};
        if (c.billboard == BillboardMode::Fixed) {
            const auto geometry = detail::spriteGeometry(record, Mat4::identity());
            if (!geometry)
                throw std::invalid_argument("Batch sprite geometry is singular");
            bound = geometry->bounds;
        }
        if (!finite(bound.minimum) || !finite(bound.maximum))
            throw std::invalid_argument("Batch bounds overflow");
        if (index == 0)
            next->data.bounds = bound;
        else {
            auto &b = next->data.bounds;
            b.minimum = {std::min(b.minimum.x, bound.minimum.x),
                         std::min(b.minimum.y, bound.minimum.y),
                         std::min(b.minimum.z, bound.minimum.z)};
            b.maximum = {std::max(b.maximum.x, bound.maximum.x),
                         std::max(b.maximum.y, bound.maximum.y),
                         std::max(b.maximum.z, bound.maximum.z)};
        }
        next->data.sprites.push_back(std::move(record));
    }
    impl_ = std::move(next);
}
std::shared_ptr<const detail::SpriteBatchData>
detail::SpriteBatchAccess::data(const SpriteBatch &b) {
    return b.impl_ ? std::shared_ptr<const SpriteBatchData>(b.impl_, &b.impl_->data) : nullptr;
}
} // namespace renderer
