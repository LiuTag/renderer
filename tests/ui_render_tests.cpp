/** @file @brief 真实 GPU 验证 UI 顺序/颜色/裁剪/纹理方向、字体、线程、快照保留和后处理合成。 */
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/ui/ui.hpp>
#include <stdexcept>
using namespace renderer;
namespace {
void require(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
Renderer create(bool thread) {
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen({320, 240}, {.renderThread = thread});
#else
    return Renderer::createOpenGLOffscreen({320, 240}, {.renderThread = thread});
#endif
}
void check(const Image &image) {
    auto sample = [&](unsigned x, unsigned y) {
        const auto p = (size_t(y) * image.width + x) * 4;
        return std::array<int, 3>{image.pixels[p], image.pixels[p + 1], image.pixels[p + 2]};
    };
    const double sx = double(image.width) / 320, sy = double(image.height) / 240;
    auto pixel = [&](unsigned x, unsigned y) { return sample(unsigned(x * sx), unsigned(y * sy)); };
    const auto red = pixel(20, 20), mixed = pixel(60, 60), green = pixel(170, 25),
               outside = pixel(215, 25), blueImage = pixel(12, 142);
    require(std::abs(red[0] - 188) < 5 && red[1] < 3 && red[2] < 3,
            "UI sRGB/linear alpha mismatch");
    require(std::abs(mixed[0] - 137) < 5 && mixed[1] < 3 && std::abs(mixed[2] - 188) < 5,
            "UI painter order/premultiplied blend mismatch");
    if (!(green[1] > 250 && green[0] < 3 && outside[1] < 3)) {
        image.savePng("ui-render-failure.png");
        std::cerr << "green=" << green[0] << "," << green[1] << "," << green[2]
                  << " outside=" << outside[0] << "," << outside[1] << "," << outside[2] << "\n";
    }
    require(green[1] > 250 && green[0] < 3 && outside[1] < 3, "UI rectangular clipping failed");
    require(blueImage[2] > 220 && blueImage[0] < 25 && blueImage[1] < 25,
            "UI texture row convention failed");
    unsigned text = 0;
    for (unsigned y = 170; y < 205; ++y)
        for (unsigned x = 20; x < 220; ++x) {
            const auto p = pixel(x, y);
            text += p[0] > 180 && p[1] > 180 && p[2] > 180;
        }
    require(text > 80, "UI font glyphs are missing");
}
} // namespace
int main() {
    try {
        for (bool thread : {false, true}) {
            auto renderer = create(thread);
            AssetStore assets;
            Scene scene;
            auto camera = scene.createNode(scene.root(), "Camera");
            scene.setComponent(camera, Camera{});
            RenderExtractor extractor;
            auto snapshot = extractor.extract(scene, assets);
            RenderView view{.cameraNode = camera, .clearColor = {0, 0, 0, 1}, .shadows = false};
            require(renderer.render(snapshot, view).overlayDrawCalls == 0,
                    "Empty frame rendered overlay");
            OverlaySnapshot kept;
            {
                UiContext ui({320, 240});
                ui.loadFont(RENDERER_TEST_UI_FONT);
                TextureDesc texture{
                    2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255}, true};
                texture.sampling.minFilter = texture.sampling.magFilter = TextureFilter::Nearest;
                ui.registerImage("image", std::move(texture));
                ui.loadDocumentFromMemory(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:22px;color:white;}
div,img {display:block;position:absolute;} #red {left:10px;top:10px;width:80px;height:80px;background-color:#ff000080;}
#blue {left:40px;top:40px;width:80px;height:80px;background-color:#0000ff80;}
#clip {left:150px;top:10px;width:40px;height:40px;overflow:hidden;} #inside {position:relative;width:100px;height:100px;background-color:#00ff00;}
#image {left:10px;top:140px;width:16px;height:16px;} #label {left:20px;top:170px;}
</style></head><body><div id="red"/><div id="blue"/><div id="clip"><div id="inside"/></div><img id="image" src="ui:image"/><div id="label">Game UI 123</div></body></rml>)");
                ui.update(1. / 60);
                kept = ui.snapshot();
                for (const auto &m : ui.takeMessages())
                    std::cerr << m << '\n';
            }
            view.overlay = kept;
            for (const auto aa : {AntiAliasing::None, AntiAliasing::FXAA}) {
                view.antiAliasing = aa;
                view.transparency = TransparencyMode::WeightedBlendedOIT;
                auto stats = renderer.render(snapshot, view);
                require(stats.overlayDrawCalls > 0 && stats.overlayTriangles > 8,
                        "UI stats missing");
                check(renderer.readback());
            }
            auto future = renderer.submit(snapshot, view);
            view.overlay = {};
            kept = {};
            future.get();
            check(renderer.readback());
            view.overlay = UiContext({320, 240}).snapshot();
            require(renderer.render(snapshot, view).overlayDrawCalls == 0,
                    "Overlay toggle retained stale geometry");
            UiContext ui({320, 240});
            ui.loadFont(RENDERER_TEST_UI_FONT);
            auto d = ui.loadDocumentFromMemory(
                "<rml><head><style>body{font-family:LatoLatin;font-size:20px;}div{width:40px;"
                "height:30px;background-color:#ff0000;}</style></head><body><div "
                "id=\"box\">UI</div></body></rml>");
            ui.update(0);
            view.overlay = ui.snapshot();
            renderer.resizeOffscreen(641, 479);
            auto image = renderer.renderToImage(snapshot, view);
            require(image.width == 641 && image.height == 479, "UI resize failed");
            ui.unloadDocument(d);
            auto target = renderer.createRenderTexture({32, 16});
            auto sourceView = view;
            sourceView.overlay = {};
            sourceView.clearColor = {.2f, .7f, .1f, 1};
            renderer.renderToTexture(snapshot, sourceView, target);
            OverlaySnapshot preview;
            {
                UiContext previewUi({641, 479});
                previewUi.registerImage("camera", target);
                previewUi.loadDocumentFromMemory(
                    R"(<rml><head><style>body{width:100%;height:100%;}img{display:block;position:absolute;left:10px;top:10px;width:50px;height:50px;}</style></head><body><img src="ui:camera"/></body></rml>)");
                preview = previewUi.snapshot();
            }
            view.overlay = preview;
            image = renderer.renderToImage(snapshot, view);
            auto green = image.pixels[(20 * size_t(image.width) + 20) * 4 + 1];
            require(green > 210 && green < 225, "UI live GPU texture color/placement mismatch");
            sourceView.clearColor = {0, 0, 1, 1};
            renderer.renderToTexture(snapshot, sourceView, target);
            image = renderer.renderToImage(snapshot, view);
            auto pixel = (20 * size_t(image.width) + 20) * 4;
            require(image.pixels[pixel + 2] > 250 && image.pixels[pixel + 1] < 3,
                    "UI snapshot did not retain live GPU texture after context destruction");
            renderer.waitIdle();
            require(renderer.debugErrorCount() == 0, "UI graphics validation error");
        }
        std::cout << "UI GPU composition and lifetime passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
