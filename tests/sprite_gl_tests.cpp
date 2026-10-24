#include "graphics_fixture.hpp"
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
std::array<int, 3> pixel(const Image &image, int x, int y) {
    const auto offset = (size_t(y) * image.width + size_t(x)) * 4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}
int encoded(float v) {
    return int(
        std::lround(255 * (v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - .055f)));
}
} // namespace
int main(int argc, char **argv) {
    try {
        for (bool threaded : {false, true}) {
            auto window = graphicsTestWindow({.width = 200,
                                              .height = 200,
                                              .title = "Sprite GPU tests",
                                              .visible = false,
                                              .vsync = false});
            auto renderer = graphicsTestRenderer(
                window, {.initialInstanceCapacity = 1, .renderThread = threaded});
            AssetStore assets;
            const auto [clientWidth, clientHeight] = window.clientSize();
            require(clientWidth > 0 && clientHeight > 0, "Client size query failed");
            Scene scene;
            RenderExtractor extractor;
            TextureDesc texture{4,
                                2,
                                std::vector<uint8_t>(32, 255),
                                false,
                                {.minFilter = TextureFilter::Nearest,
                                 .magFilter = TextureFilter::Nearest,
                                 .mipmapped = false}};
            for (uint32_t y = 0; y < 2; ++y)
                for (uint32_t x = 0; x < 4; ++x) {
                    const auto offset = (y * 4 + x) * 4;
                    texture.pixels[offset] = x < 2 ? 255 : 0;
                    texture.pixels[offset + 1] = x < 2 ? 0 : 255;
                    texture.pixels[offset + 2] = 0;
                }
            const auto atlas = assets.createTexture(texture);
            const auto red = assets.createSprite(
                {.texture = atlas, .rect = {0, 0, 2, 2}, .pivot = {.5f, .5f}, .pixelsPerUnit = 2});
            const auto green = assets.createSprite(
                {.texture = atlas, .rect = {2, 0, 2, 2}, .pivot = {.5f, .5f}, .pixelsPerUnit = 2});
            const auto camera = scene.createNode(scene.root());
            Camera c;
            c.projection = Camera::Projection::Orthographic;
            c.orthographicHeight = 2;
            scene.setComponent(camera, c);
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
            const auto a = scene.createNode(scene.root()), b = scene.createNode(scene.root());
            scene.setComponent(a, SpriteRenderer{.sprite = red});
            scene.setComponent(b, SpriteRenderer{.sprite = green});
            scene.setLocalTransform(a, Mat4::translation({-.5f, 0, 0}));
            scene.setLocalTransform(b, Mat4::translation({.5f, 0, 0}));
            RenderView view{.cameraNode = camera, .clearColor = {0, 0, 0, 1}, .shadows = false};
            const auto [w, h] = window.framebufferSize();
            const auto render = [&] {
                return renderer.render(extractor.extract(scene, assets), view);
            };
            auto stats = render();
            auto image = renderer.readback();
            auto left = pixel(image, w / 4, h / 2), right = pixel(image, w * 3 / 4, h / 2);
            require(left[0] > 250 && left[1] < 2 && right[0] < 2 && right[1] > 250,
                    "Atlas UV rectangles were not instanced correctly");
            require(stats.sceneDrawCalls == 1 && stats.visibleInstances == 2,
                    "Compatible sprites did not batch");
            auto ar = *scene.component<SpriteRenderer>(a);
            ar.tint = {.25f, 1, 1, .5f};
            scene.setComponent(a, ar);
            render();
            image = renderer.readback();
            left = pixel(image, w / 4, h / 2);
            require(std::abs(left[0] - encoded(.125f)) <= 2,
                    "Per-instance linear tint or alpha is incorrect");

            // 低矮前景与高大背景重叠：包围盒中心会给出相反次序，落地点必须保持红色在前。
            auto rd = assets.spriteDescription(red);
            rd.pivot = {.5f, 0};
            rd.pixelsPerUnit = 1;
            const auto footRed = assets.createSprite(rd);
            rd.rect.x = 2;
            const auto footGreen = assets.createSprite(rd);
            ar = {.sprite = footRed};
            auto br = SpriteRenderer{.sprite = footGreen};
            br.sizeMultiplier.y = 2;
            scene.setComponent(a, ar);
            scene.setComponent(b, br);
            scene.setLocalTransform(a, Mat4::translation({0, 0, .2f}));
            scene.setLocalTransform(b, Mat4::identity());
            c.orthographicHeight = 5;
            scene.setComponent(camera, c);
            scene.setLocalTransform(camera, *Mat4::lookAt({0, 4, 8}, {0, 0, 0}).inverse());
            const auto snapshot = extractor.extract(scene, assets);
            const auto spriteView = snapshot.spriteView(view, uint32_t(w), uint32_t(h));
            const auto clip = spriteView.viewProjection() * Vec4{0, .8f, .2f, 1};
            const auto p =
                std::optional<Vec3>{{(clip.x / clip.w * .5f + .5f) * float(w),
                                     (.5f - clip.y / clip.w * .5f) * float(h), clip.z / clip.w}};
            renderer.render(snapshot, view);
            image = renderer.readback();
            auto sample = pixel(image, int(p->x), int(p->y));
            require(sample[0] > 250 && sample[1] < 2, "Sprite footpoint sorting is wrong");
            view.transparency = TransparencyMode::WeightedBlendedOIT;
            stats = render();
            image = renderer.readback();
            sample = pixel(image, int(p->x), int(p->y));
            require(sample[0] > 250 && sample[1] < 2 && stats.transparentDrawCalls == 1,
                    "OIT swallowed ordered sprites");
            ar.transparency = br.transparency = SpriteTransparency::FollowView;
            scene.setComponent(a, ar);
            scene.setComponent(b, br);
            render();
            image = renderer.readback();
            sample = pixel(image, int(p->x), int(p->y));
            require(sample[0] > 30 && sample[1] > 30, "FollowView sprites did not enter OIT");
            scene.setVisible(b, false);
            ar.transparency = SpriteTransparency::Sorted;
            scene.setComponent(a, ar);
            view.antiAliasing = AntiAliasing::FXAA;
            stats = render();
            require(stats.antiAliasingDrawCalls == 1, "Sprite FXAA failed");
            scene.setLocalTransform(camera, *Mat4::lookAt({8, 4, 0}, {0, 1, 0}).inverse());
            stats = render();
            require(stats.visibleInstances == 1, "Rotated billboard was culled");
            const auto logical = scene.worldTransform(a);
            require(logical.elements == Mat4::translation({0, 0, .2f}).elements,
                    "Billboard modified scene transform");

            // 单个透明角落：画面与 CPU Alpha 拾取同时跳过，再验证翻转后的角落位置。
            view.transparency = TransparencyMode::SortedAlpha;
            view.antiAliasing = AntiAliasing::None;
            c.orthographicHeight = 2;
            scene.setComponent(camera, c);
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
            const auto hole = assets.createTexture(
                {2,
                 2,
                 {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 255, 0, 0, 255},
                 false,
                 {.minFilter = TextureFilter::Nearest,
                  .magFilter = TextureFilter::Nearest,
                  .mipmapped = false}});
            const auto holeSprite = assets.createSprite(
                {.texture = hole, .rect = {0, 0, 2, 2}, .pivot = {.5f, .5f}, .pixelsPerUnit = 1});
            ar = {.sprite = holeSprite};
            scene.setComponent(a, ar);
            scene.setLocalTransform(a, Mat4::identity());
            render();
            image = renderer.readback();
            auto corner = pixel(image, w / 4, h / 4);
            require(corner[0] == 0, "Transparent sprite corner rendered opaque");
            ar.flipX = true;
            scene.setComponent(a, ar);
            render();
            image = renderer.readback();
            require(pixel(image, w / 4, h / 4)[0] > 250 && pixel(image, w * 3 / 4, h / 4)[0] == 0,
                    "Flip did not update UV");
            auto maskDesc = assets.spriteDescription(holeSprite);
            maskDesc.alphaMode = AlphaMode::Mask;
            ar.sprite = assets.createSprite(maskDesc);
            ar.tint.w = 0;
            ar.castShadow = true;
            scene.setComponent(a, ar);
            view.shadows = true;
            stats = render();
            image = renderer.readback();
            require(stats.shadowDrawCalls == 1 && pixel(image, w / 4, h * 3 / 4)[0] == 0,
                    "Mask tint or shadow sprite pipeline failed");

            // 排队后销毁全部源对象仍可完成绘制；实例缓冲扩容、模式切换及窗口尺寸变化一并覆盖。
            ar.sprite = holeSprite;
            ar.tint.w = 1;
            ar.castShadow = false;
            scene.setComponent(a, ar);
            view.shadows = false;
            auto retained = extractor.extract(scene, assets);
            assets.destroySprite(holeSprite);
            assets.destroyTexture(hole);
            auto future = renderer.submit(retained, view);
            require(future.get().visibleInstances == 1, "Snapshot lost sprite resources");
            window.resize(123, 87);
            window.pollEvents();
            renderer.render(retained, view);
            image = renderer.readback();
            if (argc > 1) {
                std::filesystem::create_directories(argv[1]);
                image.savePpm(
                    (std::filesystem::path(argv[1]) / (threaded ? "threaded.ppm" : "regular.ppm"))
                        .string());
            }
            require(renderer.debugErrorCount() == 0, "Sprite rendering produced GL errors");
            renderer.waitIdle();
            std::cout << "PASS sprite GPU "
                         "atlas/tint/sort/OIT/FXAA/camera/alpha/mask/lifetime/resize threaded="
                      << threaded << '\n';
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
