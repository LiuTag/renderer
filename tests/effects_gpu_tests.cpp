/** @file @brief 双后端验证批量精灵、颜色覆盖、内描边、调色及深度接触阴影的实际像素。 */
#include "graphics_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace renderer;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
int main() {
    try {
        for (bool threaded : {false, true}) {
            auto window =
                graphicsTestWindow({.width = 192, .height = 192, .visible = false, .vsync = false});
            auto engine = graphicsTestRenderer(window, {.renderThread = threaded});
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto camera = scene.createNode(scene.root());
            scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                              .orthographicHeight = 6});
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 8}));
            TextureDesc pixels{8, 8, std::vector<uint8_t>(256), false};
            pixels.sampling.mipmapped = false;
            pixels.sampling.magFilter = TextureFilter::Nearest;
            for (size_t y = 1; y < 7; ++y)
                for (size_t x = 1; x < 7; ++x) {
                    auto i = (y * 8 + x) * 4;
                    pixels.pixels[i] = 255;
                    pixels.pixels[i + 3] = 255;
                }
            auto sprite = assets.createSprite({.texture = assets.createTexture(pixels),
                                               .rect = {0, 0, 8, 8},
                                               .pivot = {.5f, .5f},
                                               .pixelsPerUnit = 4});
            BatchSprite instance{SpriteRenderer{.sprite = sprite}, Mat4::identity(), 77};
            SpriteBatch batch;
            batch.replace(assets, std::span{&instance, 1});
            RenderView view{.cameraNode = camera, .shadows = false};
            auto render = [&] {
                batch.replace(assets, std::span{&instance, 1});
                engine.render(extractor.extract(scene, assets, std::span{&batch, 1}), view);
                return engine.readback();
            };
            auto base = render();
            const auto center = (size_t(96) * 192 + 96) * 4;
            require(base.pixels[center] > 240 && base.pixels[center + 1] < 10,
                    "Batch sprite center is not red");
            instance.renderer.colorOverlay = {1, 1, 1, 1};
            auto flash = render();
            require(flash.pixels[center + 1] > 240, "Overlay failed");
            instance.renderer.colorOverlay = {};
            instance.renderer.outlineColor = {0, 1, 0, 1};
            instance.renderer.outlineWidth = 1;
            auto outlined = render();
            size_t green = 0;
            for (size_t i = 0; i < outlined.pixels.size(); i += 4)
                green += outlined.pixels[i + 1] > 200 && outlined.pixels[i] < 20;
            require(green > 10, "Inner outline missing");
            instance.renderer.outlineWidth = 0;
            require(render().pixels == base.pixels, "Disabled sprite effects changed output");
            view.colorMultiply = {0, 1, 1};
            auto graded = render();
            require(graded.pixels[center] < 10, "Color grading failed");
            view.colorMultiply = {1, 1, 1};
            scene.setLocalTransform(camera, *Mat4::lookAt({3, 3, 5}, {0, 0, 0}).inverse());
            auto white = assets.createMaterial({.baseColor = {.7f, .7f, .7f, 1}, .unlit = true});
            auto floor = scene.createNode(scene.root());
            scene.setComponent(floor, MeshRenderer{assets.createMesh(geometry::plane(8)), {white}});
            auto cube = scene.createNode(scene.root());
            scene.setComponent(cube, MeshRenderer{assets.createMesh(geometry::cube()), {white}});
            scene.setLocalTransform(cube, Mat4::translation({0, .5f, 0}));
            auto snapshot = extractor.extract(scene, assets);
            engine.render(snapshot, view);
            auto unshadowed = engine.readback();
            view.contactShadows = true;
            view.contactShadowDistance = 2;
            view.contactShadowThickness = .3f;
            view.contactShadowSteps = 32;
            engine.render(snapshot, view);
            auto shadowed = engine.readback();
            size_t darker = 0;
            for (size_t i = 0; i < shadowed.pixels.size(); i += 4)
                darker += int(unshadowed.pixels[i]) - int(shadowed.pixels[i]) > 3;
            std::cout << "contact darker pixels=" << darker << '\n';
            require(darker > 5, "Depth contact shadows missing");
            view.contactShadows = false;
            engine.render(snapshot, view);
            require(engine.readback().pixels == unshadowed.pixels,
                    "Disabling contact shadows did not restore output");
            engine.present();
            engine.waitIdle();
            require(engine.debugErrorCount() == 0, "GPU effect validation errors");
        }
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
