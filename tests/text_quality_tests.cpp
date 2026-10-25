/** @file @brief 验证 SDF 跨字号复用、布局、LRU 页面/字形淘汰、保护集及旧快照资源保留。 */
#include <cmath>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
using namespace renderer;
namespace {
void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void throws(F f) {
    bool b = false;
    try {
        f();
    } catch (const std::exception &) {
        b = true;
    }
    require(b, "Expected cache validation failure");
}
} // namespace
int main() {
    try {
        TextSystem sdf;
        auto font = sdf.loadFont(RENDERER_TEST_FONT);
        TextStyle style;
        style.font = font;
        style.fontSize = 24;
        style.rasterMode = TextRasterMode::Sdf;
        auto small = sdf.measure(style, "Hello SDF");
        auto before = sdf.statistics();
        style.fontSize = 72;
        auto large = sdf.measure(style, "Hello SDF");
        auto after = sdf.statistics();
        require(before.cachedGlyphs == after.cachedGlyphs && after.cacheHits > before.cacheHits,
                "SDF cache duplicated a second display size");
        require(std::abs(large.size.x - small.size.x * 3) < .01f,
                "Canonical SDF metrics do not scale with display size");
        style.fontSize = 24;
        style.outlineColor = {1, 0, 0, 1};
        style.outlineWidth = 16;
        throws([&] { sdf.measure(style, "A"); });
        TextSystemConfig cfg;
        cfg.atlasSize = 64;
        cfg.maxAtlasPages = 1;
        cfg.maxCachedGlyphs = 256;
        TextSystem lru(cfg);
        auto f = lru.loadFont(RENDERER_TEST_FONT);
        TextStyle bitmap;
        bitmap.font = f;
        bitmap.fontSize = 20;
        WorldTextDesc desc;
        desc.style = bitmap;
        desc.text = "A";
        auto id = lru.create(desc);
        Scene scene;
        auto camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
        auto batch = lru.batch(scene, camera, 640, 480);
        AssetStore assets;
        RenderExtractor extractor;
        auto snapshot = extractor.extract(scene, assets, std::span(&batch, 1));
        for (char c = 'B'; c <= 'Z'; ++c)
            lru.prewarm(bitmap, std::string(1, c));
        require(lru.statistics().evictedPages > 0 && lru.statistics().atlasPages == 1,
                "Page LRU did not enforce atlas capacity");
        require(snapshot.spriteCount() == 1 &&
                    !snapshot.spriteView({.cameraNode = camera}, 640, 480).sprites().empty(),
                "LRU invalidated an old published batch");
        require(lru.batch(scene, camera, 640, 480).size() == 1,
                "Evicted label layout was not rebuilt");
        require(lru.trimCache(0) == 0, "Visible label page was not protected");
        lru.setVisible(id, false);
        require(lru.trimCache(0) == 1 && !lru.statistics().atlasPages, "Unused page not reclaimed");
        cfg.atlasSize = 256;
        cfg.maxCachedGlyphs = 2;
        TextSystem glyphs(cfg);
        bitmap.font = glyphs.loadFont(RENDERER_TEST_FONT);
        glyphs.measure(bitmap, "AB");
        glyphs.measure(bitmap, "CD");
        require(glyphs.statistics().evictedGlyphs >= 2 && glyphs.statistics().cachedGlyphs == 2,
                "Glyph LRU not bounded");
        throws([&] { glyphs.measure(bitmap, "ABC"); });
        cfg.automaticEviction = false;
        TextSystem strict(cfg);
        bitmap.font = strict.loadFont(RENDERER_TEST_FONT);
        strict.measure(bitmap, "AB");
        throws([&] { strict.measure(bitmap, "C"); });
        std::cout << "SDF metrics, canonical cache and LRU passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
