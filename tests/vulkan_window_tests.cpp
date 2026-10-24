/** @file @brief 验证 Vulkan 交换链、缩放、全屏、线程移交和底层窗口呈现入口。 */
#include "renderer/render_graph.hpp"
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <iostream>
#include <stdexcept>
using namespace renderer;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        {
            Window window({.width = 160,
                           .height = 128,
                           .title = "Vulkan low-level present",
                           .visible = false,
                           .vsync = false,
                           .graphicsApi = GraphicsApi::Vulkan});
            auto device = rhi::Device::createVulkan(window);
            require(device.capabilities().backend == "Vulkan", "Wrong device backend");
            const auto output = device.createTexture({160, 128, 1, rhi::Format::Rgba8Srgb});
            rhi::CommandEncoder encoder;
            encoder.beginRendering(
                {{{output, rhi::LoadOp::Clear, rhi::StoreOp::Store, {.2f, .4f, .6f, 1}}}});
            encoder.endRendering();
            encoder.present(output);
            auto done = device.submit(encoder.finish());
            window.present();
            device.wait(done);
            auto image = device.readTexture(output);
            require(image.pixels[0] > 100 && image.pixels[1] > 150 && image.pixels[2] > 190,
                    "Low-level sRGB clear/present failed");
            for (auto format :
                 {rhi::Format::Rgba8, rhi::Format::Rgba16Float, rhi::Format::R32Float}) {
                auto color = device.createTexture({80, 64, 1, format});
                rhi::CommandEncoder frame;
                frame.beginRendering(
                    {{{color, rhi::LoadOp::Clear, rhi::StoreOp::Store, {.2f, .4f, .6f, 1}}}});
                frame.endRendering();
                frame.present(color);
                device.submit(frame.finish());
                window.present();
                device.destroy(color);
            }
            device.waitIdle();
            require(device.debugErrorCount() == 0, "Low-level Vulkan validation errors");
        }
        for (bool threaded : {false, true}) {
            Window window({.width = 192,
                           .height = 160,
                           .title = "Vulkan swapchain tests",
                           .visible = true,
                           .vsync = false,
                           .graphicsApi = GraphicsApi::Vulkan});
            uint32_t resizeEvents = 0;
            auto subscription =
                window.subscribe<FramebufferResizeEvent>([&](const auto &) { ++resizeEvents; });
            auto renderer =
                Renderer::createVulkan(window, {.framesInFlight = 2, .renderThread = threaded});
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto camera = scene.createNode(scene.root());
            scene.setComponent(camera, Camera{});
            scene.setLocalTransform(camera, *Mat4::lookAt({0, 0, 3}, {0, 0, 0}).inverse());
            auto node = scene.createNode(scene.root());
            auto mesh = assets.createMesh(geometry::cube());
            auto material = assets.createMaterial({.baseColor = {1, .1f, .02f, 1}, .unlit = true});
            scene.setComponent(node, MeshRenderer{mesh, {material}});
            RenderView view;
            view.cameraNode = camera;
            view.shadows = false;
            for (int frame = 0; frame < 8; ++frame) {
                if (frame == 2)
                    window.resize(256, 192);
                if (frame == 4)
                    window.setMode(WindowMode::BorderlessFullscreen);
                if (frame == 5)
                    window.setMode(WindowMode::Windowed);
                if (frame == 6) {
                    window.setMode(WindowMode::ExclusiveFullscreen);
                    window.setMode(WindowMode::Windowed);
                }
                window.pollEvents();
                view.transparency = frame % 2 ? TransparencyMode::WeightedBlendedOIT
                                              : TransparencyMode::SortedAlpha;
                view.antiAliasing = frame % 2 ? AntiAliasing::FXAA : AntiAliasing::None;
                renderer.render(extractor.extract(scene, assets), view);
                renderer.present();
            }
            auto image = renderer.readback();
            const auto [w, h] = window.framebufferSize();
            require(image.width == uint32_t(w) && image.height == uint32_t(h),
                    "Resize did not rebuild Vulkan targets");
            const auto center = (size_t(image.height / 2) * image.width + image.width / 2) * 4;
            require(image.pixels[center] > 220,
                    "Vulkan rendered content was lost after swapchain rebuild");
            renderer.waitIdle();
            require(resizeEvents > 0, "Vulkan window did not dispatch resize events");
            require(renderer.debugErrorCount() == 0, "Vulkan presentation validation errors");
        }
        std::cout
            << "PASS Vulkan presentation, resize, fullscreens, thread transfer and window events\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
