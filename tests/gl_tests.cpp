#include "graphics_fixture.hpp"
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
Vec3 pixel(const Image &image, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * image.width + x) * 4;
    return {static_cast<float>(image.pixels[offset]), static_cast<float>(image.pixels[offset + 1]),
            static_cast<float>(image.pixels[offset + 2])};
}
NodeHandle camera(Scene &scene) {
    const auto node = scene.createNode(scene.root());
    scene.setLocalTransform(node, *Mat4::lookAt({0, 0, 3}, {0, 0, 0}).inverse());
    scene.setComponent(node, Camera{});
    return node;
}
NodeHandle object(Scene &scene, MeshHandle mesh, MaterialInstanceHandle material,
                  Mat4 transform = Mat4::identity()) {
    auto node = scene.createNode(scene.root());
    scene.setLocalTransform(node, transform);
    scene.setComponent(node, MeshRenderer{mesh, {material}});
    return node;
}
} // namespace
int main() {
    try {
        auto window = graphicsTestWindow({.width = 256,
                                          .height = 256,
                                          .title = "Renderer GPU tests",
                                          .visible = false,
                                          .vsync = false});
        auto renderer =
            graphicsTestRenderer(window, {.framesInFlight = 2, .initialInstanceCapacity = 1});
        std::cout << renderer.deviceInfo().device << '\n';
        {
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            const auto cam = camera(scene);
            const auto mesh = assets.createMesh(geometry::cube());
            const auto blue = assets.createMaterial({.baseColor = {0, 0, 1, 1}, .unlit = true});
            const auto red = assets.createMaterial({.baseColor = {1, 0, 0, 1}, .unlit = true});
            object(scene, mesh, red);
            object(scene, mesh, blue, Mat4::translation({0, 0, -1}));
            for (int i = 0; i < 8; ++i) {
                const auto stats = renderer.render(extractor.extract(scene, assets),
                                                   {.cameraNode = cam, .clearColor = {0, 0, 0, 1}});
                require(stats.sceneDrawCalls == 2 && stats.visibleInstances == 2,
                        "Depth test scene preparation failed");
                const auto image = renderer.readback();
                const auto center = pixel(image, 128, 128), background = pixel(image, 5, 5);
                require(center.x > 245 && center.y < 5 && center.z < 5,
                        "Depth/instance offset test: foreground red missing");
                require(background.x < 5 && background.y < 5 && background.z < 5,
                        "Clear color incorrect");
            }
            std::cout << "PASS depth, sub-batch offsets, frame-ring reuse and readback\n";
        }
        {
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            const auto cam = camera(scene);
            const auto mesh = assets.createMesh(geometry::cube());
            const auto green = assets.createMaterial({.baseColor = {0, 1, 0, 1}, .unlit = true});
            for (int i = 0; i < 48; ++i)
                object(scene, mesh, green, Mat4::scaling({-1, 1, 1}));
            const auto stats =
                renderer.render(extractor.extract(scene, assets), {.cameraNode = cam});
            const auto center = pixel(renderer.readback(), 128, 128);
            require(stats.sceneDrawCalls == 1 && stats.visibleInstances == 48,
                    "Instances did not merge into one draw");
            require(center.y > 245 && center.x < 5 && center.z < 5,
                    "Mirrored front-face handling failed");
            std::cout << "PASS instancing, stream growth and mirrored winding\n";
        }
        {
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            const auto cam = camera(scene);
            MeshDesc quad{{{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                           {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                           {{1, 1, 0}, {0, 0, 1}, {1, 1}},
                           {{-1, 1, 0}, {0, 0, 1}, {0, 1}}},
                          {0, 1, 2, 0, 2, 3},
                          {}};
            const auto mesh = assets.createMesh(std::move(quad));
            TextureDesc texture{8, 8, std::vector<uint8_t>(8 * 8 * 4), true};
            for (uint32_t y = 0; y < 8; ++y)
                for (uint32_t x = 0; x < 8; ++x) {
                    const size_t i = (size_t{y} * 8 + x) * 4;
                    texture.pixels[i] = (y < 4 && x < 4) || (y >= 4 && x >= 4) ? 255 : 0;
                    texture.pixels[i + 1] = x >= 4 ? 255 : 0;
                    texture.pixels[i + 2] = y >= 4 ? 255 : 0;
                    texture.pixels[i + 3] = 255;
                }
            const auto tex = assets.createTexture(std::move(texture));
            const auto material = assets.createMaterial({.baseColorTexture = tex, .unlit = true});
            const auto node = object(scene, mesh, material);
            renderer.render(extractor.extract(scene, assets), {.cameraNode = cam});
            auto image = renderer.readback();
            const auto topLeft = pixel(image, 90, 90), bottomLeft = pixel(image, 90, 166);
            require(topLeft.z > 245 && topLeft.x < 5, "Texture top-left orientation incorrect");
            require(bottomLeft.x > 245 && bottomLeft.z < 5,
                    "Texture bottom-left orientation incorrect");
            const auto gray =
                assets.createMaterial({.baseColor = {0.25f, 0.25f, 0.25f, 1}, .unlit = true});
            scene.setComponent(node, MeshRenderer{mesh, {gray}});
            renderer.render(extractor.extract(scene, assets), {.cameraNode = cam});
            const auto center = pixel(renderer.readback(), 128, 128);
            require(center.x > 132 && center.x < 142, "Linear-to-sRGB output conversion incorrect");
            std::cout << "PASS texture upload, UV orientation and sRGB output\n";
        }
        {
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            const auto cam = camera(scene);
            for (int i = 0; i < 5; ++i)
                renderer.render(extractor.extract(scene, assets), {.cameraNode = cam});
            renderer.waitIdle();
            const auto stats =
                renderer.render(extractor.extract(scene, assets), {.cameraNode = cam});
            require(stats.gpuMeshes == 0 && stats.gpuTextures == 0 && stats.retiredResources == 0,
                    "Unused GPU resources were not reclaimed");
            std::cout << "PASS GPU resource retirement after completion\n";
        }
        renderer.waitIdle();
        require(renderer.debugErrorCount() == 0, "graphics debug output reported errors");
        std::cout << "All GPU tests passed; zero graphics debug errors\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "GPU TEST FAILED: " << error.what() << '\n';
        return 1;
    }
}
