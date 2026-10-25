#include "renderer/sprite.hpp"
#include "internal/sprite.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace renderer {
struct SpriteAtlas::Impl {
    std::map<std::string, SpriteHandle, std::less<>> entries;
};
SpriteAtlas::SpriteAtlas(AssetStore &assets, TextureHandle texture,
                         std::span<const SpriteAtlasEntry> entries) {
    if (entries.empty())
        throw std::invalid_argument("SpriteAtlas needs entries");
    auto data = std::make_shared<Impl>();
    std::vector<SpriteHandle> created;
    created.reserve(entries.size());
    try {
        for (const auto &entry : entries) {
            if (entry.name.empty() || data->entries.contains(entry.name))
                throw std::invalid_argument("SpriteAtlas names must be nonempty and unique");
            auto desc = entry.sprite;
            desc.texture = texture;
            const auto sprite = assets.createSprite(desc);
            created.push_back(sprite);
            data->entries.emplace(entry.name, sprite);
        }
    } catch (...) {
        for (auto sprite : created)
            assets.destroySprite(sprite);
        throw;
    }
    impl_ = std::move(data);
}
SpriteAtlas::~SpriteAtlas() = default;
SpriteAtlas::SpriteAtlas(const SpriteAtlas &) = default;
SpriteAtlas &SpriteAtlas::operator=(const SpriteAtlas &) = default;
SpriteAtlas::SpriteAtlas(SpriteAtlas &&) noexcept = default;
SpriteAtlas &SpriteAtlas::operator=(SpriteAtlas &&) noexcept = default;
SpriteHandle SpriteAtlas::sprite(std::string_view name) const {
    if (impl_)
        if (const auto it = impl_->entries.find(name); it != impl_->entries.end())
            return it->second;
    throw std::out_of_range("Unknown sprite name");
}
size_t SpriteAtlas::size() const {
    return impl_ ? impl_->entries.size() : 0;
}
namespace detail {
void validateSprite(const SpriteRenderer &c) {
    if (!std::isfinite(c.edgeSoftness) || c.edgeSoftness < .25f || c.edgeSoftness > 4)
        throw std::invalid_argument("Invalid SDF edge softness");

    if (!c.sprite || !finite(c.sortOffset) ||
        (c.billboard != BillboardMode::Fixed && c.billboard != BillboardMode::Upright &&
         c.billboard != BillboardMode::ViewPlane) ||
        (c.transparency != SpriteTransparency::Sorted &&
         c.transparency != SpriteTransparency::FollowView) ||
        !std::isfinite(c.rotation) || !std::isfinite(c.sizeMultiplier.x) ||
        !std::isfinite(c.sizeMultiplier.y) || c.sizeMultiplier.x <= 0 || c.sizeMultiplier.y <= 0)
        throw std::invalid_argument("Invalid SpriteRenderer geometry or mode");
    for (float v : {c.tint.x, c.tint.y, c.tint.z, c.tint.w})
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Sprite tint must be finite and nonnegative");
    if (c.tint.w > 1)
        throw std::invalid_argument("Sprite alpha must be in [0,1]");

    for (const auto color : {c.colorOverlay, c.outlineColor}) {
        for (float v : {color.x, color.y, color.z, color.w})
            if (!std::isfinite(v) || v < 0)
                throw std::invalid_argument("Invalid sprite effect color");
        if (color.w > 1)
            throw std::invalid_argument("Invalid sprite effect weight");
    }
    if (!std::isfinite(c.outlineWidth) || c.outlineWidth < 0 || c.outlineWidth > 16)
        throw std::invalid_argument("Outline width must be in [0,16]");
}
Mat4 cameraProjection(const Camera &camera, uint32_t width, uint32_t height) {
    if (!width || !height)
        throw std::invalid_argument("Viewport dimensions must be positive");
    const float aspect = camera.aspectRatio > 0 ? camera.aspectRatio : float(width) / float(height);
    const float half = camera.orthographicHeight * 0.5f;
    return camera.projection == Camera::Projection::Perspective
               ? Mat4::perspective(camera.verticalFov, aspect, camera.nearPlane, camera.farPlane)
               : Mat4::orthographic(-half * aspect, half * aspect, -half, half, camera.nearPlane,
                                    camera.farPlane);
}
std::optional<SpriteGeometry> spriteGeometry(const SpriteRecord &record, const Mat4 &cameraWorld) {
    if (record.fixedGeometry)
        return *record.fixedGeometry;
    const auto &d = record.sprite->description;
    const auto &c = record.component;
    const float w = float(d.rect.width) / d.pixelsPerUnit * c.sizeMultiplier.x;
    const float h = float(d.rect.height) / d.pixelsPerUnit * c.sizeMultiplier.y;
    Vec3 offset{(d.trimOffset.x + float(d.rect.width) * 0.5f - d.pivot.x * d.originalSize.x) /
                    d.pixelsPerUnit * c.sizeMultiplier.x,
                (d.trimOffset.y + float(d.rect.height) * 0.5f - d.pivot.y * d.originalSize.y) /
                    d.pixelsPerUnit * c.sizeMultiplier.y,
                0};
    if (c.flipX)
        offset.x = -offset.x;
    if (c.flipY)
        offset.y = -offset.y;
    Mat4 basis = record.world;
    if (c.billboard != BillboardMode::Fixed) {
        Vec3 right, up, normal;
        if (c.billboard == BillboardMode::Upright) {
            normal = {cameraWorld(0, 2), 0, cameraWorld(2, 2)};
            // 俯视极点没有唯一水平朝向，采用固定世界 +Z，避免归一化零向量。
            normal = dot(normal, normal) > 1e-10f ? normalized(normal) : Vec3{0, 0, 1};
            up = {0, 1, 0};
            right = cross(up, normal);
        } else {
            right = normalized({cameraWorld(0, 0), cameraWorld(1, 0), cameraWorld(2, 0)});
            normal =
                normalized(cross(right, {cameraWorld(0, 1), cameraWorld(1, 1), cameraWorld(2, 1)}));
            up = cross(normal, right);
        }
        const Vec3 sx{record.world(0, 0), record.world(1, 0), record.world(2, 0)};
        const Vec3 sy{record.world(0, 1), record.world(1, 1), record.world(2, 1)};
        right = right * std::sqrt(dot(sx, sx));
        up = up * std::sqrt(dot(sy, sy));
        if (record.world.determinant3x3() < 0)
            right = right * -1;
        basis = Mat4::identity();
        for (int row = 0; row < 3; ++row) {
            basis(row, 0) = row == 0 ? right.x : row == 1 ? right.y : right.z;
            basis(row, 1) = row == 0 ? up.x : row == 1 ? up.y : up.z;
            basis(row, 2) = row == 0 ? normal.x : row == 1 ? normal.y : normal.z;
            basis(row, 3) = record.world(row, 3);
        }
    }
    const auto world =
        basis * Mat4::rotationZ(c.rotation) * Mat4::translation(offset) * Mat4::scaling({w, h, 1});
    if (!world.isFinite())
        throw std::overflow_error("Sprite geometry overflow");
    const auto inverse = world.inverse();
    if (!inverse)
        return std::nullopt;
    const auto &texture = record.sprite->material->texture->description;
    Vec4 uv{float(d.rect.x) / float(texture.width), float(d.rect.y) / float(texture.height),
            float(d.rect.width) / float(texture.width),
            float(d.rect.height) / float(texture.height)};
    if (c.flipX) {
        uv.x += uv.z;
        uv.z = -uv.z;
    }
    if (c.flipY) {
        uv.y += uv.w;
        uv.w = -uv.w;
    }
    return SpriteGeometry{world, inverse->transposed(), uv,
                          record.sprite->mesh->bounds.transformed(world)};
}
AABB conservativeSpriteBounds(const SpriteRecord &record) {
    const auto &d = record.sprite->description;
    const auto &c = record.component;
    const auto anchor = record.world.transformPoint({});
    // 固定图片使用精确几何包围盒；Billboard 的朝向随相机改变，使用旋转不变的球界。
    const float x =
        std::max(std::abs(d.trimOffset.x - d.pivot.x * d.originalSize.x),
                 std::abs(d.trimOffset.x + float(d.rect.width) - d.pivot.x * d.originalSize.x)) *
        c.sizeMultiplier.x / d.pixelsPerUnit;
    const float y =
        std::max(std::abs(d.trimOffset.y - d.pivot.y * d.originalSize.y),
                 std::abs(d.trimOffset.y + float(d.rect.height) - d.pivot.y * d.originalSize.y)) *
        c.sizeMultiplier.y / d.pixelsPerUnit;
    float scale = 0;
    for (int column = 0; column < 2; ++column)
        scale = std::max(scale, std::hypot(record.world(0, column), record.world(1, column),
                                           record.world(2, column)));
    const float radius = scale * std::hypot(x, y);
    if (!std::isfinite(radius))
        throw std::invalid_argument("Batch bounds overflow");
    AABB bound{anchor - Vec3{radius, radius, radius}, anchor + Vec3{radius, radius, radius}};
    if (c.billboard == BillboardMode::Fixed) {
        const auto geometry = detail::spriteGeometry(record, Mat4::identity());
        if (geometry)
            bound = geometry->bounds;
    }
    const float padding = record.sprite->material->description.effect.boundsPadding;
    bound.minimum = bound.minimum - Vec3{padding, padding, padding};
    bound.maximum = bound.maximum + Vec3{padding, padding, padding};
    if (!finite(bound.minimum) || !finite(bound.maximum))
        throw std::invalid_argument("Batch bounds overflow");
    return bound;
}
bool spriteSortLess(const SpriteSort &a, float depthA, const SpriteSort &b, float depthB) {
    return std::tuple{a.layer, a.order, -depthA, a.groupIndex, a.groupGeneration, a.memberOrder} <
           std::tuple{b.layer, b.order, -depthB, b.groupIndex, b.groupGeneration, b.memberOrder};
}
} // namespace detail
} // namespace renderer
