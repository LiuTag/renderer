/** @file @brief 验证真实异步传输合并、暂存复用、R8 奇数区域、COW 页面与旧快照逐像素保留。 */
#include <array>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/rhi.hpp>
#include <renderer/text/text.hpp>
using namespace renderer;
namespace {
void need(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void throws(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    need(caught, "Expected validation failure");
}
rhi::Device makeDevice() {
#ifdef RENDERER_TEST_VULKAN
    return rhi::Device::createVulkanOffscreen();
#else
    return rhi::Device::createOpenGLOffscreen();
#endif
}
Renderer makeRenderer() {
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen({400, 240});
#else
    return Renderer::createOpenGLOffscreen({400, 240});
#endif
}
} // namespace
int main() {
    try {
        {
            auto device = makeDevice();
            auto before = device.uploadStatistics();
            device.beginUploadBatch();
            throws([&] { device.beginUploadBatch(); });
            auto t = device.createTexture({17, 9, 1, rhi::Format::R8, "Odd R8", 3, true});
            for (uint32_t layer = 0; layer < 3; ++layer) {
                std::vector<uint8_t> values(17 * 9, uint8_t(30 + layer * 50));
                device.upload(t, values, 0, layer);
            }
            std::vector<uint8_t> patch(7 * 3, 220);
            device.uploadRegion(t, {3, 2, 7, 3}, patch, 0, 1);
            device.copyTexture(t, t,
                               {.width = 17, .height = 9, .sourceLayer = 1, .destinationLayer = 2});
            auto view = device.createTextureView(t, 0, 1, 0, 3, true);
            device.destroy(t);
            t = view;
            auto token = device.endUploadBatch();
            device.wait(token);
            auto read = device.readTexture(t, 2);
            need(read.pixels[((9 - 1 - 2) * 17 + 3) * 4] == 220 && read.pixels[0] == 80,
                 "R8 region/copy crossed rows or layers");
            auto after = device.uploadStatistics();
            need(after.calls - before.calls == 4 && after.waits == before.waits,
                 "Batched uploads blocked the CPU");
#ifdef RENDERER_TEST_VULKAN
            need(after.submissions - before.submissions == 1 &&
                     after.stagingAllocations - before.stagingAllocations == 1 &&
                     after.stagingReuses > before.stagingReuses,
                 "Vulkan transfers were not merged into one pooled batch");
#endif
            device.beginUploadBatch();
            throws([&] { device.uploadRegion(t, {16, 8, 2, 1}, patch); });
            device.waitIdle();
            device.endUploadBatch();
            device.destroy(t);
            need(!device.debugErrorCount(), "Transfer validation error");
        }
        auto gpu = makeRenderer();
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        auto cam = scene.createNode(scene.root());
        scene.setComponent(
            cam, Camera{.projection = Camera::Projection::Orthographic, .orthographicHeight = 4});
        scene.setLocalTransform(cam, Mat4::translation({0, 0, 8}));
        RenderView view;
        view.cameraNode = cam;
        view.shadows = false;
        view.clearColor = {0, 0, 0, 1};
        TextSystem text;
        auto font = text.loadFont(RENDERER_TEST_FONT);
        WorldTextDesc label;
        label.style.font = font;
        label.style.fontSize = 60;
        label.style.rasterMode = TextRasterMode::Sdf;
        label.text = "A";
        label.depthTest = false;
        label.sizeMode = TextSizeMode::ScreenPixels;
        auto id = text.create(label);
        auto firstBatch = text.batch(scene, cam, 400, 240);
        auto first = extractor.extract(scene, assets, std::span(&firstBatch, 1));
        auto initial = gpu.render(first, view);
        auto original = gpu.readback();
        text.setText(id, "ABCDE");
        auto secondBatch = text.batch(scene, cam, 400, 240);
        auto second = extractor.extract(scene, assets, std::span(&secondBatch, 1));
        auto incremental = gpu.render(second, view);
        auto expanded = gpu.readback();
        need(incremental.assetUploadedBytes < initial.assetUploadedBytes,
             "Adding glyphs uploaded an entire atlas page");
        need(text.statistics().atlasPixelBytes == 512 * 512, "Glyph page remained RGBA8");
        gpu.render(first, view);
        need(gpu.readback().pixels == original.pixels,
             "New glyph region overwrote an old snapshot page");
        auto clean = makeRenderer();
        clean.render(second, view);
        need(clean.readback().pixels == expanded.pixels,
             "First-use full upload differs from incremental GPU copy");
        view.spatialCulling = false;
        gpu.render(second, view);
        need(gpu.readback().pixels == expanded.pixels, "Spatial optimization changed pixels");
        need(!gpu.debugErrorCount() && !clean.debugErrorCount(), "Atlas graphics validation error");
        std::cout << "Batch transfer, R8 patch and retained atlas pixels passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
