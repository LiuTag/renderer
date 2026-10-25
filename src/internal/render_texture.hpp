/** @file @brief GPU 实时纹理的设备身份和共享状态，不导出原生图形对象。 */
#ifndef RENDERER_SRC_INTERNAL_RENDER_TEXTURE_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_RENDER_TEXTURE_HPP_INCLUDED
#include "renderer/render_texture.hpp"
#include <atomic>
namespace renderer {
struct RenderTexture::Impl {
    uint64_t owner = 0, id = 0;
    uint32_t width = 0, height = 0;
    std::atomic<bool> alive{true};
};
namespace detail {
struct RenderTextureAccess {
    using State = RenderTexture::Impl;
    static std::shared_ptr<RenderTexture::Impl> state(const RenderTexture &t) {
        return t.impl_;
    }
    static RenderTexture create(uint64_t owner, uint64_t id, uint32_t width, uint32_t height) {
        RenderTexture t;
        t.impl_ = std::make_shared<RenderTexture::Impl>();
        t.impl_->owner = owner;
        t.impl_->id = id;
        t.impl_->width = width;
        t.impl_->height = height;
        return t;
    }
};
} // namespace detail
} // namespace renderer
#endif
