/** @file @brief 在没有 Window/GLFW 初始化的独立进程验证双后端离屏渲染、线程迁移和 PNG 输出。 */
#include "renderer/renderer.hpp"
#include "renderer/rhi.hpp"
#include "renderer/sprite.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
using namespace renderer;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void throws(F action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected offscreen validation failure");
}
size_t windows() {
    size_t count = 0;
#ifdef _WIN32
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process == GetCurrentProcessId()) {
                wchar_t name[256]{};
                GetClassNameW(window, name, 256);
#ifndef RENDERER_TEST_VULKAN
                // NVIDIA 的 pbuffer 实现会分配隐藏的驱动 HWND；这里只限制应用窗口和可见窗口。
                const std::wstring type = name;
                if (!IsWindowVisible(window) && type.find(L"GLFW") != 0 &&
                    type.find(L"Renderer_") != 0)
                    return TRUE;
#endif
                ++*reinterpret_cast<size_t *>(context);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&count));
#endif
    return count;
}
Renderer create(const OffscreenConfig &target, const RendererConfig &config = {}) {
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen(target, config);
#else
    return Renderer::createOpenGLOffscreen(target, config);
#endif
}
rhi::Device device() {
#ifdef RENDERER_TEST_VULKAN
    return rhi::Device::createVulkanOffscreen();
#else
    return rhi::Device::createOpenGLOffscreen();
#endif
}
void check(const Image &image) {
    auto pixel = [&](float x, float y, size_t channel) {
        return image.pixels[(size_t(y * float(image.height)) * image.width +
                             size_t(x * float(image.width))) *
                                4 +
                            channel];
    };
    require(image.pixels.size() == size_t(image.width) * image.height * 4,
            "Invalid readback byte count");
    require(pixel(.5f, .34f, 0) > 245 && pixel(.5f, .34f, 2) < 5, "Top sprite must be red");
    require(pixel(.5f, .66f, 2) > 245 && pixel(.5f, .66f, 0) < 5, "Bottom sprite must be blue");
    require(pixel(.05f, .05f, 1) > 132 && pixel(.05f, .05f, 1) < 142,
            "Output must be display encoded");
}
} // namespace
int main() {
    try {
        const auto nativeWindows = windows();
        throws([] { create({0, 10}); });
        throws([] { create({16385, 10}); });
        // 低层设备可以不经过高层渲染器，直接渲染到普通纹理并回读。
        {
            auto gpu = device();
            require(windows() == nativeWindows, "Offscreen RHI retained a native window");
            auto texture = gpu.createTexture({19, 13, 1, rhi::Format::Rgba8, "Offscreen clear"});
            rhi::CommandEncoder commands;
            rhi::RenderingDesc pass;
            pass.colors.push_back(
                {texture, rhi::LoadOp::Clear, rhi::StoreOp::Store, {.25f, .5f, .75f, 1}});
            commands.beginRendering(pass);
            commands.endRendering();
            auto done = gpu.submit(commands.finish());
            gpu.wait(done);
            auto image = gpu.readTexture(texture);
            require(image.width == 19 && image.height == 13 && image.pixels[0] >= 63 &&
                        image.pixels[0] <= 64 && image.pixels[1] >= 127 && image.pixels[1] <= 128 &&
                        image.pixels[2] >= 191 && image.pixels[2] <= 192,
                    "RHI texture clear/readback failed");
            gpu.destroy(texture);
            gpu.waitIdle();
            require(gpu.debugErrorCount() == 0, "Offscreen RHI validation errors");
        }
        std::vector<uint8_t> reference;
        for (bool threaded : {false, true}) {
            auto engine = create({137, 91}, {.framesInFlight = 2, .renderThread = threaded});
            require(windows() == nativeWindows, "Offscreen renderer retained a native window");
            throws([&] { engine.readback(); });
            throws([&] { engine.present(); });
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto camera = scene.createNode(scene.root());
            scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                              .orthographicHeight = 6});
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 8}));
            TextureDesc pixels{
                2, 2, {0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255}, false};
            pixels.sampling.mipmapped = false;
            pixels.sampling.minFilter = pixels.sampling.magFilter = TextureFilter::Nearest;
            auto sprite = assets.createSprite({.texture = assets.createTexture(std::move(pixels)),
                                               .rect = {0, 0, 2, 2},
                                               .pivot = {.5f, .5f},
                                               .pixelsPerUnit = .5f});
            auto node = scene.createNode(scene.root());
            scene.setComponent(node,
                               SpriteRenderer{.sprite = sprite,
                                              .billboard = BillboardMode::Fixed,
                                              .transparency = SpriteTransparency::FollowView});
            RenderView view{.cameraNode = camera, .clearColor = {0, .25f, 0, 1}, .shadows = false};
            auto snapshot = extractor.extract(scene, assets);
            auto image = engine.renderToImage(snapshot, view);
            check(image);
            require(image.width == 137 && image.height == 91,
                    "Offscreen size depends on a window or DPI");
            if (threaded)
                require(image.pixels == reference, "Threaded capture changed pixels");
            else
                reference = image.pixels;
            std::vector<std::future<RenderStats>> pending;
            for (int i = 0; i < 6; ++i)
                pending.push_back(engine.submit(snapshot, view));
            for (auto &frame : pending)
                require(frame.get().visibleInstances == 1, "Queued frame lost sprite");
            require(engine.readback().pixels == image.pixels,
                    "Readback does not match renderToImage");
            engine.resizeOffscreen(233, 79);
            image = engine.renderToImage(snapshot, view);
            check(image);
            require(image.width == 233 && image.height == 79, "Offscreen resize failed");
            const auto before = image.pixels;
            throws([&] { engine.resizeOffscreen(0, 100); });
            throws([&] { engine.resizeOffscreen(100, 16385); });
            require(engine.renderToImage(snapshot, view).pixels == before,
                    "Rejected resize changed target");
            view.transparency = TransparencyMode::WeightedBlendedOIT;
            view.antiAliasing = AntiAliasing::FXAA;
            view.contactShadows = true;
            check(engine.renderToImage(snapshot, view));
            auto stats = engine.render(snapshot, view);
            require(stats.transparencyResolveDrawCalls == 1 && stats.antiAliasingDrawCalls == 1,
                    "Offscreen mode skipped OIT or FXAA");
            for (int i = 0; i < 4; ++i)
                engine.render(snapshot, view);
            engine.waitIdle();
            stats = engine.render(snapshot, view);
            require(std::none_of(stats.passes.begin(), stats.passes.end(),
                                 [](const PassTiming &pass) { return pass.name == "Present"; }),
                    "Offscreen graph included a Present pass");
            require(stats.gpuTimingValid, "Offscreen graph GPU timing unavailable");
            image = engine.readback();
            image.savePng(
                std::filesystem::path("offscreen-tests") /
                (engine.deviceInfo().backend + (threaded ? "-threaded.png" : "-serial.png")));
            require(engine.debugErrorCount() == 0, "Offscreen renderer validation errors");
            std::cout << engine.deviceInfo().backend << " threaded=" << threaded << " passed\n";
        }
        require(windows() == nativeWindows, "Offscreen destruction leaked native windows");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
