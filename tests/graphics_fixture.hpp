/** @file @brief 为同一组像素/RHI 回归测试选择 OpenGL 或 Vulkan，实现跨后端一致性验证。 */
#ifndef RENDERER_TESTS_GRAPHICS_FIXTURE_HPP_INCLUDED
#define RENDERER_TESTS_GRAPHICS_FIXTURE_HPP_INCLUDED
#include "renderer/renderer.hpp"
#include "renderer/rhi.hpp"
#include "renderer/window.hpp"
inline renderer::WindowConfig graphicsTestConfig(renderer::WindowConfig config = {}) {
#ifdef RENDERER_TEST_VULKAN
    config.graphicsApi = renderer::GraphicsApi::Vulkan;
#endif
    return config;
}
inline renderer::Window graphicsTestWindow(renderer::WindowConfig config = {}) {
    return renderer::Window(graphicsTestConfig(config));
}
inline renderer::Renderer graphicsTestRenderer(renderer::Window &window,
                                               const renderer::RendererConfig &config = {}) {
#ifdef RENDERER_TEST_VULKAN
    return renderer::Renderer::createVulkan(window, config);
#else
    return renderer::Renderer::createOpenGL(window, config);
#endif
}
inline renderer::rhi::Device graphicsTestDevice(renderer::Window &window, bool debug = true) {
#ifdef RENDERER_TEST_VULKAN
    return renderer::rhi::Device::createVulkan(window, debug);
#else
    return renderer::rhi::Device::createOpenGL(window, debug);
#endif
}
#endif // RENDERER_TESTS_GRAPHICS_FIXTURE_HPP_INCLUDED
