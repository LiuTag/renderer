/** @file @brief 自动遮挡淡出、Alpha 过滤、闪白结束和删除节点恢复测试。 */
#include "sprite_fixture.hpp"
#include <renderer/presentation/presentation.hpp>
using namespace sprite_test;
int main() {
    try {
        Fixture f;
        auto n = f.add(f.first, {0, 0, 2});
        auto c = *f.scene.component<SpriteRenderer>(n);
        c.tint.w = .8f;
        f.scene.setComponent(n, c);
        RenderView view{.cameraNode = f.camera, .shadows = false};
        SpritePresentation p;
        auto snapshot = f.extractor.extract(f.scene, f.assets);
        p.update(f.scene, snapshot, view, 200, 200, {0, .5f, 0}, .1);
        near(f.scene.component<SpriteRenderer>(n)->tint.w, .5f);
        p.update(f.scene, snapshot, view, 200, 200, {0, .5f, 0}, .1);
        near(f.scene.component<SpriteRenderer>(n)->tint.w, .2f);
        p.update(f.scene, snapshot, view, 200, 200, {3, .5f, 0}, .2);
        near(f.scene.component<SpriteRenderer>(n)->tint.w, .8f);
        p.flash(n, {1, 1, 1, 1}, .2f);
        p.update(f.scene, snapshot, view, 200, 200, {3, .5f, 0}, 0);
        near(f.scene.component<SpriteRenderer>(n)->colorOverlay.w, 1);
        p.update(f.scene, snapshot, view, 200, 200, {3, .5f, 0}, .3);
        near(f.scene.component<SpriteRenderer>(n)->colorOverlay.w, 0);
        c.colorOverlay = {1, 0, 0, .5f};
        f.scene.setComponent(n, c);
        p.flash(n, {1, 1, 1, 1}, .2f);
        p.update(f.scene, snapshot, view, 200, 200, {3, .5f, 0}, .1);
        const auto half = f.scene.component<SpriteRenderer>(n)->colorOverlay;
        near(half.x, 1);
        near(half.y, 2.f / 3);
        near(half.w, .75f);
        p.reset(f.scene);
        near(f.scene.component<SpriteRenderer>(n)->colorOverlay.w, .5f);
        near(f.scene.component<SpriteRenderer>(n)->colorOverlay.y, 0);
        p.flash(n);
        f.scene.destroySubtree(n);
        p.update(f.scene, snapshot, view, 200, 200, {3, .5f, 0}, .1);
        throws([&] { p.flash({}); });
        std::cout << "presentation tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
