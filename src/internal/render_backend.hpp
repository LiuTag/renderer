/** @file @brief 声明 OpenGL/Vulkan 共用的高层渲染管线工厂，仅供后端内部使用。 */
#ifndef RENDERER_SRC_INTERNAL_RENDER_BACKEND_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_RENDER_BACKEND_HPP_INCLUDED
#include "backend.hpp"
#include "renderer/rhi.hpp"
namespace renderer::detail {
/** @brief 接管具体 RHI 设备，创建统一的材质、阴影、透明及后处理管线。 */
std::unique_ptr<Backend> makeRenderBackend(rhi::Device device, Window &window,
                                           const RendererConfig &config);
} // namespace renderer::detail
#endif // RENDERER_SRC_INTERNAL_RENDER_BACKEND_HPP_INCLUDED
