/** @file @brief 提供覆盖层快照中的内部纹理引用，不导出到 SDK。 */
#ifndef RENDERER_SRC_INTERNAL_OVERLAY_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_OVERLAY_HPP_INCLUDED
#include "assets.hpp"
#include "renderer/overlay.hpp"
namespace renderer::detail {
struct OverlayAccess {
    static std::shared_ptr<const TextureData> texture(const OverlaySnapshot &snapshot, size_t draw);
};
} // namespace renderer::detail
#endif // RENDERER_SRC_INTERNAL_OVERLAY_HPP_INCLUDED
