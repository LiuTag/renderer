/** @file @brief 验证覆盖层输入并保存跨线程安全的几何与纹理引用。 */
#include "internal/overlay.hpp"
#include <cmath>
#include <stdexcept>
namespace renderer {
struct OverlaySnapshot::Impl {
    OverlayDesc data;
    std::vector<std::shared_ptr<const detail::TextureData>> textures;
};
OverlaySnapshot::OverlaySnapshot() = default;
OverlaySnapshot::~OverlaySnapshot() = default;
OverlaySnapshot::OverlaySnapshot(const OverlaySnapshot &) = default;
OverlaySnapshot &OverlaySnapshot::operator=(const OverlaySnapshot &) = default;
OverlaySnapshot::OverlaySnapshot(OverlaySnapshot &&) noexcept = default;
OverlaySnapshot &OverlaySnapshot::operator=(OverlaySnapshot &&) noexcept = default;
OverlaySnapshot OverlaySnapshot::create(const AssetStore &assets, OverlayDesc d) {
    if (!d.width || !d.height || d.width > 16384 || d.height > 16384 ||
        d.vertices.size() > 4000000 || d.indices.size() > 12000000 || d.draws.size() > 65536)
        throw std::invalid_argument("Invalid overlay size/capacity");
    for (const auto &v : d.vertices)
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) ||
            !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) ||
            (!finite(Vec3{v.color.x, v.color.y, v.color.z}) || !std::isfinite(v.color.w)) ||
            v.color.w < 0 || v.color.w > 1 || v.color.x < 0 || v.color.y < 0 || v.color.z < 0 ||
            v.color.x > v.color.w || v.color.y > v.color.w || v.color.z > v.color.w)
            throw std::invalid_argument("Invalid overlay vertex/linear premultiplied color");
    for (auto i : d.indices)
        if (i >= d.vertices.size())
            throw std::invalid_argument("Overlay index out of range");
    auto p = std::make_shared<Impl>();
    for (const auto &draw : d.draws) {
        if (draw.indexCount % 3 || uint64_t(draw.firstIndex) + draw.indexCount > d.indices.size())
            throw std::invalid_argument("Invalid overlay draw range");
        p->textures.push_back(draw.texture ? detail::AssetAccess::texture(assets, draw.texture)
                                           : nullptr);
    }
    p->data = std::move(d);
    OverlaySnapshot result;
    result.impl_ = std::move(p);
    return result;
}
std::pair<uint32_t, uint32_t> OverlaySnapshot::size() const {
    return impl_ ? std::pair{impl_->data.width, impl_->data.height} : std::pair{0u, 0u};
}
std::span<const OverlayVertex> OverlaySnapshot::vertices() const {
    return impl_ ? std::span<const OverlayVertex>(impl_->data.vertices)
                 : std::span<const OverlayVertex>{};
}
std::span<const uint32_t> OverlaySnapshot::indices() const {
    return impl_ ? std::span<const uint32_t>(impl_->data.indices) : std::span<const uint32_t>{};
}
std::span<const OverlayDraw> OverlaySnapshot::draws() const {
    return impl_ ? std::span<const OverlayDraw>(impl_->data.draws) : std::span<const OverlayDraw>{};
}
std::shared_ptr<const detail::TextureData> detail::OverlayAccess::texture(const OverlaySnapshot &s,
                                                                          size_t draw) {
    return s.impl_->textures.at(draw);
}
} // namespace renderer
