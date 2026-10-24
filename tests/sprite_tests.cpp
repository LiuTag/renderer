#include "sprite_fixture.hpp"
using namespace sprite_test;
int main() {
    const std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"public sprite view retains immutable render data",
         [] {
             SpriteViewSnapshot retained;
             {
                 Fixture f;
                 const auto node = f.add();
                 const auto snapshot = f.extractor.extract(f.scene, f.assets);
                 retained = snapshot.spriteView({.cameraNode = f.camera}, 200, 200);
                 const auto frame = f.frame();
                 const auto items = retained.sprites();
                 require(items.size() == 1 && items[0].node == node &&
                         retained.viewportSize() == std::pair{200u, 200u});
                 require(items[0].world.elements == frame.instances[0].world.elements);
                 near(items[0].uvRect.z, frame.instances[0].uvRect.z);
                 auto c = *f.scene.component<SpriteRenderer>(node);
                 c.flipX = true;
                 f.scene.setComponent(node, c);
                 near(retained.sprites()[0].uvRect.z, .5f);
                 f.assets.destroySprite(f.first);
                 f.assets.destroyTexture(f.texture);
                 throws([&] { snapshot.spriteView({.cameraNode = f.camera}, 0, 200); });
             }
             auto copy = retained;
             retained = {};
             require(copy.sprites()[0].texture->pixels.size() == 64 && retained.sprites().empty());
             require(retained.viewportSize() == std::pair{0u, 0u});
             require((copy.viewProjection() * copy.inverseViewProjection()).isFinite());
         }},
        {"sprite metadata and atlas validation",
         [] {
             Fixture f;
             auto d = f.assets.spriteDescription(f.first);
             near(d.originalSize.x, 2);
             near(d.originalSize.y, 4);
             auto bad = d;
             bad.rect.x = 4;
             throws([&] { f.assets.createSprite(bad); });
             bad = d;
             bad.trimOffset.x = 1;
             throws([&] { f.assets.createSprite(bad); });
             bad = d;
             bad.pixelsPerUnit = 0;
             throws([&] { f.assets.createSprite(bad); });
             bad = d;
             bad.pivot.y = std::numeric_limits<float>::quiet_NaN();
             throws([&] { f.assets.createSprite(bad); });
             AssetStore other;
             bad = d;
             bad.texture = other.createTexture({1, 1, {255, 255, 255, 255}});
             throws([&] { f.assets.createSprite(bad); });
             const std::vector<SpriteAtlasEntry> entries{{"a", d}, {"b", d}};
             SpriteAtlas atlas(f.assets, f.texture, entries);
             require(atlas.size() == 2 && atlas.sprite("a") != atlas.sprite("b"));
             throws([&] { atlas.sprite("missing"); });
             auto duplicate = entries;
             duplicate[1].name = "a";
             throws([&] { SpriteAtlas invalid(f.assets, f.texture, duplicate); });
             auto n = f.add();
             auto c = *f.scene.component<SpriteRenderer>(n);
             c.sizeMultiplier.x = -1;
             throws([&] { f.scene.setComponent(n, c); });
             f.scene.removeComponent<SpriteRenderer>(n);
             require(!f.scene.component<SpriteRenderer>(n));
         }},
        {"atlas UV and tint vary within one instanced batch",
         [] {
             Fixture f;
             f.add(f.first, {-1, 0, 0}, {1, 0, 0, 1});
             f.add(f.second, {1, 0, 0}, {0, 1, 0, .5f});
             const auto p = f.frame();
             require(p.stats.visibleInstances == 2 && p.stats.sceneDrawCalls == 1 &&
                     p.instances.size() == 2);
             near(std::abs(p.instances[0].uvRect.x - p.instances[1].uvRect.x), .5f);
             near(p.instances[0].uvRect.z, .5f);
             near(p.instances[0].tint.x + p.instances[1].tint.x, 1);
         }},
        {"trim and flip preserve the original pivot",
         [] {
             Fixture f;
             const auto s = f.assets.createSprite({.texture = f.texture,
                                                   .rect = {0, 0, 2, 4},
                                                   .originalSize = {8, 8},
                                                   .trimOffset = {1, 2},
                                                   .pivot = {.5f, 0},
                                                   .pixelsPerUnit = 2});
             const auto n = f.add(s);
             auto p = f.frame();
             auto point = p.instances[0].world.transformPoint({-.5f, -.5f, 0});
             near(point.x, -1.5f);
             near(point.y, 1);
             auto c = *f.scene.component<SpriteRenderer>(n);
             c.flipX = true;
             f.scene.setComponent(n, c);
             p = f.frame();
             point = p.instances[0].world.transformPoint({-.5f, -.5f, 0});
             near(point.x, .5f);
             near(point.y, 1);
             near(p.instances[0].uvRect.z, -.5f);
         }},
        {"billboard is per view and keeps logical transforms",
         [] {
             Fixture f;
             const auto n = f.add();
             const auto snapshot = f.extractor.extract(f.scene, f.assets);
             const auto before = *snapshot.instanceTransform(n);
             f.scene.setLocalTransform(f.camera, *Mat4::lookAt({10, 4, 0}, {0, 1, 0}).inverse());
             auto p = f.frame();
             const auto &m = p.instances[0].world;
             near(m(1, 0), 0);
             near(m(0, 1), 0);
             near(m(2, 1), 0);
             require(m(1, 1) > 0);
             const auto logical = f.scene.worldTransform(n);
             require(logical.elements == before.elements);
             auto c = *f.scene.component<SpriteRenderer>(n);
             c.billboard = BillboardMode::ViewPlane;
             f.scene.setComponent(n, c);
             p = f.frame();
             require(std::abs(p.instances[0].world(2, 1)) < 1e-4f &&
                     std::abs(p.instances[0].world(0, 1)) > .1f);
             c.billboard = BillboardMode::Fixed;
             f.scene.setComponent(n, c);
             p = f.frame();
             near(p.instances[0].world(0, 0), 1);
             f.scene.setLocalTransform(f.camera,
                                       *Mat4::lookAt({0, 10, 0}, {0, 0, 0}, {0, 0, -1}).inverse());
             c.billboard = BillboardMode::Upright;
             f.scene.setComponent(n, c);
             require(f.frame().stats.visibleInstances == 1);
         }},
        {"sorting groups keep parts contiguous and honor member order",
         [] {
             Fixture f;
             auto group = f.scene.createNode(f.scene.root());
             f.scene.setComponent(group, SortingGroup{});
             auto first = f.add({}, {0, 0, 1}, {1, 0, 0, 1}, group);
             auto second = f.add({}, {0, 0, -1}, {0, 1, 0, 1}, group);
             auto c = *f.scene.component<SpriteRenderer>(second);
             c.sortingOrder = 1;
             f.scene.setComponent(second, c);
             f.add({}, {0, 0, -.5f}, {0, 0, 1, 1});
             auto p = f.frame();
             require(p.instances.size() == 3);
             near(p.instances[0].tint.z, 1);
             near(p.instances[1].tint.x, 1);
             near(p.instances[2].tint.y, 1);
             f.scene.removeComponent<SortingGroup>(group);
             c.sortingOrder = 0;
             f.scene.setComponent(second, c);
             p = f.frame();
             near(p.instances[0].tint.y, 1);
             f.scene.setVisible(first, false);
             require(f.frame().stats.sceneInstances == 2);
         }},
        {"sprite OIT policy and dynamic culling",
         [] {
             Fixture f;
             auto n = f.add();
             auto p = f.frame(TransparencyMode::WeightedBlendedOIT);
             require(p.batches.empty() && p.orderedSpriteBatches.size() == 1 &&
                     p.stats.transparentDrawCalls == 1);
             auto c = *f.scene.component<SpriteRenderer>(n);
             c.transparency = SpriteTransparency::FollowView;
             f.scene.setComponent(n, c);
             p = f.frame(TransparencyMode::WeightedBlendedOIT);
             require(p.batches.size() == 1 && p.orderedSpriteBatches.empty());
             f.scene.setLocalTransform(n, Mat4::translation({1000, 0, 0}));
             p = f.frame();
             require(p.stats.culledInstances == 1);
             f.scene.setLocalTransform(n, Mat4::scaling({0, 0, 0}));
             p = f.frame();
             require(p.stats.culledInstances == 1 && p.instances.empty());
         }},
    };
    size_t passed = 0;
    for (const auto &[name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " sprite CPU tests passed\n";
}
