/** @file @brief 验证场景缓存身份/失效、BVH 与逐项扫描等价、跨场景隔离和 Alpha8 不可变更新。 */
#include "internal/frame.hpp"
#include "internal/snapshot.hpp"
#include "internal/spatial.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <renderer/texture.hpp>
using namespace renderer;
namespace {
void need(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void throws(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    need(failed, "Expected failure");
}
} // namespace
int main() {
    try {
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        auto camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                          .orthographicHeight = 12});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 10}));
        TextureDesc image{8, 8, std::vector<uint8_t>(256, 255), false};
        auto tex = assets.createTexture(image);
        auto sprite =
            assets.createSprite({.texture = tex, .rect = {0, 0, 8, 8}, .pixelsPerUnit = 8});
        std::vector<NodeHandle> nodes;
        for (unsigned i = 0; i < 1024; ++i) {
            auto n = scene.createNode(scene.root());
            scene.setLocalTransform(n,
                                    Mat4::translation({float(i % 32) - 16, float(i / 32) - 16, 0}) *
                                        Mat4::scaling({i % 3 == 0 ? -1.f : 1.f, 1, 1}));
            scene.setComponent(n, SpriteRenderer{.sprite = sprite,
                                                 .billboard = i % 3 == 0 ? BillboardMode::Upright
                                                                         : BillboardMode::Fixed});
            nodes.push_back(n);
        }
        auto before = extractor.extract(scene, assets), same = extractor.extract(scene, assets);
        need(&detail::SnapshotAccess::get(before).scene() ==
                 &detail::SnapshotAccess::get(same).scene(),
             "Unchanged scene was rebuilt");
        SpriteBatch batch;
        std::array<BatchSprite, 1> item{{{{.sprite = sprite}, Mat4::identity(), 99}}};
        batch.replace(assets, item);
        auto extra = extractor.extract(scene, assets, std::span(&batch, 1));
        need(extra.spriteCount() == 1025 && before.spriteCount() == 1024,
             "Batch changed old snapshot or polluted scene root");
        need(&detail::SnapshotAccess::get(extra).scene() ==
                 &detail::SnapshotAccess::get(before).scene(),
             "New batch rebuilt unchanged scene nodes");
        for (bool shadows : {false, true})
            for (auto mode :
                 {TransparencyMode::SortedAlpha, TransparencyMode::WeightedBlendedOIT}) {
                RenderView view;
                view.cameraNode = camera;
                view.shadows = shadows;
                view.transparency = mode;
                auto indexed = detail::prepareFrame(extra, view, 640, 480);
                view.spatialCulling = false;
                auto linear = detail::prepareFrame(extra, view, 640, 480);
                need(indexed.stats.visibleInstances == linear.stats.visibleInstances &&
                         indexed.stats.culledInstances == linear.stats.culledInstances &&
                         indexed.instances.size() == linear.instances.size(),
                     "BVH changed visibility or shadow caster count");
                need(std::memcmp(indexed.instances.data(), linear.instances.data(),
                                 indexed.instances.size() * sizeof(detail::InstanceGpu)) == 0,
                     "BVH changed ordered instance data");
            }
        RenderView view;
        view.cameraNode = camera;
        auto indexed = extra.spriteView(view, 640, 480);
        view.spatialCulling = false;
        auto linear = extra.spriteView(view, 640, 480);
        need(indexed.sprites().size() == linear.sprites().size(), "View BVH changed exact culling");
        auto ids = indexed.rayCandidates({0, 0, 10}, {0, 0, -1});
        need(std::is_sorted(ids.begin(), ids.end()) && ids.size() < indexed.sprites().size() / 2,
             "Ray index did not narrow or reordered candidates");
        scene.setLocalTransform(nodes[0], Mat4::translation({1, 2, 0}));
        auto changed = extractor.extract(scene, assets);
        need(before.instanceTransform(nodes[0])->transformPoint({}).x == -16 &&
                 changed.instanceTransform(nodes[0])->transformPoint({}).x == 1,
             "Mutation changed old cached snapshot");
        RenderView movedView;
        movedView.cameraNode = camera;
        movedView.shadows = false;
        auto fastMoved = detail::prepareFrame(changed, movedView, 640, 480);
        movedView.spatialCulling = false;
        auto slowMoved = detail::prepareFrame(changed, movedView, 640, 480);
        need(fastMoved.instances.size() == slowMoved.instances.size() &&
                 std::memcmp(fastMoved.instances.data(), slowMoved.instances.data(),
                             fastMoved.instances.size() * sizeof(detail::InstanceGpu)) == 0,
             "Refitted BVH missed moved geometry or modified old topology");
        extractor.clearCache();
        need(changed.spriteCount() == 1024, "Clearing cache destroyed an existing snapshot");
        Scene other;
        auto cam2 = other.createNode(other.root());
        other.setComponent(cam2, Camera{});
        auto isolated = extractor.extract(other, assets);
        need(isolated.spriteCount() == 0 && isolated.cameraCount() == 1,
             "Scene owner cache collision");
        extractor.extract(scene, assets);
        assets.destroySprite(sprite);
        throws([&] { extractor.extract(scene, assets); });
        TextureDesc alpha;
        alpha.width = 7;
        alpha.height = 9;
        alpha.srgb = false;
        alpha.sampling.mipmapped = false;
        alpha.channels = TextureChannels::Alpha8;
        alpha.pixels.resize(63, 0);
        auto base = assets.createTexture(alpha);
        std::array<uint8_t, 6> patch{10, 20, 30, 40, 50, 60};
        auto updated = assets.updateTexture(base, {2, 3, 3, 2}, patch);
        auto result = assets.textureDescription(updated);
        need(result.pixels.size() == 63 && assets.textureDescription(base).pixels[23] == 0 &&
                 result.pixels[23] == 10 && result.pixels[32] == 60,
             "Alpha8 copy-on-write region mismatch");
        auto raw = texture::decode(result);
        need(raw.pixels[23 * 4] == 255 && raw.pixels[23 * 4 + 3] == 10,
             "Alpha8 decode coverage mismatch");
        const auto file = std::filesystem::path(RENDERER_TEST_OUTPUT) / "alpha.dds";
        texture::saveDds(result, file);
        auto read = texture::loadDds(file);
        need(read.channels == TextureChannels::Alpha8 && texture::decode(read).pixels == raw.pixels,
             "DDS A8 row order/size mismatch");
        throws([&] { assets.updateTexture(base, {6, 8, 2, 1}, patch); });
        MeshDesc triangle;
        triangle.vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 0}}};
        triangle.indices = {0, 1, 2};
        auto mesh = assets.createMesh(triangle);
        MaterialDesc material;
        material.effect.boundsPadding = 3e38f;
        auto mat = assets.createMaterial(material);
        Scene huge;
        auto object = huge.createNode(huge.root());
        huge.setLocalTransform(object, Mat4::translation({3e38f, 0, 0}));
        huge.setComponent(object, MeshRenderer{mesh, {mat}});
        throws([&] { extractor.extract(huge, assets); });
        extractor.setCachingEnabled(false);
        std::cout << "Scene reuse, BVH equivalence and immutable Alpha8 passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
