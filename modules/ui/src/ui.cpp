/** @file @brief 将 RmlUi 文档、输入和几何适配为公共引擎接口；不包含原生图形 API 或内部头文件。 */
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <renderer/renderer.hpp>
#include <renderer/texture.hpp>
#include <renderer/ui/ui.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace renderer {
namespace {
void require(bool ok, const char *text) {
    if (!ok)
        throw std::invalid_argument(text);
}
std::string utf8(const std::filesystem::path &p) {
    const auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
void validate(const UiViewport &v) {
    require(v.width && v.height && v.width <= 16384 && v.height <= 16384 &&
                std::isfinite(v.inputWidth) && std::isfinite(v.inputHeight) && v.inputWidth >= 0 &&
                v.inputHeight >= 0 && std::isfinite(v.density) && v.density > 0 && v.density <= 16,
            "Invalid UI viewport");
}
float linear(float c) {
    return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
}
Vec4 color(const Rml::ColourbPremultiplied &c) {
    const float a = c.alpha / 255.f;
    if (!a)
        return {};
    return {linear(std::min(1.f, c.red / 255.f / a)) * a,
            linear(std::min(1.f, c.green / 255.f / a)) * a,
            linear(std::min(1.f, c.blue / 255.f / a)) * a, a};
}
void convert(TextureDesc &d, bool premultiplied) {
    require(d.width && d.height && uint64_t(d.width) * d.height * 4 == d.pixels.size(),
            "Invalid UI image");
    for (size_t i = 0; i < d.pixels.size(); i += 4) {
        const float a = d.pixels[i + 3] / 255.f;
        for (size_t c = 0; c < 3; ++c) {
            float value = d.pixels[i + c] / 255.f;
            if (premultiplied)
                value = a > 0 ? std::min(1.f, value / a) : 0;
            if (d.srgb)
                value = linear(value);
            d.pixels[i + c] = uint8_t(std::lround(std::clamp(value * a, 0.f, 1.f) * 255));
        }
    }
    d.srgb = false;
    d.sampling.addressU = d.sampling.addressV = TextureAddress::Clamp;
    d.sampling.mipmapped = false;
}
std::string escaped(const std::string &s) {
    std::string out;
    for (char c : s)
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    return out;
}
struct Files : Rml::FileInterface {
    struct File {
        std::vector<char> data;
        size_t offset = 0;
    };
    Rml::FileHandle Open(const Rml::String &name) override {
        std::ifstream stream(path(name), std::ios::binary | std::ios::ate);
        if (!stream)
            return 0;
        auto size = stream.tellg();
        if (size < 0 || size > 64 * 1024 * 1024)
            return 0;
        auto f = std::make_unique<File>();
        f->data.resize(size_t(size));
        stream.seekg(0);
        if (size && !stream.read(f->data.data(), size))
            return 0;
        return reinterpret_cast<Rml::FileHandle>(f.release());
    }
    void Close(Rml::FileHandle h) override {
        delete reinterpret_cast<File *>(h);
    }
    size_t Read(void *out, size_t n, Rml::FileHandle h) override {
        auto &f = *reinterpret_cast<File *>(h);
        n = std::min(n, f.data.size() - f.offset);
        if (n)
            std::memcpy(out, f.data.data() + f.offset, n);
        f.offset += n;
        return n;
    }
    bool Seek(Rml::FileHandle h, long offset, int origin) override {
        auto &f = *reinterpret_cast<File *>(h);
        const int64_t base = origin == SEEK_SET   ? 0
                             : origin == SEEK_CUR ? int64_t(f.offset)
                             : origin == SEEK_END ? int64_t(f.data.size())
                                                  : -1;
        const int64_t next = base + offset;
        if (base < 0 || next < 0 || uint64_t(next) > f.data.size())
            return false;
        f.offset = size_t(next);
        return true;
    }
    size_t Tell(Rml::FileHandle h) override {
        return reinterpret_cast<File *>(h)->offset;
    }
};
// RmlUi 的通用 <button> 本身不是表单控件；补上禁用、焦点和键盘激活语义。
struct Button : Rml::ElementFormControl {
    explicit Button(const Rml::String &tag) : Rml::ElementFormControl(tag) {
        SetProperty("tab-index", "auto");
    }
    Rml::String GetValue() const override {
        return GetAttribute<Rml::String>("value", "");
    }
    void SetValue(const Rml::String &value) override {
        SetAttribute("value", value);
    }
    void ProcessDefaultAction(Rml::Event &event) override {
        Rml::ElementFormControl::ProcessDefaultAction(event);
        if (event.GetType() == "blur") {
            SetPseudoClass("active", false);
            return;
        }
        if (IsDisabled())
            return;
        const auto identifier = event.GetParameter<int>("key_identifier", 0);
        if (event.GetType() == "keydown" && identifier == Rml::Input::KI_RETURN) {
            DispatchEvent("click", {});
            event.StopPropagation();
        }
        if (event.GetType() == "keydown" && identifier == Rml::Input::KI_SPACE) {
            SetPseudoClass("active", true);
            event.StopPropagation();
        }
        if (event.GetType() == "keyup" && identifier == Rml::Input::KI_SPACE &&
            IsPseudoClassSet("active")) {
            SetPseudoClass("active", false);
            DispatchEvent("click", {});
            event.StopPropagation();
        }
    }
};
struct Runtime : Rml::SystemInterface {
    Files files;
    Rml::ElementInstancerGeneric<Button> buttons;
    std::thread::id thread = std::this_thread::get_id();
    double seconds = 0;
    std::vector<std::string> messages;
    std::string clipboard;
    UiPlatformServices *services = nullptr;
    Runtime() {
        Rml::SetSystemInterface(this);
        Rml::SetFileInterface(&files);
        if (!Rml::Initialise())
            throw std::runtime_error("RmlUi initialization failed");
        Rml::Factory::RegisterElementInstancer("button", &buttons);
    }
    ~Runtime() {
        Rml::Shutdown();
        Rml::SetSystemInterface(nullptr);
        Rml::SetFileInterface(nullptr);
    }
    double GetElapsedTime() override {
        return seconds;
    }
    bool LogMessage(Rml::Log::Type, const Rml::String &message) override {
        if (messages.size() < 4096)
            messages.push_back(message);
        return true;
    }
    void SetClipboardText(const Rml::String &text) override {
        clipboard = text;
        if (services && services->setClipboard)
            services->setClipboard(text);
    }
    void GetClipboardText(Rml::String &text) override {
        if (services && services->getClipboard)
            clipboard = services->getClipboard();
        text = clipboard;
    }
};
struct PlatformScope {
    Runtime &runtime;
    UiPlatformServices *previous;
    PlatformScope(Runtime &r, UiPlatformServices &s) : runtime(r), previous(r.services) {
        r.services = &s;
    }
    ~PlatformScope() {
        runtime.services = previous;
    }
};
void validateComposition(const TextCompositionEvent &event) {
    require(event.phase == CompositionPhase::Begin || event.phase == CompositionPhase::Update ||
                event.phase == CompositionPhase::Commit || event.phase == CompositionPhase::End,
            "Invalid composition phase");
    require(event.text.size() <= 65536, "IME text capacity exceeded");
    for (size_t i = 0; i < event.text.size();) {
        const auto first = uint8_t(event.text[i++]);
        uint32_t scalar = 0, minimum = 0;
        unsigned count = 0;
        if (first < 0x80)
            scalar = first;
        else if (first >= 0xc2 && first <= 0xdf) {
            scalar = first & 31;
            count = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            scalar = first & 15;
            count = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            scalar = first & 7;
            count = 3;
            minimum = 0x10000;
        } else
            throw std::invalid_argument("Invalid IME UTF-8");
        require(i + count <= event.text.size(), "Incomplete IME UTF-8");
        for (unsigned n = 0; n < count; ++n) {
            auto byte = uint8_t(event.text[i++]);
            require((byte & 0xc0) == 0x80, "Invalid IME continuation");
            scalar = (scalar << 6) | (byte & 63);
        }
        require(scalar >= minimum && scalar <= 0x10ffff &&
                    !(scalar >= 0xd800 && scalar <= 0xdfff) &&
                    (scalar >= 32 || scalar == '\n' || scalar == '\t'),
                "Invalid IME Unicode scalar");
    }
}
struct TextHandler : Rml::TextInputHandler {
    Rml::TextInputContext *active = nullptr;
    bool composing = false;
    int start = 0, end = 0, originalEnd = 0;
    std::string original;
    void cancel() {
        if (active && composing) {
            active->SetText(original, start, end);
            active->SetCompositionRange(0, 0);
            active->SetSelectionRange(start, originalEnd);
        }
        composing = false;
        original.clear();
    }
    void OnActivate(Rml::TextInputContext *c) override {
        if (active != c)
            cancel();
        active = c;
    }
    void OnDeactivate(Rml::TextInputContext *c) override {
        if (active == c) {
            cancel();
            active = nullptr;
        }
    }
    void OnDestroy(Rml::TextInputContext *c) override {
        if (active == c) {
            active = nullptr;
            composing = false;
            original.clear();
        }
    }
    void begin(Rml::Context &context) {
        if (composing)
            cancel();
        if (!active)
            return;
        active->GetSelectionRange(start, end);
        if (start > end)
            std::swap(start, end);
        originalEnd = end;
        for (auto *p = context.GetFocusElement(); p; p = p->GetParentNode())
            if (auto *control = dynamic_cast<Rml::ElementFormControl *>(p)) {
                auto value = control->GetValue();
                const auto a = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(value,
                                                                                        start),
                           b = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(value, end);
                original = value.substr(size_t(a), size_t(b - a));
                break;
            }
        composing = true;
    }
    void process(Rml::Context &context, const TextCompositionEvent &event) {
        validateComposition(event);
        if (!active)
            return;
        if (event.phase == CompositionPhase::End) {
            cancel();
            return;
        }
        if (event.phase == CompositionPhase::Begin) {
            begin(context);
            return;
        }
        if (!composing)
            begin(context);
        int count = int(Rml::StringUtilities::LengthUTF8(event.text));
        active->SetText(event.text, start, end);
        end = start + count;
        active->SetCompositionRange(start, end);
        if (event.phase == CompositionPhase::Commit) {
            active->CommitComposition(event.text);
            active->SetCompositionRange(0, 0);
            composing = false;
            original.clear();
        } else
            active->SetCursorPosition(start + int(std::min(event.cursor, uint32_t(count))));
    }
};
std::shared_ptr<Runtime> runtime() {
    static std::mutex mutex;
    static std::thread::id owner;
    static std::weak_ptr<Runtime> existing;
    std::lock_guard lock(mutex);
    if (owner == std::thread::id{})
        owner = std::this_thread::get_id();
    if (owner != std::this_thread::get_id())
        throw std::logic_error("All UI contexts must use the same UI thread");
    auto r = existing.lock();
    if (!r) {
        r = std::make_shared<Runtime>();
        existing = r;
    }
    return r;
}
struct Entry {
    UiDocumentId document;
    std::string id, type;
    std::function<void(const UiEvent &)> callback;
};
struct Hub {
    std::mutex mutex;
    uint64_t next = 1;
    std::map<uint64_t, Entry> entries;
};
struct Queued {
    UiEvent event;
    std::vector<std::string> ancestors;
};
std::atomic<uint64_t> nextIdentity{1};
Rml::Input::KeyIdentifier key(Key k) {
    using namespace Rml::Input;
    const int n = int(k);
    if (k >= Key::A && k <= Key::Z)
        return KeyIdentifier(KI_A + n - int(Key::A));
    if (k >= Key::D0 && k <= Key::D9)
        return KeyIdentifier(KI_0 + n - int(Key::D0));
    if (k >= Key::Keypad0 && k <= Key::Keypad9)
        return KeyIdentifier(KI_NUMPAD0 + n - int(Key::Keypad0));
    if (k >= Key::F1 && k <= Key::F12)
        return KeyIdentifier(KI_F1 + n - int(Key::F1));
    switch (k) {
    case Key::Space:
        return KI_SPACE;
    case Key::Enter:
    case Key::KeypadEnter:
        return KI_RETURN;
    case Key::Tab:
        return KI_TAB;
    case Key::Backspace:
        return KI_BACK;
    case Key::Escape:
        return KI_ESCAPE;
    case Key::Delete:
        return KI_DELETE;
    case Key::Insert:
        return KI_INSERT;
    case Key::Home:
        return KI_HOME;
    case Key::End:
        return KI_END;
    case Key::PageUp:
        return KI_PRIOR;
    case Key::PageDown:
        return KI_NEXT;
    case Key::Left:
        return KI_LEFT;
    case Key::Right:
        return KI_RIGHT;
    case Key::Up:
        return KI_UP;
    case Key::Down:
        return KI_DOWN;
    case Key::LeftShift:
        return KI_LSHIFT;
    case Key::RightShift:
        return KI_RSHIFT;
    case Key::LeftControl:
        return KI_LCONTROL;
    case Key::RightControl:
        return KI_RCONTROL;
    case Key::LeftAlt:
        return KI_LMENU;
    case Key::RightAlt:
        return KI_RMENU;
    case Key::Minus:
        return KI_OEM_MINUS;
    case Key::Equal:
        return KI_OEM_PLUS;
    case Key::Comma:
        return KI_OEM_COMMA;
    case Key::Period:
        return KI_OEM_PERIOD;
    case Key::Slash:
        return KI_OEM_2;
    default:
        return KI_UNKNOWN;
    }
}
int modifiers(Modifiers m) {
    const auto bits = unsigned(m);
    return ((bits & unsigned(Modifiers::Shift)) ? Rml::Input::KM_SHIFT : 0) |
           ((bits & unsigned(Modifiers::Control)) ? Rml::Input::KM_CTRL : 0) |
           ((bits & unsigned(Modifiers::Alt)) ? Rml::Input::KM_ALT : 0) |
           ((bits & unsigned(Modifiers::Super)) ? Rml::Input::KM_META : 0) |
           ((bits & unsigned(Modifiers::CapsLock)) ? Rml::Input::KM_CAPSLOCK : 0) |
           ((bits & unsigned(Modifiers::NumLock)) ? Rml::Input::KM_NUMLOCK : 0);
}
struct Painter : Rml::RenderInterface {
    struct Geometry {
        std::vector<OverlayVertex> vertices;
        std::vector<uint32_t> indices;
    };
    AssetStore assets;
    std::map<uintptr_t, Geometry> geometries;
    std::map<uintptr_t, TextureHandle> textures;
    std::set<uintptr_t> flippedImages;
    std::map<std::string, TextureHandle> images;
    uintptr_t next = 1;
    OverlayDesc output;
    bool clipping = false;
    Rml::Rectanglei clip;
    Mat4 transform = Mat4::identity();
    std::vector<std::string> messages;
    std::set<std::string> warned;
    void warning(const std::string &s) {
        if (warned.insert(s).second)
            messages.push_back(s);
    }
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                Rml::Span<const int> indices) override {
        Geometry g;
        g.vertices.reserve(v.size());
        for (const auto &vertex : v)
            g.vertices.push_back({{vertex.position.x, vertex.position.y},
                                  {vertex.tex_coord.x, vertex.tex_coord.y},
                                  color(vertex.colour)});
        for (auto i : indices) {
            require(i >= 0 && size_t(i) < v.size(), "Invalid RmlUi index");
            g.indices.push_back(uint32_t(i));
        }
        const auto id = next++;
        geometries.emplace(id, std::move(g));
        return id;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle id, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override {
        const auto &g = geometries.at(id);
        if (g.indices.empty())
            return;
        if (output.vertices.size() + g.vertices.size() > 4000000 ||
            output.indices.size() + g.indices.size() > 12000000 || output.draws.size() >= 65536)
            throw std::length_error("UI frame capacity exceeded");
        const auto base = uint32_t(output.vertices.size()), first = uint32_t(output.indices.size());
        for (auto v : g.vertices) {
            const auto p =
                transform * Vec4{v.position.x + translation.x, v.position.y + translation.y, 0, 1};
            require(finite(Vec3{p.x, p.y, p.z}) && std::isfinite(p.w) && std::abs(p.w) > 1e-6f,
                    "Invalid UI transform");
            v.position = {p.x / p.w, p.y / p.w};
            if (flippedImages.contains(texture))
                v.uv.y = 1 - v.uv.y;
            output.vertices.push_back(v);
        }
        for (auto i : g.indices)
            output.indices.push_back(base + i);
        OverlayDraw draw{first, uint32_t(g.indices.size()),
                         texture ? textures.at(texture) : TextureHandle{}};
        if (clipping) {
            draw.clipX = clip.Left();
            draw.clipY = clip.Top();
            draw.clipWidth = uint32_t(std::max(0, clip.Width()));
            draw.clipHeight = uint32_t(std::max(0, clip.Height()));
        } else {
            draw.clipWidth = output.width;
            draw.clipHeight = output.height;
        }
        // 仅合并相邻且状态相同的命令，始终保留透明 UI 的绘制顺序。
        if (!output.draws.empty()) {
            auto &last = output.draws.back();
            if (last.texture == draw.texture && last.clipX == draw.clipX &&
                last.clipY == draw.clipY && last.clipWidth == draw.clipWidth &&
                last.clipHeight == draw.clipHeight) {
                last.indexCount += draw.indexCount;
                return;
            }
        }
        output.draws.push_back(draw);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle id) override {
        geometries.erase(id);
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &size, const Rml::String &source) override {
        try {
            TextureHandle h;
            if (source.rfind("ui:", 0) == 0)
                h = images.at(source.substr(3));
            else {
                const auto loaded = assets.loadTexture(path(source));
                auto image = assets.textureDescription(loaded);
                assets.destroyTexture(loaded);
                if (!image.data.empty() || image.layers > 1 ||
                    image.origin != TextureOrigin::BottomLeft ||
                    image.channels != TextureChannels::Rgba8)
                    image = renderer::texture::decode(image);
                convert(image, false);
                h = assets.createTexture(std::move(image));
            }
            const auto d = assets.textureDescription(h);
            size = {int(d.width), int(d.height)};
            const auto id = next++;
            textures[id] = h;
            flippedImages.insert(id);
            return id;
        } catch (const std::exception &e) {
            messages.push_back("UI image " + source + ": " + e.what());
            return 0;
        }
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> data,
                                       Rml::Vector2i size) override {
        TextureDesc d;
        d.width = uint32_t(size.x);
        d.height = uint32_t(size.y);
        d.pixels.assign(data.begin(), data.end());
        convert(d, true);
        const auto id = next++;
        textures[id] = assets.createTexture(std::move(d));
        return id;
    }
    void ReleaseTexture(Rml::TextureHandle id) override {
        auto it = textures.find(id);
        if (it == textures.end())
            return;
        const auto h = it->second;
        textures.erase(it);
        flippedImages.erase(id);
        bool registered = false;
        for (const auto &[name, image] : images) {
            (void)name;
            registered |= image == h;
        }
        if (!registered)
            assets.destroyTexture(h);
    }
    void EnableScissorRegion(bool enabled) override {
        clipping = enabled;
    }
    void SetScissorRegion(Rml::Rectanglei r) override {
        clip = r;
    }
    void SetTransform(const Rml::Matrix4f *m) override {
        transform = Mat4::identity();
        if (m)
            std::copy_n(m->data(), 16, transform.elements.begin());
    }
    void EnableClipMask(bool enabled) override {
        if (enabled)
            warning("UI: non-rectangular clip masks are not supported by this adapter");
    }
    Rml::LayerHandle PushLayer() override {
        warning("UI: compositing layers/filters are not supported by this adapter");
        return 0;
    }
    Rml::CompiledShaderHandle CompileShader(const Rml::String &, const Rml::Dictionary &) override {
        warning("UI: gradient/custom shader decorators are not supported by this adapter");
        return 0;
    }
};
} // namespace

struct UiSubscription::Impl {
    std::weak_ptr<Hub> hub;
    uint64_t id = 0;
    ~Impl() {
        if (auto h = hub.lock()) {
            std::lock_guard lock(h->mutex);
            h->entries.erase(id);
        }
    }
};
UiSubscription::UiSubscription() = default;
UiSubscription::~UiSubscription() = default;
UiSubscription::UiSubscription(UiSubscription &&) noexcept = default;
UiSubscription &UiSubscription::operator=(UiSubscription &&) noexcept = default;
void UiSubscription::cancel() {
    impl_.reset();
}

struct UiContext::Impl {
    std::shared_ptr<Runtime> system = runtime();
    Painter painter;
    TextHandler ime;
    UiPlatformServices services;
    UiDocumentId debugDocument = 0;
    std::shared_ptr<Hub> hub = std::make_shared<Hub>();
    Rml::Context *context = nullptr;
    UiViewport viewport;
    std::string name;
    double seconds = 0;
    int mods = 0;
    Rml::Vector2f mouse{-1, -1};
    std::array<bool, 8> pressed{};
    std::set<Rml::Input::KeyIdentifier> keys;
    bool suspended = false, dispatching = false, pollOverflow = false;
    std::vector<Queued> pending;
    std::vector<UiEvent> events;
    std::vector<std::string> messages;
    struct Listener : Rml::EventListener {
        Impl *owner;
        UiDocumentId document;
        Listener(Impl *o, UiDocumentId d) : owner(o), document(d) {}
        void ProcessEvent(Rml::Event &e) override {
            if (owner->pending.size() >= 4096) {
                if (owner->messages.size() < 4096)
                    owner->messages.push_back("UI event queue limit reached");
                return;
            }
            Queued queued;
            queued.event.document = document;
            queued.event.type = e.GetType();
            auto *target = e.GetTargetElement();
            queued.event.targetId = target->GetId();
            Rml::ElementFormControl *control = nullptr;
            for (auto *p = target; p && p != owner->context->GetRootElement();
                 p = p->GetParentNode()) {
                if (!p->GetId().empty())
                    queued.ancestors.push_back(p->GetId());
                if (!control)
                    control = dynamic_cast<Rml::ElementFormControl *>(p);
            }
            if (!queued.ancestors.empty())
                queued.event.elementId = queued.ancestors.front();
            if (control) {
                queued.event.value = control->GetValue();
                queued.event.checked = control->HasAttribute("checked");
            }
            if (owner->events.size() < 4096)
                owner->events.push_back(queued.event);
            else if (!owner->pollOverflow) {
                owner->pollOverflow = true;
                owner->messages.push_back(
                    "UI polling event queue limit reached; subscriptions remain active");
            }
            owner->pending.push_back(std::move(queued));
        }
    };
    struct Document {
        Rml::ElementDocument *element;
        std::unique_ptr<Listener> listener;
        std::set<std::string> types;
    };
    std::map<UiDocumentId, Document> documents;
    explicit Impl(const UiViewport &v) : viewport(v) {
        validate(v);
        name = "renderer-ui-" + std::to_string(nextIdentity++);
        context = Rml::CreateContext(name, {int(v.width), int(v.height)}, &painter, &ime);
        if (!context)
            throw std::runtime_error("Cannot create RmlUi context");
        context->SetDensityIndependentPixelRatio(v.density);
    }
    ~Impl() {
        system->seconds = seconds;
        for (auto &[id, d] : documents) {
            (void)id;
            for (const auto &type : d.types)
                d.element->RemoveEventListener(type, d.listener.get());
        }
        Rml::RemoveContext(name);
        Rml::ReleaseRenderManagers();
    }
    Document &doc(UiDocumentId id) {
        auto it = documents.find(id);
        require(it != documents.end(), "Invalid UI document identity");
        return it->second;
    }
    Rml::Element &element(UiDocumentId id, const std::string &nameId) {
        auto *e = doc(id).element->GetElementById(nameId);
        require(e != nullptr, "UI element id does not exist");
        return *e;
    }
    void listen(Document &d, const std::string &type) {
        if (d.types.insert(type).second)
            d.element->AddEventListener(type, d.listener.get());
    }
    UiDocumentId adopt(Rml::ElementDocument *e, bool focus = true) {
        if (!e)
            throw std::runtime_error("RmlUi document load failed; see takeMessages");
        const auto id = nextIdentity++;
        Document d{e, std::make_unique<Listener>(this, id), {}};
        for (const auto *type : {"click", "change", "submit"})
            listen(d, type);
        documents.emplace(id, std::move(d));
        e->Show(Rml::ModalFlag::None, focus ? Rml::FocusFlag::Auto : Rml::FocusFlag::None);
        return id;
    }
    bool hit() const {
        auto *e = context->GetElementAtPoint(mouse);
        return e && e != context->GetRootElement() && e->GetOwnerDocument() &&
               e != e->GetOwnerDocument();
    }
    UiCapture capture() const {
        auto *e = context->GetFocusElement();
        return {hit() || std::any_of(pressed.begin(), pressed.end(), [](bool b) { return b; }),
                e && e != context->GetRootElement() && e->GetOwnerDocument() &&
                    e != e->GetOwnerDocument()};
    }
    void move(double x, double y) {
        require(std::isfinite(x) && std::isfinite(y), "Invalid UI pointer position");
        mouse = {
            float(std::clamp(x * viewport.width /
                                 (viewport.inputWidth > 0 ? viewport.inputWidth : viewport.width),
                             -1e7, 1e7)),
            float(
                std::clamp(y * viewport.height /
                               (viewport.inputHeight > 0 ? viewport.inputHeight : viewport.height),
                           -1e7, 1e7))};
        context->ProcessMouseMove(int(std::lround(mouse.x)), int(std::lround(mouse.y)), mods);
    }
    void dispatch() {
        auto queue = std::move(pending);
        pending.clear();
        dispatching = true;
        for (const auto &q : queue) {
            std::vector<uint64_t> ids;
            {
                std::lock_guard lock(hub->mutex);
                for (const auto &[id, entry] : hub->entries)
                    if (entry.document == q.event.document && entry.type == q.event.type &&
                        std::find(q.ancestors.begin(), q.ancestors.end(), entry.id) !=
                            q.ancestors.end())
                        ids.push_back(id);
            }
            for (auto id : ids) {
                std::function<void(const UiEvent &)> callback;
                UiEvent event = q.event;
                {
                    std::lock_guard lock(hub->mutex);
                    auto it = hub->entries.find(id);
                    if (it == hub->entries.end())
                        continue;
                    callback = it->second.callback;
                    event.elementId = it->second.id;
                }
                try {
                    callback(event);
                } catch (const std::exception &e) {
                    messages.push_back(std::string("UI callback: ") + e.what());
                } catch (...) {
                    messages.push_back("UI callback: unknown exception");
                }
            }
        }
        dispatching = false;
    }
};
UiContext::UiContext(const UiViewport &v) : impl_(std::make_unique<Impl>(v)) {}
UiContext::~UiContext() = default;
UiContext::UiContext(UiContext &&) noexcept = default;
UiContext &UiContext::operator=(UiContext &&) noexcept = default;
UiContext::Impl &UiContext::checked() const {
    if (!impl_)
        throw std::logic_error("UiContext has been moved from");
    if (impl_->system->thread != std::this_thread::get_id())
        throw std::logic_error("UI operation requires the UI thread");
    impl_->system->seconds = impl_->seconds;
    return *impl_;
}
void UiContext::loadFont(const std::filesystem::path &p, bool fallback, int index) {
    auto &s = checked();
    (void)s;
    require(index >= 0, "Invalid font face index");
    if (!Rml::LoadFontFace(utf8(p), fallback, Rml::Style::FontWeight::Auto, index))
        throw std::runtime_error("Cannot load UI font: " + utf8(p));
}
UiDocumentId UiContext::loadDocument(const std::filesystem::path &p) {
    auto &s = checked();
    return s.adopt(s.context->LoadDocument(utf8(p)));
}
UiDocumentId UiContext::loadDocumentFromMemory(const std::string &rml, const std::string &source) {
    auto &s = checked();
    return s.adopt(s.context->LoadDocumentFromMemory(rml, source));
}
void UiContext::unloadDocument(UiDocumentId id) {
    auto &s = checked();
    auto &d = s.doc(id);
    for (const auto &t : d.types)
        d.element->RemoveEventListener(t, d.listener.get());
    d.element->Close();
    s.documents.erase(id);
    std::lock_guard lock(s.hub->mutex);
    std::erase_if(s.hub->entries, [id](const auto &entry) { return entry.second.document == id; });
}
void UiContext::showDocument(UiDocumentId id, bool visible) {
    auto &d = checked().doc(id);
    if (visible)
        d.element->Show();
    else
        d.element->Hide();
}
UiElementState UiContext::element(UiDocumentId id, const std::string &name) const {
    auto &e = checked().element(id, name);
    const auto p = e.GetAbsoluteOffset(Rml::BoxArea::Border),
               size = e.GetBox().GetSize(Rml::BoxArea::Border);
    auto *c = dynamic_cast<Rml::ElementFormControl *>(&e);
    return {e.GetId(),
            e.GetTagName(),
            e.GetInnerRML(),
            c ? c->GetValue() : e.GetAttribute<Rml::String>("value", ""),
            {p.x, p.y, size.x, size.y},
            e.IsVisible(true),
            !c || !c->IsDisabled(),
            e.HasAttribute("checked")};
}
void UiContext::setText(UiDocumentId d, const std::string &id, const std::string &text) {
    auto &e = checked().element(d, id);
    const auto rml = escaped(text);
    if (e.GetInnerRML() != rml)
        e.SetInnerRML(rml);
}
void UiContext::setMarkup(UiDocumentId d, const std::string &id, const std::string &rml) {
    checked().element(d, id).SetInnerRML(rml);
}
void UiContext::setProperty(UiDocumentId d, const std::string &id, const std::string &property,
                            const std::string &value) {
    require(checked().element(d, id).SetProperty(property, value), "Invalid UI property/value");
}
void UiContext::setValue(UiDocumentId d, const std::string &id, const std::string &value) {
    auto &e = checked().element(d, id);
    if (auto *c = dynamic_cast<Rml::ElementFormControl *>(&e))
        c->SetValue(value);
    else
        e.SetAttribute("value", value);
}
void UiContext::setChecked(UiDocumentId d, const std::string &id, bool value) {
    auto &e = checked().element(d, id);
    if (e.HasAttribute("checked") == value)
        return;
    if (value)
        e.SetAttribute("checked", "");
    else
        e.RemoveAttribute("checked");
}
void UiContext::setEnabled(UiDocumentId d, const std::string &id, bool enabled) {
    auto *c = dynamic_cast<Rml::ElementFormControl *>(&checked().element(d, id));
    require(c != nullptr, "UI element is not a form control");
    c->SetDisabled(!enabled);
}
bool UiContext::focus(UiDocumentId d, const std::string &id) {
    return checked().element(d, id).Focus();
}
void UiContext::registerImage(const std::string &name, TextureDesc image, uint32_t layer) {
    auto &p = checked().painter;
    require(!name.empty() && name.find(':') == std::string::npos && !p.images.contains(name),
            "Invalid/duplicate UI image name");
    if (layer || image.layers > 1 || image.arrayTexture || !image.data.empty() ||
        image.origin != TextureOrigin::BottomLeft || image.channels != TextureChannels::Rgba8)
        image = renderer::texture::decode(image, layer);
    convert(image, false);
    p.images.emplace(name, p.assets.createTexture(std::move(image)));
}
void UiContext::registerImage(const std::string &name, const RenderTexture &image) {
    auto &p = checked().painter;
    require(!name.empty() && name.find(':') == std::string::npos && !p.images.contains(name),
            "Invalid UI image name");
    p.images.emplace(name, p.assets.createTexture(image));
}
void UiContext::releaseFontCache() {
    auto &s = checked();
    if (s.dispatching)
        throw std::logic_error("Cannot release UI fonts during callback dispatch");
    Rml::ReleaseFontResources();
}
void UiContext::setPlatformServices(UiPlatformServices services) {
    checked().services = std::move(services);
}
TextInputArea UiContext::textInputArea() const {
    const auto &s = checked();
    Rml::Rectanglef box;
    if (!s.ime.active || !s.ime.active->GetBoundingBox(box))
        return {};
    const float x =
        s.viewport.inputWidth > 0 ? float(s.viewport.inputWidth / float(s.viewport.width)) : 1;
    const float y =
        s.viewport.inputHeight > 0 ? float(s.viewport.inputHeight / float(s.viewport.height)) : 1;
    return {true, box.Left() * x, box.Top() * y, box.Width() * x, box.Height() * y};
}
UiDocumentId UiContext::setDebugPanel(const RenderStats &stats, const RendererMemoryStats &gpu,
                                      const AssetMemoryStats &cpu, bool visible) {
    auto &s = checked();
    if (!s.debugDocument || !s.documents.contains(s.debugDocument))
        s.debugDocument = s.adopt(s.context->LoadDocumentFromMemory(R"(<rml><head><style>
body {pointer-events:none;width:100%;height:100%;font-family:LatoLatin;font-size:14dp;color:#e5f2ff;}
div {display:block;}
#panel {position:absolute;left:12dp;top:12dp;background:#15202de8;padding:12dp;width:330dp;}
#metrics {white-space:pre;}
</style></head><body><div id="panel"><div>Renderer diagnostics</div><div id="metrics"></div></div></body></rml>)"),
                                  false);
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "CPU prepare / submit: " << stats.prepareMilliseconds << " / "
        << stats.submitMilliseconds << " ms\n";
    out << "GPU (completed frame): " << stats.gpuMilliseconds << " ms\n";
    out << "Visible / scene: " << stats.visibleInstances << " / " << stats.sceneInstances
        << "\nDraw calls: " << stats.drawCalls << "\n";
    out << "Upload: " << stats.uploadedBytes << " B, assets " << stats.assetUploadedBytes << " B\n";
    out << "Instances changed / reused: " << stats.instanceUploadedBytes << " / "
        << stats.instanceReusedBytes << " B\n";
    out << "Pending resource draws: " << stats.pendingResourceDraws << "\n";
    out << "GPU payload estimate: " << double(gpu.estimatedGpuBytes()) / 1048576 << " MiB\n";
    out << "CPU mesh / texture payload: " << double(cpu.meshBytes + cpu.textureBytes) / 1048576
        << " MiB\n";
    for (const auto &pass : stats.passes)
        out << pass.name << ": " << pass.gpuMilliseconds << " ms\n";
    setText(s.debugDocument, "metrics", out.str());
    auto *document = s.doc(s.debugDocument).element;
    if (visible && !document->IsVisible())
        document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    else if (!visible && document->IsVisible())
        document->Hide();
    return s.debugDocument;
}
void UiContext::setViewport(const UiViewport &v) {
    validate(v);
    auto &s = checked();
    s.viewport = v;
    s.suspended = false;
    s.context->SetDimensions({int(v.width), int(v.height)});
    s.context->SetDensityIndependentPixelRatio(v.density);
}
UiCapture UiContext::capture() const {
    return checked().capture();
}
bool UiContext::processEvent(const WindowEvent &event) {
    auto &s = checked();
    if (s.dispatching)
        throw std::logic_error("Cannot process UI input from a UI callback");
    PlatformScope platform(*s.system, s.services);
    return std::visit(
        [&](const auto &e) -> bool {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, MouseMoveEvent>) {
                s.move(e.x, e.y);
                return s.capture().mouse;
            } else if constexpr (std::is_same_v<T, MouseButtonEvent>) {
                s.mods = modifiers(e.modifiers);
                s.move(e.x, e.y);
                const auto n = size_t(e.button);
                require(n < 8, "Invalid UI mouse button");
                const bool captured = s.capture().mouse;
                if (e.action == InputAction::Release) {
                    s.context->ProcessMouseButtonUp(int(n), s.mods);
                    s.pressed[n] = false;
                } else {
                    s.pressed[n] = captured;
                    s.context->ProcessMouseButtonDown(int(n), s.mods);
                }
                return captured;
            } else if constexpr (std::is_same_v<T, MouseScrollEvent>) {
                require(std::isfinite(e.xOffset) && std::isfinite(e.yOffset), "Invalid UI scroll");
                const bool captured = s.capture().mouse;
                if (captured)
                    s.context->ProcessMouseWheel({float(std::clamp(-e.xOffset, -1e6, 1e6)),
                                                  float(std::clamp(-e.yOffset, -1e6, 1e6))},
                                                 s.mods);
                return captured;
            } else if constexpr (std::is_same_v<T, KeyEvent>) {
                s.mods = modifiers(e.modifiers);
                if (s.ime.composing && e.action != InputAction::Release)
                    return true;
                const auto k = key(e.key);
                const bool captured = s.capture().keyboard;
                if (e.action == InputAction::Release) {
                    if (s.keys.erase(k))
                        s.context->ProcessKeyUp(k, s.mods);
                } else if (captured || (e.key == Key::Tab && s.hit())) {
                    s.keys.insert(k);
                    s.context->ProcessKeyDown(k, s.mods);
                }
                return captured;
            } else if constexpr (std::is_same_v<T, TextCompositionEvent>) {
                s.ime.process(*s.context, e);
                return s.capture().keyboard;
            } else if constexpr (std::is_same_v<T, TextInputEvent>) {
                require(e.codepoint <= 0x10ffff &&
                            !(e.codepoint >= 0xd800 && e.codepoint <= 0xdfff),
                        "Invalid UI Unicode code point");
                const bool captured = s.capture().keyboard;
                if (captured && !s.ime.composing)
                    s.context->ProcessTextInput(Rml::Character(e.codepoint));
                return captured;
            } else if constexpr (std::is_same_v<T, CursorEnterEvent>) {
                if (!e.entered) {
                    s.mouse = {-1, -1};
                    s.context->ProcessMouseLeave();
                }
                return false;
            } else if constexpr (std::is_same_v<T, WindowFocusEvent>) {
                if (!e.focused) {
                    // 先离开/失焦再释放，避免把 Alt-Tab 等取消动作误当成按钮点击。
                    s.mouse = {-1, -1};
                    s.context->ProcessMouseLeave();
                    if (auto *f = s.context->GetFocusElement())
                        f->Blur();
                    for (auto k : s.keys)
                        s.context->ProcessKeyUp(k, 0);
                    s.keys.clear();
                    for (size_t n = 0; n < 8; ++n)
                        if (s.pressed[n])
                            s.context->ProcessMouseButtonUp(int(n), 0);
                    s.pressed.fill(false);
                    s.mods = 0;
                }
                return false;
            } else if constexpr (std::is_same_v<T, FramebufferResizeEvent>) {
                if (e.width > 0 && e.height > 0) {
                    auto v = s.viewport;
                    v.width = uint32_t(e.width);
                    v.height = uint32_t(e.height);
                    setViewport(v);
                } else
                    s.suspended = true;
                return false;
            } else if constexpr (std::is_same_v<T, WindowResizeEvent>) {
                if (e.width > 0 && e.height > 0) {
                    auto v = s.viewport;
                    v.inputWidth = e.width;
                    v.inputHeight = e.height;
                    setViewport(v);
                }
                return false;
            } else if constexpr (std::is_same_v<T, WindowContentScaleEvent>) {
                auto v = s.viewport;
                v.density = e.xScale;
                setViewport(v);
                return false;
            } else
                return false;
        },
        event);
}
void UiContext::update(double dt) {
    auto &s = checked();
    require(std::isfinite(dt) && dt >= 0 && dt <= 4, "UI time step must be in [0,4]");
    if (s.dispatching)
        throw std::logic_error("Cannot update UI from a UI callback");
    s.seconds += dt;
    s.system->seconds = s.seconds;
    s.context->Update();
    s.dispatch();
}
OverlaySnapshot UiContext::snapshot() {
    auto &s = checked();
    if (s.dispatching)
        throw std::logic_error("Cannot snapshot UI from a UI callback");
    s.context->Update();
    s.painter.output = {s.viewport.width, s.viewport.height, {}, {}, {}};
    s.painter.clipping = false;
    s.painter.transform = Mat4::identity();
    if (!s.suspended && !s.context->Render())
        throw std::runtime_error("RmlUi render failed");
    return OverlaySnapshot::create(s.painter.assets, std::move(s.painter.output));
}
UiSubscription UiContext::subscribe(UiDocumentId d, const std::string &id, const std::string &type,
                                    std::function<void(const UiEvent &)> callback) {
    auto &s = checked();
    s.element(d, id);
    require(!type.empty() && bool(callback), "Invalid UI subscription");
    s.listen(s.doc(d), type);
    UiSubscription result;
    auto p = std::make_unique<UiSubscription::Impl>();
    p->hub = s.hub;
    {
        std::lock_guard lock(s.hub->mutex);
        p->id = s.hub->next++;
        s.hub->entries.emplace(p->id, Entry{d, id, type, std::move(callback)});
    }
    result.impl_ = std::move(p);
    return result;
}
std::vector<UiEvent> UiContext::takeEvents() {
    auto &s = checked();
    auto result = std::move(s.events);
    s.events.clear();
    s.pollOverflow = false;
    return result;
}
std::vector<std::string> UiContext::takeMessages() {
    auto &s = checked();
    auto result = std::move(s.messages);
    s.messages.clear();
    for (auto &m : s.system->messages)
        result.push_back(std::move(m));
    s.system->messages.clear();
    for (auto &m : s.painter.messages)
        result.push_back(std::move(m));
    s.painter.messages.clear();
    return result;
}
} // namespace renderer
