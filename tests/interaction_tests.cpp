#include "renderer/interaction/picking.hpp"
#include "sprite_fixture.hpp"
#include <renderer/texture.hpp>
using namespace sprite_test;
int main() {
    const std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"compressed array layer and SDF contour picking",
         [] {
             Fixture f;
             TextureDesc transparent{8, 8, std::vector<uint8_t>(256, 255), false};
             for (size_t i = 3; i < transparent.pixels.size(); i += 4)
                 transparent.pixels[i] = 0;
             auto opaque = transparent;
             for (size_t i = 3; i < opaque.pixels.size(); i += 4)
                 opaque.pixels[i] = 255;
             std::array<TextureDesc, 2> images{transparent, opaque};
             auto tex = f.assets.createTexture(
                 texture::compress(texture::makeArray(images), TextureCompression::BC3));
             auto d = f.assets.spriteDescription(f.first);
             d.texture = tex;
             d.rect = {0, 0, 8, 8};
             d.originalSize = {};
             d.pixelsPerUnit = 4;
             auto n = f.add(f.assets.createSprite(d));
             auto hit = [&] {
                 return SceneQuery(f.extractor.extract(f.scene, f.assets), {.cameraNode = f.camera},
                                   200, 200)
                     .pickSprite({100, 80});
             };
             require(!hit());
             auto c = *f.scene.component<SpriteRenderer>(n);
             c.textureLayer = 1;
             f.scene.setComponent(n, c);
             require(hit()->node == n);
             for (size_t i = 3; i < opaque.pixels.size(); i += 4)
                 opaque.pixels[i] = 100;
             d.texture = f.assets.createTexture(opaque);
             d.textureLayer = 0;
             d.distanceFieldRange = 8;
             c.textureLayer = UINT32_MAX;
             c.sprite = f.assets.createSprite(d);
             f.scene.setComponent(n, c);
             require(!hit());
             for (size_t i = 3; i < opaque.pixels.size(); i += 4)
                 opaque.pixels[i] = 180;
             d.texture = f.assets.createTexture(opaque);
             c.sprite = f.assets.createSprite(d);
             f.scene.setComponent(n, c);
             require(hit()->node == n);
         }},
        {"unoccluded world sprites match rendering priority",
         [] {
             Fixture f;
             auto opaqueDesc = f.assets.spriteDescription(f.first);
             opaqueDesc.alphaMode = AlphaMode::Opaque;
             const auto front = f.add(f.assets.createSprite(opaqueDesc), {0, 0, 1});
             const auto back = f.add(f.second, {0, 0, -1});
             auto component = *f.scene.component<SpriteRenderer>(back);
             component.depthTest = false;
             f.scene.setComponent(back, component);
             for (auto mode :
                  {TransparencyMode::SortedAlpha, TransparencyMode::WeightedBlendedOIT}) {
                 SceneQuery query(f.extractor.extract(f.scene, f.assets),
                                  {.cameraNode = f.camera, .transparency = mode}, 200, 200);
                 require(query.pickSprite({100, 80})->node == back);
             }
             component.depthTest = true;
             f.scene.setComponent(back, component);
             SceneQuery query(f.extractor.extract(f.scene, f.assets), {.cameraNode = f.camera}, 200,
                              200);
             require(query.pickSprite({100, 80})->node == front);
         }},
        {"batch IDs distinguish ordered transparency from opaque and OIT",
         [] {
             Fixture f;
             auto opaqueDesc = f.assets.spriteDescription(f.first);
             opaqueDesc.alphaMode = AlphaMode::Opaque;
             const auto opaque = f.assets.createSprite(opaqueDesc);
             std::vector<BatchSprite> data{
                 {{.sprite = opaque}, Mat4::translation({0, 0, -2}), 10},
                 {{.sprite = f.first, .transparency = SpriteTransparency::FollowView},
                  Mat4::translation({0, 0, -1}),
                  20},
                 {{.sprite = f.first}, Mat4::identity(), 30}};
             SpriteBatch batch;
             batch.replace(f.assets, data);
             RenderView view{.cameraNode = f.camera,
                             .transparency = TransparencyMode::WeightedBlendedOIT};
             SceneQuery all(f.extractor.extract(f.scene, f.assets, std::span{&batch, 1}), view, 200,
                            200);
             auto hit = all.pickSprite({100, 90});
             require(hit && !hit->node && hit->instanceId == 30);
             data.pop_back();
             batch.replace(f.assets, data);
             SceneQuery oit(f.extractor.extract(f.scene, f.assets, std::span{&batch, 1}), view, 200,
                            200);
             require(oit.pickSprite({100, 90})->instanceId == 20);
         }},
        {"interaction filters never modify render data",
         [] {
             Fixture f;
             const auto back = f.add(f.first, {0, 0, -1});
             const auto front = f.add(f.second);
             auto c = *f.scene.component<SpriteRenderer>(front);
             c.layerMask = 2;
             f.scene.setComponent(front, c);
             c = *f.scene.component<SpriteRenderer>(back);
             c.layerMask = 1;
             f.scene.setComponent(back, c);
             const auto snapshot = f.extractor.extract(f.scene, f.assets);
             RenderView view{.cameraNode = f.camera};
             SceneQueryFilter filter;
             filter.layerMask = 1;
             SceneQuery query(snapshot, view, 200, 200, filter);
             const auto hit = query.pickSprite({100, 90});
             require(hit && hit->node == back);
             filter.excludedNodes.push_back(back);
             SceneQuery excluded(snapshot, view, 200, 200, filter);
             require(!excluded.pickSprite({100, 90}));
             require(snapshot.spriteView(view, 200, 200).sprites().size() == 2 &&
                     f.frame().stats.visibleInstances == 2);
         }},
        {"alpha picking agrees with sorting and flipped UV",
         [] {
             Fixture f;
             auto texture = f.assets.createTexture(
                 {2,
                  1,
                  {0, 0, 0, 0, 255, 255, 255, 255},
                  false,
                  {.magFilter = TextureFilter::Nearest, .mipmapped = false}});
             auto front = f.assets.createSprite({.texture = texture,
                                                 .rect = {0, 0, 2, 1},
                                                 .pivot = {.5f, .5f},
                                                 .pixelsPerUnit = 1});
             auto back = f.assets.createSprite({.texture = texture,
                                                .rect = {1, 0, 1, 1},
                                                .pivot = {.5f, .5f},
                                                .pixelsPerUnit = 1});
             const auto bn = f.add(back, {0, 0, -.2f});
             auto bc = *f.scene.component<SpriteRenderer>(bn);
             bc.sizeMultiplier.x = 2;
             f.scene.setComponent(bn, bc);
             const auto fn = f.add(front);
             Camera camera;
             camera.projection = Camera::Projection::Orthographic;
             camera.orthographicHeight = 2;
             f.scene.setComponent(f.camera, camera);
             const RenderView view{.cameraNode = f.camera};
             SceneQuery q(f.extractor.extract(f.scene, f.assets), view, 200, 200);
             require(q.pickSprite({50, 100})->node == bn);
             require(q.pickSprite({150, 100})->node == fn);
             auto fc = *f.scene.component<SpriteRenderer>(fn);
             fc.flipX = true;
             f.scene.setComponent(fn, fc);
             SceneQuery flipped(f.extractor.extract(f.scene, f.assets), view, 200, 200);
             require(flipped.pickSprite({50, 100})->node == fn &&
                     flipped.pickSprite({150, 100})->node == bn);
             require(!q.pickSprite({-1, 100}));
             throws([&] { q.pickSprite({100, 100}, 2); });
         }},
        {"perspective ground rays and mask coverage",
         [] {
             Fixture f;
             f.scene.setComponent(f.camera, Camera{});
             f.scene.setLocalTransform(f.camera, *Mat4::lookAt({0, 5, 8}, {0, 0, 0}).inverse());
             SceneQuery perspective(f.extractor.extract(f.scene, f.assets),
                                    {.cameraNode = f.camera}, 200, 100);
             const auto screen = perspective.worldToScreen({1, 0, -1});
             require(bool(screen));
             const auto point = perspective.groundPoint({screen->x, screen->y});
             require(bool(point));
             near(point->x, 1, .001f);
             near(point->z, -1, .001f);
             require(!perspective.worldToScreen({0, 5, 20}));
             f.scene.setComponent(f.camera, Camera{.projection = Camera::Projection::Orthographic,
                                                   .orthographicHeight = 4});
             f.scene.setLocalTransform(f.camera, Mat4::translation({0, 0, 10}));
             auto mask = f.assets.spriteDescription(f.first);
             mask.alphaMode = AlphaMode::Mask;
             const auto back = f.add(f.second, {0, 0, -1});
             const auto front = f.add(f.assets.createSprite(mask));
             SceneQuery full(f.extractor.extract(f.scene, f.assets), {.cameraNode = f.camera}, 200,
                             200);
             require(full.pickSprite({100, 80})->node == front);
             auto c = *f.scene.component<SpriteRenderer>(front);
             c.tint.w = .4f;
             f.scene.setComponent(front, c);
             SceneQuery cut(f.extractor.extract(f.scene, f.assets), {.cameraNode = f.camera}, 200,
                            200);
             require(cut.pickSprite({100, 80})->node == back);
             require(!cut.groundPoint({100, 80}));
         }},
        {"screen ground projection and snapshot lifetime",
         [] {
             RenderSnapshot snapshot;
             NodeHandle camera;
             {
                 Fixture f;
                 f.add();
                 camera = f.camera;
                 f.scene.setLocalTransform(camera, *Mat4::lookAt({0, 8, 8}, {0, 0, 0}).inverse());
                 snapshot = f.extractor.extract(f.scene, f.assets);
                 f.assets.destroySprite(f.first);
                 f.assets.destroyTexture(f.texture);
                 throws([&] { f.extractor.extract(f.scene, f.assets); });
             }
             SceneQuery q(snapshot, {.cameraNode = camera}, 200, 100);
             const auto screen = q.worldToScreen({1, 0, -1});
             require(bool(screen));
             const auto ground = q.groundPoint({screen->x, screen->y});
             require(bool(ground));
             near(ground->x, 1);
             near(ground->y, 0);
             near(ground->z, -1);
             auto p =
                 detail::prepareFrame(snapshot, {.cameraNode = camera, .shadows = false}, 200, 100);
             require(p.stats.visibleInstances == 1 && snapshot.spriteCount() == 1);
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
    std::cout << passed << " interaction CPU tests passed\n";
}
