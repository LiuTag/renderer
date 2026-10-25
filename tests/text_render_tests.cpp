/** @file @brief 实际回读验证世界文字遮挡、无深度队列、固定屏幕字号、图集版本、线程与后处理。 */
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
#include <stdexcept>
using namespace renderer;
namespace {
void require(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
Renderer create(bool thread) {
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen({400, 300}, {.renderThread = thread});
#else
    return Renderer::createOpenGLOffscreen({400, 300}, {.renderThread = thread});
#endif
}
std::array<int, 5> red(const Image &image) {
    std::array<int, 5> b{int(image.width), int(image.height), 0, 0, 0};
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const auto i = (size_t(y) * image.width + x) * 4;
            if (image.pixels[i] > 100 && image.pixels[i + 1] < 40 &&
                image.pixels[i] > image.pixels[i + 2] + 30) {
                b[0] = std::min(b[0], int(x));
                b[1] = std::min(b[1], int(y));
                b[2] = std::max(b[2], int(x));
                b[3] = std::max(b[3], int(y));
                ++b[4];
            }
        }
    return b;
}
} // namespace
int main() {
    try {
        for (bool threaded : {false, true}) {
            auto renderer = create(threaded);
            Scene scene;
            AssetStore assets;
            RenderExtractor extractor;
            auto camera = scene.createNode(scene.root(), "Camera");
            Camera cam;
            cam.projection = Camera::Projection::Orthographic;
            cam.orthographicHeight = 4;
            scene.setComponent(camera, cam);
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
            auto wall = scene.createNode(scene.root(), "Occluder");
            auto mesh = assets.createMesh(geometry::cube());
            auto material = assets.createMaterial({.baseColor = {0, 0, 1, 1}, .unlit = true});
            scene.setComponent(wall, MeshRenderer{mesh, {material}});
            scene.setLocalTransform(wall,
                                    Mat4::translation({0, 0, 1}) * Mat4::scaling({5, 2, .5f}));
            TextSystem text;
            const auto font = text.loadFont(RENDERER_TEST_TEXT_FONT);
            WorldTextDesc desc;
            desc.style = {.font = font, .fontSize = 48};
            desc.text = "MMMM";
            desc.color = {1, 0, 0, 1};
            desc.anchor = {.5f, .5f};
            desc.billboard = BillboardMode::Fixed;
            desc.pixelsPerUnit = 64;
            auto id = text.create(desc);
            RenderView view{.cameraNode = camera, .clearColor = {0, 0, 0, 1}, .shadows = false};
            const auto snapshot = [&](unsigned w = 400, unsigned h = 300) {
                auto batch = text.batch(scene, camera, w, h);
                return extractor.extract(scene, assets, std::span{&batch, 1});
            };
            require(red(renderer.renderToImage(snapshot(), view))[4] == 0,
                    "World text ignored opaque scene depth");
            desc.depthTest = false;
            text.setDescription(id, desc);
            for (auto mode : {TransparencyMode::SortedAlpha, TransparencyMode::WeightedBlendedOIT})
                for (auto aa : {AntiAliasing::None, AntiAliasing::FXAA}) {
                    view.transparency = mode;
                    view.antiAliasing = aa;
                    auto stats = renderer.render(snapshot(), view);
                    require(stats.visibleInstances >= 5 && stats.sceneDrawCalls >= 2,
                            "World text draw stats missing");
                    require(red(renderer.readback())[4] > 300,
                            "Unoccluded world text disappeared behind wall");
                }
            desc.depthTest = true;
            text.setDescription(id, desc);
            require(red(renderer.renderToImage(snapshot(), view))[4] == 0,
                    "World text depth toggle did not restore occlusion");
            scene.setVisible(wall, false);
            cam.projection = Camera::Projection::Perspective;
            scene.setComponent(camera, cam);
            desc.billboard = BillboardMode::ViewPlane;
            desc.sizeMode = TextSizeMode::ScreenPixels;
            desc.style.fontSize = 36;
            desc.text = "Screen 123";
            text.setDescription(id, desc);
            view.antiAliasing = AntiAliasing::None;
            auto image = renderer.renderToImage(snapshot(), view);
            auto near = red(image);
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 10}));
            auto far = red(renderer.renderToImage(snapshot(), view));
            require(near[4] > 200 && std::abs((near[2] - near[0]) - (far[2] - far[0])) <= 1 &&
                        std::abs((near[3] - near[1]) - (far[3] - far[1])) <= 1,
                    "Screen-sized text changed with camera distance");
            renderer.resizeOffscreen(640, 360);
            auto resized = red(renderer.renderToImage(snapshot(640, 360), view));
            require(std::abs((far[3] - far[1]) - (resized[3] - resized[1])) <= 2,
                    "Text pixel size changed after viewport resize");
            renderer.resizeOffscreen(400, 300);
            auto old = snapshot();
            auto future = renderer.submit(old, view);
            text.setText(id, "Atlas growth abcdefghijklmnopqrstuvwxyz");
            auto next = snapshot();
            future.get();
            require(red(renderer.readback())[4] > 200, "Atlas update damaged queued text frame");
            SpriteBatch kept;
            {
                TextSystem temporary;
                auto f = temporary.loadFont(RENDERER_TEST_TEXT_FONT);
                auto d = desc;
                d.style.font = f;
                d.text = "Kept";
                temporary.create(d);
                kept = temporary.batch(scene, camera, 400, 300);
            }
            auto detached = extractor.extract(scene, assets, std::span{&kept, 1});
            require(red(renderer.renderToImage(detached, view))[4] > 100,
                    "Text depended on destroyed font/system");
            renderer.render(next, view);
            renderer.waitIdle();
            require(renderer.debugErrorCount() == 0, "World text graphics validation failed");
        }
        std::cout << "World text GPU depth, screen size, atlas and lifetime passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
