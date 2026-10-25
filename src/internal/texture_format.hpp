/** @file @brief 共用 RHI 块格式分类与载荷大小计算，避免双后端传输长度不一致。 */
#ifndef RENDERER_SRC_INTERNAL_TEXTURE_FORMAT_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_TEXTURE_FORMAT_HPP_INCLUDED
#include "renderer/rhi.hpp"
#include "renderer/texture.hpp"
namespace renderer::rhi::detail {
inline bool compressed(Format f) {
    return f >= Format::Bc1 && f <= Format::Bc7Srgb;
}
inline TextureCompression compression(Format f) {
    switch (f) {
    case Format::Bc1:
    case Format::Bc1Srgb:
        return TextureCompression::BC1;
    case Format::Bc2:
    case Format::Bc2Srgb:
        return TextureCompression::BC2;
    case Format::Bc3:
    case Format::Bc3Srgb:
        return TextureCompression::BC3;
    case Format::Bc4:
        return TextureCompression::BC4;
    case Format::Bc5:
        return TextureCompression::BC5;
    case Format::Bc7:
    case Format::Bc7Srgb:
        return TextureCompression::BC7;
    default:
        return TextureCompression::None;
    }
}
inline uint64_t imageBytes(Format f, uint32_t w, uint32_t h) {
    if (compressed(f))
        return texture::byteSize(compression(f), w, h);
    return uint64_t(w) * h *
           (f == Format::R8            ? 1
            : f == Format::Rgba16Float ? 8
            : f == Format::Rgba32Float ? 16
                                       : 4);
}
inline bool blockCopyExtent(const TextureCopy &r, uint32_t sw, uint32_t sh, uint32_t dw,
                            uint32_t dh) {
    return (!(r.width % 4) || (r.sourceX + r.width == sw && r.destinationX + r.width == dw)) &&
           (!(r.height % 4) || (r.sourceY + r.height == sh && r.destinationY + r.height == dh));
}
inline bool array(const TextureDesc &d) {
    return d.layers > 1 || d.arrayTexture;
}
} // namespace renderer::rhi::detail
#endif
