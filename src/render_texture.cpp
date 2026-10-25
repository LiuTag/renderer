/** @file @brief 实现实时 GPU 纹理的共享引用及线程安全存活查询。 */
#include "internal/render_texture.hpp"
namespace renderer {
RenderTexture::RenderTexture() = default;
RenderTexture::~RenderTexture() = default;
RenderTexture::RenderTexture(const RenderTexture &) = default;
RenderTexture &RenderTexture::operator=(const RenderTexture &) = default;
RenderTexture::RenderTexture(RenderTexture &&) noexcept = default;
RenderTexture &RenderTexture::operator=(RenderTexture &&) noexcept = default;
std::pair<uint32_t, uint32_t> RenderTexture::size() const {
    return impl_ ? std::pair{impl_->width, impl_->height} : std::pair{0u, 0u};
}
RenderTexture::operator bool() const {
    return impl_ && impl_->alive.load();
}
} // namespace renderer
