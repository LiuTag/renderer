#include "../../internal/render_backend.hpp"
namespace renderer {
Renderer Renderer::createOpenGL(Window &window, const RendererConfig &config) {
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->backend =
        detail::makeRenderBackend(rhi::Device::createOpenGL(window, config.debug), window, config);
    return Renderer(std::move(impl));
}
Renderer Renderer::createOpenGLOffscreen(const OffscreenConfig &target,
                                         const RendererConfig &config) {
    detail::validateOffscreen(target);
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->backend = detail::makeOffscreenRenderBackend(
        rhi::Device::createOpenGLOffscreen(config.debug), target, config);
    return Renderer(std::move(impl));
}
} // namespace renderer
