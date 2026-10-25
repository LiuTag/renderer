/** @file @brief 无窗口验证真实 RmlUi 布局、基本控件、DPI、输入捕获、事件和资源快照生命周期。 */
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <renderer/renderer.hpp>
#include <renderer/ui/ui.hpp>
#include <stdexcept>
#include <thread>
using namespace renderer;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void throws(F action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected UI failure");
}
const char *rml = R"(<rml><head><style>
body {font-family:LatoLatin;font-size:16px;width:100%;height:100%;pointer-events:none;}
#panel {position:absolute;left:10px;top:10px;width:240px;height:270px;background-color:#203040;pointer-events:auto;}
input,button,progress {display:block;width:160px;height:26px;margin-bottom:5px;background-color:#407080;color:white;}
input {tab-index:auto;}
#label {height:24px;} #clip {height:20px;overflow:hidden;}
</style></head><body><div id="panel"><div id="label">Initial</div><button id="button">Action</button><input id="text" type="text" value="hello"/><input id="check" type="checkbox"/><input id="range" type="range" min="0" max="100" value="25"/><progress id="progress" value="0.5"/><div id="clip"><div style="height:80px;background-color:red;">Clipped</div></div><img id="icon" src="ui:icon" width="8" height="8"/></div></body></rml>)";
void click(UiContext &ui, const UiRect &r, double scale = 1) {
    const double x = (r.x + r.width * .5) / scale, y = (r.y + r.height * .5) / scale;
    ui.processEvent(MouseMoveEvent{x, y});
    require(ui.processEvent(
                MouseButtonEvent{MouseButton::Left, InputAction::Press, Modifiers::None, x, y}),
            "UI did not capture press");
    ui.processEvent(
        MouseButtonEvent{MouseButton::Left, InputAction::Release, Modifiers::None, x, y});
    ui.update(0);
}
} // namespace
int main() {
    try {
        const auto directory = std::filesystem::path(RENDERER_TEST_UI_DATA) / u8"中文路径";
        std::filesystem::create_directories(directory);
        const auto font = directory / u8"字体.ttf";
        std::filesystem::copy_file(RENDERER_TEST_UI_FONT, font,
                                   std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream css(directory / u8"皮肤.rcss", std::ios::binary);
            css << "body{font-family:LatoLatin;font-size:16px;}img{width:20px;height:20px;}";
        }
        {
            std::ofstream file(directory / u8"界面.rml", std::ios::binary);
            file << "<rml><head><link type=\"text/rcss\" href=\"皮肤.rcss\"/></head><body><img "
                    "src=\"图片.png\"/></body></rml>";
        }
        Image{1, 1, {255, 128, 0, 255}}.savePng(directory / u8"图片.png");
        UiContext ui({400, 320});
        ui.loadFont(font);
        ui.registerImage("icon", {1, 1, {255, 0, 0, 128}, true});
        auto doc = ui.loadDocumentFromMemory(rml);
        ui.update(0);
        require(ui.element(doc, "button").bounds.width > 100, "RmlUi layout did not run");
        int count = 0;
        auto sub = ui.subscribe(doc, "button", "click", [&](const UiEvent &e) {
            require(e.document == doc && e.elementId == "button", "UI event mismatch");
            ++count;
        });
        click(ui, ui.element(doc, "button").bounds);
        require(count == 1, "Click callback missing");
        ui.setEnabled(doc, "button", false);
        ui.update(0);
        click(ui, ui.element(doc, "button").bounds);
        require(count == 1 && !ui.element(doc, "button").enabled, "Disabled button accepted input");
        ui.setEnabled(doc, "button", true);
        ui.update(0);
        sub.cancel();
        click(ui, ui.element(doc, "button").bounds);
        require(count == 1, "Canceled callback ran");
        int keyboardClicks = 0;
        auto keyboard =
            ui.subscribe(doc, "button", "click", [&](const auto &) { ++keyboardClicks; });
        require(ui.focus(doc, "button"), "Button focus failed");
        ui.processEvent(KeyEvent{Key::Enter});
        ui.processEvent(KeyEvent{.key = Key::Enter, .action = InputAction::Release});
        ui.update(0);
        require(keyboardClicks == 1, "Keyboard button activation failed");
        ui.processEvent(KeyEvent{Key::Space});
        ui.processEvent(WindowFocusEvent{false});
        ui.update(0);
        require(keyboardClicks == 1, "Focus loss activated held keyboard button");
        const auto buttonBounds = ui.element(doc, "button").bounds;
        ui.processEvent(MouseButtonEvent{MouseButton::Left, InputAction::Press, Modifiers::None,
                                         buttonBounds.x + 2, buttonBounds.y + 2});
        ui.processEvent(WindowFocusEvent{false});
        ui.update(0);
        require(keyboardClicks == 1, "Focus loss activated held mouse button");
        keyboard.cancel();
        auto throwing = ui.subscribe(doc, "button", "click", [](const auto &) {
            throw std::runtime_error("expected UI callback test");
        });
        auto later = ui.subscribe(doc, "button", "click", [&](const auto &) { ++count; });
        click(ui, ui.element(doc, "button").bounds);
        require(count == 2, "Callback failure blocked later callbacks");
        auto messages = ui.takeMessages();
        require(!messages.empty(), "Callback exception was not recorded");
        throwing.cancel();
        later.cancel();
        ui.setText(doc, "label", "<literal>&");
        require(ui.element(doc, "label").markup.find("<literal>") == std::string::npos,
                "setText did not escape markup");
        ui.setValue(doc, "text", "demo");
        ui.focus(doc, "text");
        require(ui.capture().keyboard, "Text field did not capture keyboard");
        ui.processEvent(KeyEvent{Key::End});
        ui.processEvent(TextInputEvent{U'!'});
        require(ui.element(doc, "text").value == "demo!", "Committed text input failed");
        ui.setChecked(doc, "check", true);
        require(ui.element(doc, "check").checked, "Checkbox setter failed");
        ui.setEnabled(doc, "text", false);
        require(!ui.element(doc, "text").enabled, "Disabled state missing");
        ui.setValue(doc, "range", "75");
        require(std::abs(std::stof(ui.element(doc, "range").value) - 75) < .01f,
                "Slider setter failed");
        ui.setValue(doc, "progress", "0.8");
        require(ui.element(doc, "progress").value == "0.8", "Progress setter failed");
        ui.processEvent(WindowFocusEvent{false});
        require(!ui.capture().keyboard, "Focus loss retained keyboard capture");
        ui.setViewport({800, 640, 400, 320, 2});
        ui.update(0);
        auto bounds = ui.element(doc, "button").bounds;
        int dpiClicks = 0;
        auto dpi = ui.subscribe(doc, "button", "click", [&](const auto &) { ++dpiClicks; });
        click(ui, bounds, 2);
        require(dpiClicks == 1, "DPI pointer mapping failed");
        const double x = (bounds.x + 2) / 2, y = (bounds.y + 2) / 2;
        ui.processEvent(
            MouseButtonEvent{MouseButton::Left, InputAction::Press, Modifiers::None, x, y});
        require(ui.processEvent(MouseMoveEvent{399, 319}), "Mouse drag lost capture outside UI");
        ui.processEvent(
            MouseButtonEvent{MouseButton::Left, InputAction::Release, Modifiers::None, 399, 319});
        require(!ui.capture().mouse, "Mouse capture was not released");
        auto snapshot = ui.snapshot();
        require(!snapshot.draws().empty() && snapshot.vertices().size() > 30,
                "UI geometry/text missing");
        require(std::any_of(snapshot.draws().begin(), snapshot.draws().end(),
                            [](const auto &d) { return bool(d.texture); }),
                "Font/image texture missing");
        ui.showDocument(doc, false);
        require(ui.snapshot().draws().empty(), "Hidden document still rendered");
        ui.showDocument(doc);
        ui.update(0);
        UiContext other;
        auto unicode = other.loadDocument(directory / u8"界面.rml");
        other.update(0);
        require(!other.snapshot().draws().empty(), "Unicode document/style/image loading failed");
        other.unloadDocument(unicode);
        throws([&] { other.element(doc, "label"); });
        auto doc2 = other.loadDocumentFromMemory(rml);
        other.unloadDocument(doc2);
        throws([&] { ui.update(-1); });
        throws([&] { ui.update(std::numeric_limits<double>::quiet_NaN()); });
        throws([&] { ui.setViewport({0, 10}); });
        throws([&] { ui.element(doc, "missing"); });
        throws([&] { ui.registerImage("icon", {1, 1, {255, 255, 255, 255}, true}); });
        throws([&] { ui.processEvent(TextInputEvent{0xd800}); });
        bool crossThread = false;
        std::thread worker([&] {
            try {
                ui.update(0);
            } catch (const std::logic_error &) {
                crossThread = true;
            }
        });
        worker.join();
        require(crossThread, "UI thread ownership missing");
        // 应用只使用订阅、不读取轮询队列时，队列满不能让控件失去响应。
        ui.takeEvents();
        for (unsigned i = 0; i < 4200; ++i) {
            ui.setValue(doc, "range", std::to_string(i % 100));
            ui.update(0);
        }
        int afterOverflow = 0;
        auto overflow =
            ui.subscribe(doc, "button", "click", [&](const auto &) { ++afterOverflow; });
        click(ui, ui.element(doc, "button").bounds, 2);
        require(afterOverflow == 1, "Full polling queue blocked subscribed UI events");
        ui.unloadDocument(doc);
        require(ui.snapshot().draws().empty() && !snapshot.draws().empty(),
                "UI snapshot depended on live document");
        throws([&] { ui.element(doc, "label"); });
        auto events = ui.takeEvents();
        require(events.size() == 4096, "UI polling queue capacity was not enforced");
        UiContext moved = std::move(ui);
        throws([&] { ui.snapshot(); });
        moved.update(0);
        std::cout << "RmlUi controls, events, DPI and snapshot lifetime passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
