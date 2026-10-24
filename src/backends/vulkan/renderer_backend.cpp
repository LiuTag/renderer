/** @file @brief Vulkan 高层工厂；实际帧管线与 OpenGL 共用。 */
#include "../../internal/render_backend.hpp"
namespace renderer {
Renderer Renderer::createVulkan(Window &window, const RendererConfig &config) {
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->backend =
        detail::makeRenderBackend(rhi::Device::createVulkan(window, config.debug), window, config);
    return Renderer(std::move(impl));
}
Renderer Renderer::createVulkanOffscreen(const OffscreenConfig &target,
                                         const RendererConfig &config) {
    detail::validateOffscreen(target);
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->backend = detail::makeOffscreenRenderBackend(
        rhi::Device::createVulkanOffscreen(config.debug), target, config);
    return Renderer(std::move(impl));
}
} // namespace renderer
