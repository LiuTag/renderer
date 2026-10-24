// 仅使用已安装 SDK 的公共接口验证独立扩展；不创建窗口、不依赖 OpenGL。
#include <renderer/renderer.hpp>
#if SDK_CONSUMER_ANIMATION
#include <renderer/animation/sprite_animation.hpp>
#endif
#if SDK_CONSUMER_INTERACTION
#include <renderer/interaction/picking.hpp>
#endif
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
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        const auto texture = assets.createTexture({2, 2, std::vector<uint8_t>(16, 255)});
        const auto first = assets.createSprite(
            {.texture = texture, .rect = {0, 0, 2, 2}, .pivot = {.5f, .5f}, .pixelsPerUnit = 1});
        const auto second = assets.createSprite(
            {.texture = texture, .rect = {0, 0, 2, 2}, .pivot = {.5f, .5f}, .pixelsPerUnit = 2});
        const auto node = scene.createNode(scene.root());
        scene.setComponent(node, SpriteRenderer{.sprite = first});
        const auto camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                          .orthographicHeight = 4});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 10}));
        RenderView view{.cameraNode = camera};
#if SDK_CONSUMER_ANIMATION
        SpriteAnimationClip clip({{{{first, .125, {}}, {second, .125, "step"}}}});
        SpriteAnimator animator;
        animator.play(clip);
        animator.advance(.1875);
        animator.apply(scene, node);
        require(animator.sprite() == second && animator.takeEvents().size() == 1,
                "Animation module failed");
#else
        (void)second;
#endif
        const auto snapshot = extractor.extract(scene, assets);
        const auto visible = snapshot.spriteView(view, 100, 100);
        require(visible.sprites().size() == 1 && visible.sprites()[0].texture->pixels.size() == 16,
                "Core public sprite view failed");
#if SDK_CONSUMER_INTERACTION
        SceneQuery query(snapshot, view, 100, 100);
        const auto hit = query.pickSprite({50, 50});
        require(hit && hit->node == node, "Interaction module failed");
        SceneQueryFilter filter;
        filter.excludedNodes.push_back(node);
        SceneQuery excluded(snapshot, view, 100, 100, filter);
        require(!excluded.pickSprite({50, 50}), "Interaction filter failed");
#endif
        std::cout << "Module SDK verified without OpenGL\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
