/** @file @brief 使用公共精灵几何和 Alpha 数据识别遮挡并渐变恢复，闪白不携带玩法语义。 */
#include <algorithm>
#include <cmath>
#include <map>
#include <renderer/presentation/presentation.hpp>
#include <set>
#include <stdexcept>
#include <tuple>
namespace renderer {
namespace {
using Key = std::tuple<uint64_t, uint32_t, uint64_t>;
Key key(NodeHandle n) {
    return {n.owner, n.index, n.generation};
}
float alpha(const TextureDesc &t, Vec2 uv) {
    const float x = uv.x * float(t.width) - .5f, y = uv.y * float(t.height) - .5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    auto at = [&](int px, int py) {
        return float(t.pixels[(size_t(std::clamp(py, 0, int(t.height) - 1)) * t.width +
                               size_t(std::clamp(px, 0, int(t.width) - 1))) *
                                  4 +
                              3]) /
               255;
    };
    if (t.sampling.magFilter == TextureFilter::Nearest)
        return at(int(std::floor(x + .5f)), int(std::floor(y + .5f)));
    const float a = x - float(x0), b = y - float(y0);
    return (at(x0, y0) * (1 - a) + at(x0 + 1, y0) * a) * (1 - b) +
           (at(x0, y0 + 1) * (1 - a) + at(x0 + 1, y0 + 1) * a) * b;
}
} // namespace
struct SpritePresentation::Impl {
    struct Entry {
        NodeHandle node;
        bool captured = false;
        float alpha = 1, factor = 1, remaining = 0, duration = 1;
        Vec4 original{}, flash{};
    };
    SpritePresentationConfig config;
    std::map<Key, Entry> entries;
};
SpritePresentation::SpritePresentation(const SpritePresentationConfig &c)
    : impl_(std::make_unique<Impl>()) {
    if (!std::isfinite(c.occludedAlpha) || c.occludedAlpha < 0 || c.occludedAlpha > 1 ||
        !std::isfinite(c.fadeSeconds) || c.fadeSeconds <= 0 || !std::isfinite(c.alphaThreshold) ||
        c.alphaThreshold < 0 || c.alphaThreshold > 1)
        throw std::invalid_argument("Invalid sprite presentation config");
    impl_->config = c;
}
SpritePresentation::~SpritePresentation() = default;
SpritePresentation::SpritePresentation(SpritePresentation &&) noexcept = default;
SpritePresentation &SpritePresentation::operator=(SpritePresentation &&) noexcept = default;
void SpritePresentation::flash(NodeHandle node, Vec4 color, float seconds) {
    if (!node || !std::isfinite(seconds) || seconds <= 0)
        throw std::invalid_argument("Invalid sprite flash");
    for (float v : {color.x, color.y, color.z, color.w})
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid flash color");
    if (color.w > 1)
        throw std::invalid_argument("Invalid flash weight");
    auto &e = impl_->entries[key(node)];
    e.node = node;
    e.flash = color;
    e.remaining = e.duration = seconds;
}
void SpritePresentation::reset(Scene &scene) {
    for (auto &[k, e] : impl_->entries) {
        (void)k;
        if (e.captured && scene.valid(e.node))
            if (auto c = scene.component<SpriteRenderer>(e.node)) {
                c->tint.w = e.alpha;
                c->colorOverlay = e.original;
                scene.setComponent(e.node, *c);
            }
    }
    impl_->entries.clear();
}
void SpritePresentation::update(Scene &scene, const RenderSnapshot &snapshot,
                                const RenderView &view, uint32_t width, uint32_t height,
                                Vec3 target, double seconds) {
    if (!finite(target) || !std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Invalid sprite presentation update");
    const auto expanded = snapshot.spriteView(view, width, height);
    const auto clip = expanded.viewProjection() * Vec4{target.x, target.y, target.z, 1};
    std::set<Key> occluded;
    if (clip.w > 0 && clip.z >= 0 && clip.z <= clip.w && std::abs(clip.x) <= clip.w &&
        std::abs(clip.y) <= clip.w) {
        const auto h =
            expanded.inverseViewProjection() * Vec4{clip.x / clip.w, clip.y / clip.w, 0, 1};
        const Vec3 origin{h.x / h.w, h.y / h.w, h.z / h.w};
        const auto delta = target - origin;
        const float distance = std::sqrt(dot(delta, delta));
        if (distance > 1e-5f) {
            const auto direction = delta * (1 / distance);
            for (const auto &s : expanded.sprites()) {
                if (!s.node || !scene.valid(s.node) || s.alphaMode != AlphaMode::Blend ||
                    !(s.layerMask & impl_->config.occluderMask) ||
                    std::find(impl_->config.excludedNodes.begin(),
                              impl_->config.excludedNodes.end(),
                              s.node) != impl_->config.excludedNodes.end())
                    continue;
                const auto inv = s.world.inverse();
                if (!inv)
                    continue;
                const auto o = inv->transformPoint(origin);
                const auto d = *inv * Vec4{direction.x, direction.y, direction.z, 0};
                if (std::abs(d.z) < 1e-8f)
                    continue;
                const float t = -o.z / d.z;
                const Vec3 local{o.x + d.x * t, o.y + d.y * t, 0};
                if (t < 0 || t >= distance - .001f || std::abs(local.x) > .5f ||
                    std::abs(local.y) > .5f)
                    continue;
                const auto previous = impl_->entries.find(key(s.node));
                const float originalAlpha =
                    previous != impl_->entries.end() && previous->second.captured
                        ? previous->second.alpha
                        : s.tint.w;
                const float coverage =
                    alpha(*s.texture, {s.uvRect.x + (local.x + .5f) * s.uvRect.z,
                                       s.uvRect.y + (local.y + .5f) * s.uvRect.w}) *
                    originalAlpha;
                if (coverage <= 0 || coverage < impl_->config.alphaThreshold)
                    continue;
                occluded.insert(key(s.node));
                auto &e = impl_->entries[key(s.node)];
                e.node = s.node;
            }
        }
    }
    for (auto i = impl_->entries.begin(); i != impl_->entries.end();) {
        auto &e = i->second;
        if (!scene.valid(e.node)) {
            i = impl_->entries.erase(i);
            continue;
        }
        auto c = scene.component<SpriteRenderer>(e.node);
        if (!c) {
            i = impl_->entries.erase(i);
            continue;
        }
        if (!e.captured) {
            e.alpha = c->tint.w;
            e.original = c->colorOverlay;
            e.captured = true;
        }
        const float wanted = occluded.contains(i->first) ? impl_->config.occludedAlpha : 1;
        const float step = float(std::min(seconds / double(impl_->config.fadeSeconds), 1.0)) *
                           (1 - impl_->config.occludedAlpha);
        e.factor += std::clamp(wanted - e.factor, -step, step);
        c->tint.w = e.alpha * e.factor;
        if (e.remaining > 0) {
            e.remaining = std::max(0.f, e.remaining - float(seconds));
            const float t = e.remaining / e.duration;
            // 按最终覆盖贡献插值，原本已有覆盖色时也能平滑恢复，避免结束瞬间跳色。
            const float originalWeight = e.original.w * (1 - t), flashWeight = e.flash.w * t;
            const float weight = originalWeight + flashWeight;
            c->colorOverlay =
                weight > 0
                    ? Vec4{(e.original.x * originalWeight + e.flash.x * flashWeight) / weight,
                           (e.original.y * originalWeight + e.flash.y * flashWeight) / weight,
                           (e.original.z * originalWeight + e.flash.z * flashWeight) / weight,
                           weight}
                    : e.original;
        } else
            c->colorOverlay = e.original;
        if (e.factor == 1 && e.remaining == 0) {
            c->tint.w = e.alpha;
            c->colorOverlay = e.original;
            scene.setComponent(e.node, *c);
            i = impl_->entries.erase(i);
        } else {
            scene.setComponent(e.node, *c);
            ++i;
        }
    }
}
} // namespace renderer
