/**
 * @file
 * @brief 精灵及扩展模块测试共用的场景夹具，不作为 SDK 导出。
 */
#ifndef RENDERER_TESTS_SPRITE_FIXTURE_HPP_INCLUDED
#define RENDERER_TESTS_SPRITE_FIXTURE_HPP_INCLUDED
#include "internal/frame.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace renderer;
namespace sprite_test {
inline void require(bool c, const char *m = "Sprite assertion failed") {
    if (!c)
        throw std::runtime_error(m);
}
inline void near(float a, float b, float e = 1e-4f) {
    require(std::abs(a - b) < e, "Unexpected numeric result");
}
template <class F> void throws(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected an exception");
}
struct Fixture {
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    NodeHandle camera;
    TextureHandle texture;
    SpriteHandle first, second;
    Fixture() {
        texture = assets.createTexture({4,
                                        4,
                                        std::vector<uint8_t>(64, 255),
                                        false,
                                        {.magFilter = TextureFilter::Nearest, .mipmapped = false}});
        first = assets.createSprite({.texture = texture, .rect = {0, 0, 2, 4}, .pixelsPerUnit = 2});
        second =
            assets.createSprite({.texture = texture, .rect = {2, 0, 2, 4}, .pixelsPerUnit = 2});
        camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                          .orthographicHeight = 8});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 10}));
    }
    NodeHandle add(SpriteHandle s = {}, Vec3 position = {}, Vec4 tint = {1, 1, 1, 1},
                   NodeHandle parent = {}) {
        auto n = scene.createNode(parent ? parent : scene.root());
        scene.setLocalTransform(n, Mat4::translation(position));
        scene.setComponent(n, SpriteRenderer{.sprite = s ? s : first, .tint = tint});
        return n;
    }
    detail::PreparedFrame frame(TransparencyMode mode = TransparencyMode::SortedAlpha) {
        return detail::prepareFrame(extractor.extract(scene, assets),
                                    {.cameraNode = camera, .shadows = false, .transparency = mode},
                                    200, 200);
    }
};
} // namespace sprite_test

#endif // RENDERER_TESTS_SPRITE_FIXTURE_HPP_INCLUDED
