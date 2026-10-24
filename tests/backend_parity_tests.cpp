/** @file @brief 同一场景快照通过 OpenGL/Vulkan 渲染，比较像素、批次数与缩放结果。 */
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace renderer;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        for (bool threaded : {false, true}) {
            Window gl({.width = 256,
                       .height = 192,
                       .title = "OpenGL parity",
                       .visible = false,
                       .vsync = false});
            Window vk({.width = 256,
                       .height = 192,
                       .title = "Vulkan parity",
                       .visible = false,
                       .vsync = false,
                       .graphicsApi = GraphicsApi::Vulkan});
            auto opengl = Renderer::createOpenGL(gl, {.renderThread = threaded});
            auto vulkan = Renderer::createVulkan(vk, {.renderThread = threaded});
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto camera = scene.createNode(scene.root());
            scene.setComponent(camera, Camera{});
            scene.setLocalTransform(camera, *Mat4::lookAt({2, 1.5f, 4}, {0, 0, 0}).inverse());
            const auto cube = assets.createMesh(geometry::cube());
            auto textured = assets.createMaterial(
                {.baseColor = {.7f, .4f, .2f, 1},
                 .baseColorTexture = assets.createTexture(geometry::checkerboard()),
                 .roughness = .7f});
            auto glass = assets.createMaterial({.baseColor = {.1f, .6f, .9f, .4f},
                                                .unlit = true,
                                                .alphaMode = AlphaMode::Blend,
                                                .doubleSided = true});
            auto node = scene.createNode(scene.root());
            scene.setComponent(node, MeshRenderer{cube, {textured}});
            auto transparent = scene.createNode(scene.root());
            scene.setComponent(transparent, MeshRenderer{cube, {glass}});
            scene.setLocalTransform(transparent, Mat4::translation({.6f, .2f, .7f}) *
                                                     Mat4::scaling({.8f, 1.2f, .2f}));
            auto plane = scene.createNode(scene.root());
            scene.setComponent(plane,
                               MeshRenderer{assets.createMesh(geometry::plane(8)), {textured}});
            scene.setLocalTransform(plane, Mat4::translation({0, -.7f, 0}));
            auto atlas = assets.createTexture(
                {2, 2, {255, 0, 0, 255, 0, 255, 0, 180, 0, 0, 255, 255, 255, 255, 255, 0}, true});
            auto sprite =
                assets.createSprite({.texture = atlas, .rect = {0, 0, 2, 2}, .pixelsPerUnit = 2});
            auto billboard = scene.createNode(scene.root());
            scene.setComponent(billboard, SpriteRenderer{.sprite = sprite});
            scene.setLocalTransform(billboard, Mat4::translation({-1, 0, .2f}));
            RenderView view;
            view.cameraNode = camera;
            view.shadowResolution = 128;
            for (int frame = 0; frame < 6; ++frame) {
                if (frame == 4) {
                    gl.resize(320, 224);
                    vk.resize(320, 224);
                    gl.pollEvents();
                    vk.pollEvents();
                }
                view.transparency = frame % 2 ? TransparencyMode::WeightedBlendedOIT
                                              : TransparencyMode::SortedAlpha;
                view.antiAliasing = frame % 3 ? AntiAliasing::FXAA : AntiAliasing::None;
                const auto snapshot = extractor.extract(scene, assets);
                const auto a = opengl.render(snapshot, view), b = vulkan.render(snapshot, view);
                require(a.drawCalls == b.drawCalls && a.visibleInstances == b.visibleInstances,
                        "Backend frame preparation differs");
                auto first = opengl.readback(), second = vulkan.readback();
                require(first.width == second.width && first.height == second.height,
                        "Backend output dimensions differ");
                double error = 0;
                size_t outliers = 0;
                for (size_t pixel = 0; pixel < first.pixels.size(); pixel += 4) {
                    int largest = 0;
                    for (size_t channel = 0; channel < 3; ++channel) {
                        const auto difference = std::abs(int(first.pixels[pixel + channel]) -
                                                         int(second.pixels[pixel + channel]));
                        error += difference;
                        largest = std::max(largest, difference);
                    }
                    outliers += largest > 8;
                }
                const auto pixels = first.pixels.size() / 4;
                const auto mean = error / double(pixels * 3);
                std::cout << "parity threaded=" << threaded << " frame=" << frame
                          << " mean=" << mean << " outliers=" << outliers << '\n';
                require(mean < 1.5 && outliers < pixels / 50,
                        "OpenGL/Vulkan pixels exceed parity tolerance");
                opengl.present();
                vulkan.present();
            }
            opengl.waitIdle();
            vulkan.waitIdle();
            require(opengl.debugErrorCount() == 0 && vulkan.debugErrorCount() == 0,
                    "Backend parity generated graphics errors");
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
