/** @file @brief 无 GPU 检查字体、排版、字形缓存、节点绑定、Billboard、屏幕字号和快照资源存活。 */
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
#include <stdexcept>
#include <thread>
using namespace renderer;
namespace {
void require(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
void near(float a, float b, float e = 1e-3f) {
    require(std::abs(a - b) < e, "Text numeric mismatch");
}
template <class F> void throws(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected text validation failure");
}
std::array<float, 4> screenBounds(const SpriteViewSnapshot &view) {
    std::array<float, 4> b{1e9f, 1e9f, -1e9f, -1e9f};
    const auto [w, h] = view.viewportSize();
    for (const auto &s : view.sprites())
        for (float x : {-.5f, .5f})
            for (float y : {-.5f, .5f}) {
                const auto p = view.viewProjection() * (s.world * Vec4{x, y, 0, 1});
                const float px = (p.x / p.w * .5f + .5f) * w, py = (.5f - p.y / p.w * .5f) * h;
                b[0] = std::min(b[0], px);
                b[1] = std::min(b[1], py);
                b[2] = std::max(b[2], px);
                b[3] = std::max(b[3], py);
            }
    return b;
}
} // namespace
int main() {
    try {
        const auto directory = std::filesystem::path(RENDERER_TEST_TEXT_DATA) / u8"文字资源";
        std::filesystem::create_directories(directory);
        const auto path = directory / u8"字体.ttf";
        std::filesystem::copy_file(RENDERER_TEST_TEXT_FONT, path,
                                   std::filesystem::copy_options::overwrite_existing);
        TextSystem text;
        const auto font = text.loadFont(path);
        TextStyle style{.font = font, .fontSize = 32};
        auto a = text.measure(style, "A"), spaces = text.measure(style, "  "),
             lines = text.measure(style, "AB\r\nCD");
        require(a.glyphs == 1 && a.size.x > 10 && a.size.y > 0 && spaces.glyphs == 0 &&
                    spaces.size.x > 0,
                "Glyph metrics missing");
        require(lines.lines == 2 && lines.glyphs == 4 && text.measure(style, "").lines == 0,
                "Text newline/empty handling failed");
        near(lines.size.y, style.fontSize * style.lineSpacing * 2);
        require(text.measure(style, "A\tB").size.x > text.measure(style, "AB").size.x,
                "Tab stop failed");
        const auto av = text.measure(style, "AV");
        require(av.size.x <= text.measure(style, "A").size.x + text.measure(style, "V").size.x,
                "Kerning expanded Latin pair");
        auto wrap = style;
        wrap.maxWidth = a.size.x + 1;
        require(text.measure(wrap, "AAAA").lines >= 2, "Glyph wrapping failed");
        Scene scene;
        AssetStore assets;
        RenderExtractor extractor;
        auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
        WorldTextDesc desc;
        desc.style = style;
        desc.text = "AV";
        desc.anchor = {0, 0};
        desc.billboard = BillboardMode::Fixed;
        const auto id = text.create(desc);
        auto batch = text.batch(scene, camera, 400, 400);
        require(batch.size() == 2, "Text batch glyph count mismatch");
        auto snapshot = extractor.extract(scene, assets, std::span{&batch, 1});
        auto view = snapshot.spriteView({.cameraNode = camera}, 400, 400);
        require(view.sprites().size() == 2 && view.sprites()[0].instanceId == id,
                "Text batch identity missing");
        const auto originalPixels = view.sprites()[0].texture;
        require(std::any_of(originalPixels->pixels.begin(), originalPixels->pixels.end(),
                            [](uint8_t p) { return p > 0 && p < 255; }),
                "FreeType coverage AA missing");
        const auto cached = text.statistics().cachedGlyphs;
        text.batch(scene, camera, 400, 400);
        require(text.statistics().cachedGlyphs == cached, "Unchanged label rerasterized glyphs");
        text.setColor(id, {.3f, .5f, 1, 1});
        text.setTransform(id, Mat4::translation({1, 0, 0}));
        auto moved = text.batch(scene, camera, 400, 400);
        near(moved.bounds().minimum.x - batch.bounds().minimum.x, 1);
        require(text.statistics().cachedGlyphs == cached,
                "Color/transform invalidated glyph cache");
        text.setText(id, "AV xyz 0123456789");
        text.batch(scene, camera, 400, 400);
        require(view.sprites()[0].texture == originalPixels && snapshot.spriteCount() == 2,
                "Atlas growth invalidated old snapshot");
        auto parent = scene.createNode(scene.root(), "Parent");
        auto child = scene.createNode(parent, "Text anchor");
        scene.setLocalTransform(parent, Mat4::translation({2, 0, 0}));
        scene.setLocalTransform(child, Mat4::translation({0, 1, 0}));
        desc.node = child;
        desc.text = "A";
        desc.transform = Mat4::translation({0, 1, 0});
        text.setDescription(id, desc);
        auto bound = text.batch(scene, camera, 400, 400);
        scene.setLocalTransform(parent, Mat4::translation({3, 0, 0}));
        auto bound2 = text.batch(scene, camera, 400, 400);
        near(bound2.bounds().minimum.x - bound.bounds().minimum.x, 1);
        scene.setVisible(parent, false);
        require(text.batch(scene, camera, 400, 400).size() == 0,
                "Hidden ancestor text was rendered");
        scene.setVisible(parent, true);
        scene.destroySubtree(parent);
        require(text.batch(scene, camera, 400, 400).size() == 0, "Destroyed anchor was rendered");
        desc.node = {};
        desc.transform = Mat4::identity();
        desc.text = "Screen";
        desc.billboard = BillboardMode::ViewPlane;
        desc.sizeMode = TextSizeMode::ScreenPixels;
        desc.anchor = {.5f, .5f};
        text.setDescription(id, desc);
        const auto sample = [&] {
            auto b = text.batch(scene, camera, 400, 400);
            return extractor.extract(scene, assets, std::span{&b, 1})
                .spriteView({.cameraNode = camera}, 400, 400);
        };
        auto nearBounds = screenBounds(sample());
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 10}));
        auto farBounds = screenBounds(sample());
        near(nearBounds[2] - nearBounds[0], farBounds[2] - farBounds[0]);
        near(nearBounds[3] - nearBounds[1], farBounds[3] - farBounds[1]);
        auto component = *scene.component<Camera>(camera);
        component.projection = Camera::Projection::Orthographic;
        component.orthographicHeight = 12;
        scene.setComponent(camera, component);
        auto ortho = screenBounds(sample());
        near(ortho[3] - ortho[1], farBounds[3] - farBounds[1]);
        desc.sizeMode = TextSizeMode::WorldUnits;
        desc.billboard = BillboardMode::Upright;
        text.setDescription(id, desc);
        scene.setLocalTransform(camera, *Mat4::lookAt({4, 5, 8}, {0, 0, 0}).inverse());
        auto upright = sample();
        require(!upright.sprites().empty(), "Upright text missing");
        near(upright.sprites()[0].world(0, 1), 0);
        near(upright.sprites()[0].world(2, 1), 0);
        auto invisible = desc;
        invisible.layerMask = 0;
        text.setDescription(id, invisible);
        require(text.batch(scene, camera, 400, 400).size() == 0, "Text layer mask ignored");
        text.setDescription(id, desc);
        auto centered = style;
        centered.maxWidth = 200;
        centered.alignment = TextAlignment::Center;
        auto left = style;
        left.maxWidth = 200;
        require(text.measure(centered, "AA").inkMinimum.x >
                    text.measure(left, "AA").inkMinimum.x + 30,
                "Text alignment failed");
        throws([&] { text.setText(id, std::string("\xc0\x80", 2)); });
        require(text.description(id).text == desc.text, "Invalid UTF-8 changed text");
        throws([&] { text.measure(style, std::string("\xed\xa0\x80", 3)); });
        throws([&] { text.measure(style, std::string("\xf4\x90\x80\x80", 4)); });
        throws([&] { text.setColor(id, {0, 0, 0, -1}); });
        throws([&] { text.setTransform(id, Mat4::scaling({0, 1, 1})); });
        throws([&] { text.batch(scene, camera, 0, 400); });
        auto bad = desc;
        bad.billboard = BillboardMode::Fixed;
        bad.sizeMode = TextSizeMode::ScreenPixels;
        throws([&] { text.create(bad); });
        TextSystem other;
        throws([&] { other.description(id); });
        throws([&] { other.measure(style, "A"); });
        TextSystem tiny(
            {.atlasSize = 64, .maxAtlasPages = 1, .maxCachedGlyphs = 64, .maxLabels = 1});
        auto tinyFont = tiny.loadFont(path);
        auto tinyDesc = desc;
        tinyDesc.style.font = tinyFont;
        tinyDesc.text = "A";
        auto tinyId = tiny.create(tinyDesc);
        throws([&] { tiny.create(tinyDesc); });
        auto saved = tiny.batch(scene, camera, 400, 400);
        throws([&] {
            tiny.measure({.font = tinyFont, .fontSize = 48}, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        });
        require(saved.size() == 1, "Cache failure damaged old batch");
        tiny.destroy(tinyId);
        throws([&] { tiny.destroy(tinyId); });
        const auto chinese = std::filesystem::path("C:/Windows/Fonts/msyh.ttc");
        if (std::filesystem::exists(chinese)) {
            const auto before = text.measure(style, "中文");
            const auto fallback = text.loadFont(chinese);
            text.setFallbackFonts(std::span{&fallback, 1});
            auto after = text.measure(style, "中文");
            require(after.size.x > before.size.x, "Chinese fallback font was not selected");
        }
        bool threadRejected = false;
        std::thread worker([&] {
            try {
                text.statistics();
            } catch (const std::logic_error &) {
                threadRejected = true;
            }
        });
        worker.join();
        require(threadRejected, "Text thread ownership missing");
        SpriteBatch kept;
        {
            TextSystem shortLife;
            auto f = shortLife.loadFont(path);
            WorldTextDesc d;
            d.style.font = f;
            d.text = "Kept";
            shortLife.create(d);
            kept = shortLife.batch(scene, camera, 400, 400);
        }
        require(extractor.extract(scene, assets, std::span{&kept, 1}).spriteCount() == 4,
                "Text batch depended on live font/system");
        text.clear();
        require(text.statistics().labels == 0, "Text clear failed");
        TextSystem move = std::move(text);
        throws([&] { text.statistics(); });
        move.statistics();
        std::cout << "Text layout, atlas, world binding, screen size and lifetime passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
