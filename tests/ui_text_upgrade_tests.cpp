/** @file @brief 无窗口验证中文组合编辑/取消、剪贴板路由、调试面板和字形缓存回收。 */
#include "internal/overlay.hpp"
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
#include <renderer/texture.hpp>
#include <renderer/ui/ui.hpp>
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
    require(b, "Expected rejection");
}
} // namespace
int main() {
    try {
        UiContext ui({640, 480, 320, 240});
        TextureDesc red{4, 4, std::vector<uint8_t>(64, 0), false}, blue = red;
        for (size_t i = 0; i < 64; i += 4) {
            red.pixels[i] = red.pixels[i + 3] = 255;
            blue.pixels[i + 2] = blue.pixels[i + 3] = 255;
        }
        std::array<TextureDesc, 2> images{red, blue};
        ui.registerImage("dds-layer",
                         texture::compress(texture::makeArray(images), TextureCompression::BC3), 1);
        auto imageDocument =
            ui.loadDocumentFromMemory("<rml><head><style>img{width:20px;height:20px;}</style></"
                                      "head><body><img src=\"ui:dds-layer\"/></body></rml>");
        auto imageOverlay = ui.snapshot();
        bool foundBlue = false;
        for (size_t i = 0; i < imageOverlay.draws().size(); ++i) {
            auto t = detail::OverlayAccess::texture(imageOverlay, i);
            if (t && t->description.pixels.size() >= 4 && t->description.pixels[2] > 240 &&
                t->description.pixels[0] < 5)
                foundBlue = true;
        }
        require(foundBlue, "UI compressed array selected the wrong layer");
        ui.unloadDocument(imageDocument);
        ui.loadFont(RENDERER_TEST_FONT);
        auto document = ui.loadDocumentFromMemory(
            R"(<rml><head><style>body{font-family:LatoLatin;font-size:18px;}input{width:200px;height:30px;tab-index:auto;}</style></head><body><input id="edit" type="text" value=""/></body></rml>)");
        ui.update(0);
        require(ui.focus(document, "edit"), "Cannot focus input");
        require(ui.textInputArea().active && ui.textInputArea().width < 200,
                "DPI text area conversion missing");
        ui.processEvent(TextCompositionEvent{CompositionPhase::Begin});
        ui.processEvent(TextCompositionEvent{CompositionPhase::Update, "zhong", 2});
        require(ui.element(document, "edit").value == "zhong",
                "Preedit did not replace composition text");
        ui.processEvent(TextCompositionEvent{CompositionPhase::End});
        require(ui.element(document, "edit").value.empty(), "Cancelled preedit left ghost text");
        ui.processEvent(TextCompositionEvent{CompositionPhase::Begin});
        ui.processEvent(TextCompositionEvent{CompositionPhase::Update, "zhong", 5});
        ui.processEvent(TextCompositionEvent{CompositionPhase::Commit, "中文", 2});
        ui.processEvent(TextCompositionEvent{CompositionPhase::End});
        require(ui.element(document, "edit").value == "中文",
                "IME commit duplicated or lost Chinese text");
        std::string clipboard;
        ui.setPlatformServices({[&](const auto &s) { clipboard = s; }, [&] { return clipboard; }});
        const auto key = [&](Key k) {
            ui.processEvent(KeyEvent{k, -1, InputAction::Press, Modifiers::Control});
            ui.processEvent(KeyEvent{k, -1, InputAction::Release, Modifiers::Control});
        };
        key(Key::A);
        key(Key::C);
        require(clipboard == "中文", "System clipboard setter was not routed");
        ui.setValue(document, "edit", "");
        key(Key::V);
        require(ui.element(document, "edit").value == "中文",
                "System clipboard getter was not routed");
        ui.processEvent(TextCompositionEvent{CompositionPhase::Begin});
        ui.processEvent(TextCompositionEvent{CompositionPhase::Update, "cancel", 6});
        ui.processEvent(WindowFocusEvent{false});
        require(!ui.textInputArea().active && ui.element(document, "edit").value == "中文",
                "Focus loss did not restore composition");
        ui.focus(document, "edit");
        key(Key::A);
        ui.processEvent(TextCompositionEvent{CompositionPhase::Begin});
        ui.processEvent(TextCompositionEvent{CompositionPhase::Update, "pending", 3});
        auto panel =
            ui.setDebugPanel({.uploadedBytes = 1234}, {.meshBytes = 4096}, {.textureBytes = 8192});
        require(ui.element(panel, "metrics").markup.find("1234") != std::string::npos,
                "Debug panel lost metrics");
        ui.setDebugPanel({}, {}, {});
        require(ui.textInputArea().active, "Debug panel stole editable focus");
        ui.processEvent(TextCompositionEvent{CompositionPhase::End});
        require(ui.element(document, "edit").value == "中文",
                "Selection was not restored after cancelling preedit");
        throws([&] {
            ui.processEvent(
                TextCompositionEvent{CompositionPhase::Update, std::string("\xc0\x80", 2)});
        });
        auto overlay = ui.snapshot();
        require(!overlay.draws().empty(), "Debug panel did not render");
        ui.releaseFontCache();
        auto afterRelease = ui.snapshot();
        require(!afterRelease.draws().empty() && !overlay.draws().empty(),
                "UI font cache release invalidated active text or old overlay");
        TextSystem text;
        auto font = text.loadFont(RENDERER_TEST_FONT);
        TextStyle style;
        style.font = font;
        text.prewarm(style, "Forest Camp 0123456789");
        auto warm = text.statistics();
        require(warm.cachedGlyphs > 10 && warm.fontFileBytes > 0,
                "Font prewarming/statistics missing");
        WorldTextDesc label;
        label.style = style;
        label.text = "Camp";
        text.create(label);
        Scene scene;
        auto camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
        auto old = text.batch(scene, camera, 640, 480);
        text.resetGlyphCache();
        require(text.statistics().cachedGlyphs == 0 && text.statistics().atlasPages == 0,
                "Glyph cache not reclaimed");
        auto renewed = text.batch(scene, camera, 640, 480);
        require(old.size() == 4 && renewed.size() == 4,
                "Cache reset invalidated labels or old batch");
        AssetStore assets;
        RenderExtractor extractor;
        auto retained = extractor.extract(scene, assets, std::span(&old, 1));
        require(retained.spriteCount() == 4, "Old atlas resources lost after reset");
        throws([&] { text.unloadFont(font); });
        text.clear();
        text.unloadFont(font);
        require(text.statistics().fonts == 0, "Unused font remained loaded");
        std::cout << "IME, clipboard, debug panel and font cache passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
