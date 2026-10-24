/** @file @brief 声明 OpenGL/Vulkan 共用的高层渲染管线工厂，仅供后端内部使用。 */
#ifndef RENDERER_SRC_INTERNAL_RENDER_BACKEND_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_RENDER_BACKEND_HPP_INCLUDED
#include "backend.hpp"
#include "renderer/rhi.hpp"
#include <stdexcept>
namespace renderer::detail {
/** @brief 接管具体 RHI 设备，创建统一的材质、阴影、透明及后处理管线。 */
std::unique_ptr<Backend> makeRenderBackend(rhi::Device device, Window &window,
                                           const RendererConfig &config);
std::unique_ptr<Backend> makeOffscreenRenderBackend(rhi::Device device,
                                                    const OffscreenConfig &target,
                                                    const RendererConfig &config);
inline void validateOffscreen(const OffscreenConfig &target) {
    if (!target.width || !target.height || target.width > 16384 || target.height > 16384)
        throw std::invalid_argument("Offscreen dimensions must be in [1,16384]");
}
} // namespace renderer::detail
#endif // RENDERER_SRC_INTERNAL_RENDER_BACKEND_HPP_INCLUDED
