#include "renderer/interaction/picking.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <renderer/texture.hpp>
#include <stdexcept>
#include <unordered_set>

namespace renderer {
namespace {
struct NodeHash {
    size_t operator()(NodeHandle node) const noexcept {
        return std::hash<uint64_t>{}(node.owner) ^ std::hash<uint64_t>{}(node.generation) ^
               std::hash<uint32_t>{}(node.index);
    }
};
Vec3 unproject(const Mat4 &inverse, Vec2 pixel, uint32_t width, uint32_t height, float depth) {
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y))
        throw std::invalid_argument("Screen coordinates must be finite");
    const auto p =
        inverse * Vec4{2 * pixel.x / float(width) - 1, 1 - 2 * pixel.y / float(height), depth, 1};
    if (!std::isfinite(p.w) || std::abs(p.w) < 1e-12f)
        throw std::overflow_error("Cannot unproject screen position");
    const Vec3 result{p.x / p.w, p.y / p.w, p.z / p.w};
    if (!finite(result))
        throw std::overflow_error("Unprojected position overflow");
    return result;
}
int address(int index, int size, TextureAddress mode) {
    if (mode == TextureAddress::Clamp)
        return std::clamp(index, 0, size - 1);
    if (mode == TextureAddress::Mirror) {
        index = (index % (size * 2) + size * 2) % (size * 2);
        return index < size ? index : size * 2 - 1 - index;
    }
    return (index % size + size) % size;
}
float alpha(const TextureDesc &texture, Vec2 uv, uint32_t layer) {
    // 高层相机输出固定为不透明，不存在 CPU 像素副本。
    if (texture.renderTexture.size().first)
        return 1;
    const auto sample = [&](int x, int y) {
        x = address(x, int(texture.width), texture.sampling.addressU);
        y = address(y, int(texture.height), texture.sampling.addressV);
        return renderer::texture::alpha(texture, uint32_t(x), uint32_t(y), layer);
    };
    if (texture.sampling.magFilter == TextureFilter::Nearest)
        return sample(int(std::floor(uv.x * float(texture.width))),
                      int(std::floor(uv.y * float(texture.height))));
    const float x = uv.x * float(texture.width) - 0.5f, y = uv.y * float(texture.height) - 0.5f;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const float fx = x - float(ix), fy = y - float(iy);
    return (sample(ix, iy) * (1 - fx) + sample(ix + 1, iy) * fx) * (1 - fy) +
           (sample(ix, iy + 1) * (1 - fx) + sample(ix + 1, iy + 1) * fx) * fy;
}
} // namespace
struct SceneQuery::Impl {
    struct Entry {
        const SpriteViewItem *source;
        Mat4 inverse;
    };
    SpriteViewSnapshot snapshot;
    RenderView view;
    uint32_t width, height;
    Mat4 vp, inverse;
    std::vector<Entry> entries;
    std::vector<size_t> entryBySprite;
};
SceneQuery::SceneQuery(RenderSnapshot snapshot, const RenderView &view, uint32_t width,
                       uint32_t height, const SceneQueryFilter &filter)
    : impl_(std::make_unique<Impl>()) {
    impl_->snapshot = snapshot.spriteView(view, width, height);
    impl_->vp = impl_->snapshot.viewProjection();
    impl_->inverse = impl_->snapshot.inverseViewProjection();
    impl_->view = view;
    impl_->width = width;
    impl_->height = height;
    const std::unordered_set<NodeHandle, NodeHash> excluded(filter.excludedNodes.begin(),
                                                            filter.excludedNodes.end());
    impl_->entryBySprite.assign(impl_->snapshot.sprites().size(), SIZE_MAX);
    for (const auto &sprite : impl_->snapshot.sprites()) {
        if (!(sprite.layerMask & filter.layerMask) || excluded.contains(sprite.node))
            continue;
        impl_->entryBySprite[size_t(&sprite - impl_->snapshot.sprites().data())] =
            impl_->entries.size();
        impl_->entries.push_back({&sprite, *sprite.world.inverse()});
    }
}
SceneQuery::~SceneQuery() = default;
SceneQuery::SceneQuery(SceneQuery &&) noexcept = default;
SceneQuery &SceneQuery::operator=(SceneQuery &&) noexcept = default;
Ray SceneQuery::screenRay(Vec2 pixel) const {
    const auto near = unproject(impl_->inverse, pixel, impl_->width, impl_->height, 0);
    const auto far = unproject(impl_->inverse, pixel, impl_->width, impl_->height, 1);
    return {near, normalized(far - near)};
}
std::optional<Vec3> SceneQuery::groundPoint(Vec2 pixel, float height) const {
    if (!std::isfinite(height))
        throw std::invalid_argument("Ground height must be finite");
    const auto ray = screenRay(pixel);
    if (std::abs(ray.direction.y) < 1e-7f)
        return std::nullopt;
    const float distance = (height - ray.origin.y) / ray.direction.y;
    if (distance < 0 || !std::isfinite(distance))
        return std::nullopt;
    auto point = ray.origin + ray.direction * distance;
    point.y = height;
    const auto clip = impl_->vp * Vec4{point.x, point.y, point.z, 1};
    if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w)
        return std::nullopt;
    return point;
}
std::optional<Vec3> SceneQuery::worldToScreen(Vec3 world) const {
    if (!finite(world))
        throw std::invalid_argument("World position must be finite");
    const auto p = impl_->vp * Vec4{world.x, world.y, world.z, 1};
    if (!finite({p.x, p.y, p.z}) || !std::isfinite(p.w))
        throw std::overflow_error("Projected position overflow");
    if (p.w <= 0 || p.x < -p.w || p.x > p.w || p.y < -p.w || p.y > p.w || p.z < 0 || p.z > p.w)
        return std::nullopt;
    return Vec3{(p.x / p.w * 0.5f + 0.5f) * float(impl_->width),
                (0.5f - p.y / p.w * 0.5f) * float(impl_->height), p.z / p.w};
}
std::optional<SpriteHit> SceneQuery::pickSprite(Vec2 pixel, float alphaThreshold) const {
    if (!std::isfinite(alphaThreshold) || alphaThreshold < 0 || alphaThreshold > 1)
        throw std::invalid_argument("Pick alpha threshold must be in [0,1]");
    const auto ray = screenRay(pixel);
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= float(impl_->width) ||
        pixel.y >= float(impl_->height))
        return std::nullopt;
    struct Candidate {
        const Impl::Entry *entry;
        SpriteHit hit;
    };
    std::vector<Candidate> candidates;
    float opaqueDistance = std::numeric_limits<float>::infinity();
    std::optional<SpriteHit> opaque;
    for (auto i : impl_->snapshot.rayCandidates(ray.origin, ray.direction)) {
        const auto at = impl_->entryBySprite[i];
        if (at == SIZE_MAX)
            continue;
        const auto &entry = impl_->entries[at];
        const auto o = entry.inverse.transformPoint(ray.origin);
        const auto vector =
            entry.inverse * Vec4{ray.direction.x, ray.direction.y, ray.direction.z, 0};
        const Vec3 d{vector.x, vector.y, vector.z};
        if (std::abs(d.z) < 1e-8f)
            continue;
        const float t = -o.z / d.z;
        const auto local = o + d * t;
        if (t < 0 || !std::isfinite(t) || std::abs(local.x) > 0.5f || std::abs(local.y) > 0.5f)
            continue;
        const auto world = ray.origin + ray.direction * t;
        const auto clip = impl_->vp * Vec4{world.x, world.y, world.z, 1};
        if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w)
            continue;
        const auto &r = entry.source->uvRect;
        const Vec2 uv{r.x + (local.x + 0.5f) * r.z, r.y + (local.y + 0.5f) * r.w};
        const auto &sprite = *entry.source;
        const float coverage = alpha(*sprite.texture, uv, sprite.textureLayer);
        const float a = (sprite.distanceFieldRange > 0 ? (coverage >= .5f ? 1.f : 0.f) : coverage) *
                        sprite.tint.w;
        const auto mode = sprite.alphaMode;
        if (mode == AlphaMode::Mask && a < sprite.alphaCutoff)
            continue;
        if (mode == AlphaMode::Blend && (a <= 0 || a < alphaThreshold))
            continue;
        SpriteHit hit{entry.source->node, world, uv, t, entry.source->instanceId};
        if (mode != AlphaMode::Blend && sprite.depthTest) {
            if (t < opaqueDistance) {
                opaqueDistance = t;
                opaque = hit;
            }
        } else
            candidates.push_back({&entry, hit});
    }
    std::optional<SpriteHit> result = opaque, oit;
    bool resultIsOpaque = opaque.has_value();
    // 常规透明与渲染同序，最后覆盖者优先。OIT 本身没有确定前后顺序，选最近交点。
    for (const auto &candidate : candidates) {
        if (!candidate.entry->source->depthTest) {
            result = candidate.hit;
            resultIsOpaque = false;
            continue;
        }
        if (candidate.hit.distance >= opaqueDistance)
            continue;
        const bool weighted =
            impl_->view.transparency == TransparencyMode::WeightedBlendedOIT &&
            candidate.entry->source->transparency == SpriteTransparency::FollowView;
        if (weighted) {
            if (!oit || candidate.hit.distance < oit->distance)
                oit = candidate.hit;
        } else {
            result = candidate.hit;
            resultIsOpaque = false;
        }
    }
    if (oit && (!result || resultIsOpaque))
        result = oit;
    return result;
}
} // namespace renderer
