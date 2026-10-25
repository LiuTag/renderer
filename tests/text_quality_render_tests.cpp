/** @file @brief 真实像素验证 SDF 缩放抗锯齿、外描边、Alpha 淡变及淘汰后的旧快照。 */
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
using namespace renderer;
namespace {
void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
Renderer create(bool threaded) {
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen({800, 360}, {.renderThread = threaded});
#else
    return Renderer::createOpenGLOffscreen({800, 360}, {.renderThread = threaded});
#endif
}
} // namespace
int main() {
    try {
        for (bool threaded : {false, true}) {
            auto gpu = create(threaded);
            TextSystem text;
            auto font = text.loadFont(RENDERER_TEST_FONT);
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto camera = scene.createNode(scene.root());
            scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                              .orthographicHeight = 4});
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
            WorldTextDesc d;
            d.style.font = font;
            d.style.rasterMode = TextRasterMode::Sdf;
            d.style.fontSize = 28;
            d.text = "TEXT";
            d.sizeMode = TextSizeMode::ScreenPixels;
            d.anchor = {0, 0};
            d.transform = Mat4::translation({-3.5f, 1.1f, 0});
            auto small = text.create(d);
            d.style.fontSize = 112;
            d.transform = Mat4::translation({-3.5f, -1.4f, 0});
            auto large = text.create(d);
            auto batch = text.batch(scene, camera, 800, 360);
            auto snapshot = extractor.extract(scene, assets, std::span(&batch, 1));
            RenderView view;
            view.cameraNode = camera;
            view.shadows = false;
            view.clearColor = {0, 0, 0, 1};
            gpu.render(snapshot, view);
            auto image = gpu.readback();
            uint64_t white = 0, gray = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4) {
                white += image.pixels[i] > 240;
                gray += image.pixels[i] > 10 && image.pixels[i] < 230;
            }
            require(white > 3000 && gray > 200,
                    "SDF glyph fill or derivative antialiasing missing");
            auto count = text.statistics().cachedGlyphs;
            d = text.description(large);
            d.style.outlineColor = {1, 0, 0, 1};
            d.style.outlineWidth = 3;
            text.setDescription(large, d);
            auto outlined = text.batch(scene, camera, 800, 360);
            auto outlineSnapshot = extractor.extract(scene, assets, std::span(&outlined, 1));
            gpu.render(outlineSnapshot, view);
            auto border = gpu.readback();
            uint64_t red = 0;
            for (size_t i = 0; i < border.pixels.size(); i += 4)
                red += border.pixels[i] > 100 && border.pixels[i + 1] < 70;
            require(red > 300 && text.statistics().cachedGlyphs == count,
                    "SDF outline did not expand coverage or rerasterized glyphs");
            text.setVisible(small, false);
            text.setVisible(large, false);
            text.trimCache(0);
            require(!text.statistics().atlasPages, "Hidden SDF atlas not reclaimed");
            gpu.render(snapshot, view);
            require(gpu.readback().pixels == image.pixels,
                    "Cache eviction changed an old SDF frame");
            gpu.waitIdle();
            require(!gpu.debugErrorCount(), "SDF graphics validation errors");
        }
        std::cout << "SDF quality on GPU passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
