#include "../../internal/render_backend.hpp"
namespace renderer {
Renderer Renderer::createOpenGL(Window &window, const RendererConfig &config) {
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->backend =
        detail::makeRenderBackend(rhi::Device::createOpenGL(window, config.debug), window, config);
    return Renderer(std::move(impl));
}
} // namespace renderer
