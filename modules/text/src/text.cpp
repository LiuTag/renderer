/** @file @brief 使用 FreeType 排版/缓存字形并发布世界精灵批次，不依赖 RmlUi、窗口或私有核心头文件。
 */
#include <ft2build.h>
#include <renderer/text/text.hpp>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace renderer {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::invalid_argument(message);
}
std::atomic<uint64_t> nextId{1};
std::vector<char32_t> decode(const std::string &text) {
    require(text.size() <= 1024 * 1024, "Text exceeds 1 MiB");
    std::vector<char32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        auto first = uint8_t(text[i++]);
        uint32_t c = 0, min = 0;
        unsigned trailing = 0;
        if (first < 128)
            c = first;
        else if (first >= 0xc2 && first <= 0xdf) {
            c = first & 31;
            trailing = 1;
            min = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            c = first & 15;
            trailing = 2;
            min = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            c = first & 7;
            trailing = 3;
            min = 0x10000;
        } else
            throw std::invalid_argument("Invalid text UTF-8");
        require(i + trailing <= text.size(), "Incomplete text UTF-8");
        for (unsigned j = 0; j < trailing; ++j) {
            const auto b = uint8_t(text[i++]);
            require((b & 0xc0) == 0x80, "Invalid UTF-8 continuation");
            c = (c << 6) | (b & 63);
        }
        require(c >= min && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff) && c != 0,
                "Invalid text Unicode scalar");
        if (c == '\r') {
            if (i < text.size() && text[i] == '\n')
                ++i;
            c = '\n';
        }
        require(c >= 32 || c == '\n' || c == '\t', "Unsupported text control character");
        out.push_back(char32_t(c));
    }
    return out;
}
void colorValid(Vec4 c) {
    require(finite(Vec3{c.x, c.y, c.z}) && c.x >= 0 && c.y >= 0 && c.z >= 0 && std::isfinite(c.w) &&
                c.w >= 0 && c.w <= 1,
            "Invalid world text color");
}
Vec3 column(const Mat4 &m, int i) {
    return {m(0, i), m(1, i), m(2, i)};
}
Mat4 axes(Vec3 position, Vec3 right, Vec3 up, Vec3 back) {
    auto m = Mat4::identity();
    const Vec3 values[]{right, up, back, position};
    for (int c = 0; c < 4; ++c) {
        m(0, c) = values[c].x;
        m(1, c) = values[c].y;
        m(2, c) = values[c].z;
    }
    return m;
}
struct Library {
    FT_Library value{};
    Library() {
        if (FT_Init_FreeType(&value))
            throw std::runtime_error("FreeType initialization failed");
    }
    ~Library() {
        FT_Done_FreeType(value);
    }
};
struct Font {
    std::vector<uint8_t> bytes;
    FT_Face face{};
    uint32_t pixelSize = 0;
    ~Font() {
        if (face)
            FT_Done_Face(face);
    }
};
using GlyphKey = std::tuple<TextFontId, uint32_t, FT_UInt, TextRasterMode>;
struct Glyph {
    GlyphKey key;
    float advance = 0;
    int left = 0, top = 0;
    uint32_t width = 0, height = 0;
    size_t page = SIZE_MAX;
    SpriteRect rect;
    SpriteHandle sprite;
    uint64_t lastUse = 0;
};
struct Placement {
    GlyphKey key;
    float x = 0, y = 0, scale = 1;
};
struct Layout {
    TextMetrics metrics;
    std::vector<Placement> glyphs;
};
struct Page {
    TextureDesc image;
    TextureHandle texture;
    uint32_t x = 1, y = 1, rowHeight = 0;
    bool dirty = false;
    TextureRegion dirtyRegion;
    std::vector<GlyphKey> glyphs;
    uint64_t lastUse = 0;
};
struct Label {
    WorldTextDesc desc;
    std::optional<Layout> layout;
};
} // namespace
struct TextSystem::Impl {
    Library library; // 必须晚于字体释放。
    AssetStore assets;
    TextSystemConfig config;
    std::thread::id thread = std::this_thread::get_id();
    std::map<TextFontId, std::unique_ptr<Font>> fonts;
    std::vector<TextFontId> fallbacks;
    std::map<GlyphKey, Glyph> glyphs;
    std::vector<Page> pages;
    std::map<WorldTextId, Label> labels;
    uint32_t publishedLabels = 0, publishedGlyphs = 0;
    uint64_t clock = 0, hits = 0, misses = 0, evictedGlyphs = 0, evictedPages = 0;
    uint64_t pageRevisions = 0, dirtyPixelBytes = 0;
    std::set<GlyphKey> pins;
    explicit Impl(const TextSystemConfig &c) : config(c) {
        require(c.sdfFontSize >= 16 && c.sdfFontSize <= 128 && c.sdfSpread >= 2 &&
                    c.sdfSpread <= 32,
                "Invalid SDF configuration");
        unsigned spread = c.sdfSpread;
        if (FT_Property_Set(library.value, "sdf", "spread", &spread))
            throw std::runtime_error("FreeType SDF renderer is unavailable");
        require(c.atlasSize >= 64 && c.atlasSize <= 4096 && std::has_single_bit(c.atlasSize) &&
                    c.maxAtlasPages > 0 && c.maxAtlasPages <= 256 &&
                    uint64_t(c.atlasSize) * c.atlasSize * c.maxAtlasPages <= 128ull * 1024 * 1024 &&
                    c.maxCachedGlyphs > 0 && c.maxCachedGlyphs <= 1000000 && c.maxLabels > 0 &&
                    c.maxLabels <= 100000 && c.maxBatchGlyphs > 0 && c.maxBatchGlyphs <= 1000000,
                "Invalid text cache configuration");
    }
    Font &font(TextFontId id) const {
        auto it = fonts.find(id);
        require(it != fonts.end(), "Invalid text font identity");
        return *it->second;
    }
    Label &label(WorldTextId id) {
        auto it = labels.find(id);
        require(it != labels.end(), "Invalid world text identity");
        return it->second;
    }
    void validate(const TextStyle &s) const {
        font(s.font);
        colorValid(s.outlineColor);
        require((s.rasterMode == TextRasterMode::Bitmap || s.rasterMode == TextRasterMode::Sdf) &&
                    std::isfinite(s.outlineWidth) && s.outlineWidth >= 0 && s.outlineWidth <= 16 &&
                    std::isfinite(s.edgeSoftness) && s.edgeSoftness >= .25f && s.edgeSoftness <= 4,
                "Invalid text raster/outline parameters");
        if (s.rasterMode == TextRasterMode::Sdf && s.outlineColor.w > 0)
            require(s.outlineWidth * config.sdfFontSize / s.fontSize <
                        std::min(float(config.sdfSpread), 16.f),
                    "SDF outline exceeds distance range");
        require(s.fontSize >= 1 && s.fontSize <= 256 && std::isfinite(s.maxWidth) &&
                    s.maxWidth >= 0 && s.maxWidth <= 1000000 && std::isfinite(s.lineSpacing) &&
                    s.lineSpacing >= .5f && s.lineSpacing <= 4 && s.tabSpaces >= 1 &&
                    s.tabSpaces <= 16 &&
                    (s.alignment == TextAlignment::Left || s.alignment == TextAlignment::Center ||
                     s.alignment == TextAlignment::Right),
                "Invalid text style");
    }
    void validate(const WorldTextDesc &d) const {
        validate(d.style);
        decode(d.text);
        colorValid(d.color);
        require(d.transform.isAffine() && bool(d.transform.inverse()),
                "Text transform must be invertible affine");
        require(std::isfinite(d.anchor.x) && std::isfinite(d.anchor.y) && d.anchor.x >= 0 &&
                    d.anchor.x <= 1 && d.anchor.y >= 0 && d.anchor.y <= 1 &&
                    std::isfinite(d.pixelsPerUnit) && d.pixelsPerUnit > 0 &&
                    (d.billboard == BillboardMode::Fixed || d.billboard == BillboardMode::Upright ||
                     d.billboard == BillboardMode::ViewPlane) &&
                    (d.sizeMode == TextSizeMode::WorldUnits ||
                     d.sizeMode == TextSizeMode::ScreenPixels) &&
                    (d.sizeMode != TextSizeMode::ScreenPixels ||
                     d.billboard == BillboardMode::ViewPlane),
                "Invalid text size/billboard/anchor");
    }
    FT_Face sized(TextFontId id, uint32_t size) {
        auto &f = font(id);
        if (f.pixelSize != size) {
            if (FT_Set_Pixel_Sizes(f.face, 0, size))
                throw std::runtime_error("Cannot set text font pixel size");
            f.pixelSize = size;
        }
        return f.face;
    }
    GlyphKey key(TextFontId primary, uint32_t size, char32_t code, TextRasterMode mode) const {
        auto selected = primary;
        auto index = FT_Get_Char_Index(font(primary).face, FT_ULong(code));
        if (!index)
            for (auto id : fallbacks) {
                auto candidate = FT_Get_Char_Index(font(id).face, FT_ULong(code));
                if (candidate) {
                    selected = id;
                    index = candidate;
                    break;
                }
            }
        if (!index) {
            index = FT_Get_Char_Index(font(primary).face, 0xfffd);
            if (!index)
                index = FT_Get_Char_Index(font(primary).face, '?');
        }
        return {selected, mode == TextRasterMode::Sdf ? config.sdfFontSize : size, index, mode};
    }
    void begin() {
        pins.clear();
        ++clock;
    }
    void pin(const TextStyle &style, const std::string &text) {
        validate(style);
        for (auto c : decode(text))
            if (c != '\n')
                pins.insert(key(style.font, style.fontSize, c == '\t' ? ' ' : c, style.rasterMode));
    }
    void invalidate(const std::set<GlyphKey> &removed) {
        for (auto &[id, label] : labels) {
            (void)id;
            if (label.layout &&
                std::any_of(label.layout->glyphs.begin(), label.layout->glyphs.end(),
                            [&](const auto &p) { return removed.contains(p.key); }))
                label.layout.reset();
        }
    }
    void erasePage(size_t index) {
        auto &p = pages[index];
        std::set<GlyphKey> removed(p.glyphs.begin(), p.glyphs.end());
        for (const auto &k : removed) {
            auto it = glyphs.find(k);
            if (it != glyphs.end()) {
                if (it->second.sprite)
                    assets.destroySprite(it->second.sprite);
                glyphs.erase(it);
                ++evictedGlyphs;
            }
        }
        if (p.texture)
            assets.destroyTexture(p.texture);
        p = Page{};
        ++evictedPages;
        invalidate(removed);
    }
    size_t coldPage() const {
        size_t found = SIZE_MAX;
        for (size_t i = 0; i < pages.size(); ++i) {
            const auto &p = pages[i];
            if (!p.image.width || std::any_of(p.glyphs.begin(), p.glyphs.end(),
                                              [&](const auto &k) { return pins.contains(k); }))
                continue;
            if (found == SIZE_MAX || p.lastUse < pages[found].lastUse)
                found = i;
        }
        return found;
    }
    bool eraseColdGlyph() {
        auto victim = glyphs.end();
        for (auto it = glyphs.begin(); it != glyphs.end(); ++it)
            if (!pins.contains(it->first) &&
                (victim == glyphs.end() || it->second.lastUse < victim->second.lastUse))
                victim = it;
        if (victim == glyphs.end())
            return false;
        const auto k = victim->first;
        auto &g = victim->second;
        if (g.sprite)
            assets.destroySprite(g.sprite);
        if (g.page != SIZE_MAX) {
            auto &keys = pages[g.page].glyphs;
            std::erase(keys, k);
        }
        glyphs.erase(victim);
        ++evictedGlyphs;
        invalidate({k});
        return true;
    }
    std::pair<size_t, SpriteRect> allocate(uint32_t w, uint32_t h) {
        const auto size = config.atlasSize;
        if (w + 2 > size || h + 2 > size)
            throw std::length_error("Glyph is larger than atlas page");
        for (;;) {
            for (size_t i = 0; i <= pages.size(); ++i) {
                if (i == pages.size()) {
                    if (pages.size() >= config.maxAtlasPages)
                        break;
                    pages.emplace_back();
                }
                auto &p = pages[i];
                if (!p.image.width) {
                    const auto channels = config.compactAtlas ? 1u : 4u;
                    p.image = {size, size, std::vector<uint8_t>(size_t(size) * size * channels, 0),
                               false};
                    p.image.channels =
                        config.compactAtlas ? TextureChannels::Alpha8 : TextureChannels::Rgba8;
                    if (!config.compactAtlas)
                        for (size_t pixel = 0; pixel < p.image.pixels.size(); pixel += 4)
                            p.image.pixels[pixel] = p.image.pixels[pixel + 1] =
                                p.image.pixels[pixel + 2] = 255;
                    p.image.sampling.addressU = p.image.sampling.addressV = TextureAddress::Clamp;
                    p.image.sampling.mipmapped = false;
                    p.x = p.y = 1;
                }
                auto x = p.x, y = p.y, row = p.rowHeight;
                if (x + w + 1 > size) {
                    x = 1;
                    y += row + 2;
                    row = 0;
                }
                if (y + h + 1 > size)
                    continue;
                p.x = x + w + 2;
                p.y = y;
                p.rowHeight = std::max(row, h);
                p.lastUse = clock;
                return {i, {x, y, w, h}};
            }
            auto victim = coldPage();
            if (!config.automaticEviction || victim == SIZE_MAX)
                throw std::length_error("All text atlas pages are pinned; increase capacity");
            erasePage(victim);
        }
    }
    Glyph &glyph(TextFontId primary, uint32_t size, char32_t code, TextRasterMode mode) {
        auto k = key(primary, size, code, mode);
        if (auto it = glyphs.find(k); it != glyphs.end()) {
            ++hits;
            it->second.lastUse = clock;
            if (it->second.page != SIZE_MAX)
                pages[it->second.page].lastUse = clock;
            return it->second;
        }
        ++misses;
        while (glyphs.size() >= config.maxCachedGlyphs)
            if (!config.automaticEviction || !eraseColdGlyph())
                throw std::length_error("Required glyphs exceed cache capacity");
        auto face = sized(std::get<0>(k), std::get<1>(k));
        const bool sdf = mode == TextRasterMode::Sdf;
        if (FT_Load_Glyph(face, std::get<2>(k),
                          sdf ? (FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) : FT_LOAD_DEFAULT))
            throw std::runtime_error("Cannot load glyph");
        // 空白字符只有推进量，不向 SDF renderer 发送空轮廓。
        const bool blank =
            face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_points == 0;
        if (!blank &&
            FT_Render_Glyph(face->glyph, sdf ? FT_RENDER_MODE_SDF : FT_RENDER_MODE_NORMAL))
            throw std::runtime_error("Cannot render glyph");
        const auto &bitmap = face->glyph->bitmap;
        Glyph g{k,
                float(face->glyph->advance.x) / 64,
                face->glyph->bitmap_left,
                face->glyph->bitmap_top,
                blank ? 0 : bitmap.width,
                blank ? 0 : bitmap.rows,
                SIZE_MAX,
                {},
                {},
                clock};
        if (g.width && g.height) {
            require(bitmap.pixel_mode == FT_PIXEL_MODE_GRAY ||
                        bitmap.pixel_mode == FT_PIXEL_MODE_MONO,
                    "Unsupported glyph bitmap");
            auto [page, rect] = allocate(g.width, g.height);
            g.page = page;
            g.rect = rect;
            auto &p = pages[page];
            for (uint32_t y = 0; y < g.height; ++y) {
                const auto *row = bitmap.buffer + ptrdiff_t(y) * bitmap.pitch;
                for (uint32_t x = 0; x < g.width; ++x) {
                    auto alpha = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                                     ? ((row[x / 8] & (0x80u >> (x % 8))) ? 255 : 0)
                                     : int(row[x]) * 255 / std::max(1, int(bitmap.num_grays) - 1);
                    const auto at =
                        size_t(rect.y + g.height - 1 - y) * config.atlasSize + rect.x + x;
                    p.image.pixels[config.compactAtlas ? at : at * 4 + 3] = uint8_t(alpha);
                }
            }
            p.glyphs.push_back(k);
            if (!p.dirty)
                p.dirtyRegion = {rect.x, rect.y, rect.width, rect.height};
            else {
                auto &r = p.dirtyRegion;
                const auto x = std::min(r.x, rect.x), y = std::min(r.y, rect.y);
                r = {x, y, std::max(r.x + r.width, rect.x + rect.width) - x,
                     std::max(r.y + r.height, rect.y + rect.height) - y};
            }
            p.dirty = true;
        }
        return glyphs.emplace(k, std::move(g)).first->second;
    }
    float kerning(const Glyph *left, const Glyph &right) {
        if (!left || std::get<0>(left->key) != std::get<0>(right.key))
            return 0;
        auto face = sized(std::get<0>(right.key), std::get<1>(right.key));
        if (!FT_HAS_KERNING(face))
            return 0;
        FT_Vector value{};
        if (FT_Get_Kerning(face, std::get<2>(left->key), std::get<2>(right.key), FT_KERNING_DEFAULT,
                           &value))
            return 0;
        return float(value.x) / 64;
    }
    Layout layout(const TextStyle &style, const std::string &text) {
        validate(style);
        const auto runes = decode(text);
        Layout result;
        if (runes.empty())
            return result;
        const float scale = style.rasterMode == TextRasterMode::Sdf
                                ? float(style.fontSize) / config.sdfFontSize
                                : 1.f;
        const auto face = sized(style.font, style.fontSize);
        auto &m = result.metrics;
        m.ascent = float(face->size->metrics.ascender) / 64;
        m.descent = -float(face->size->metrics.descender) / 64;
        m.lineHeight = style.fontSize * style.lineSpacing;
        struct Line {
            size_t begin = 0, end = 0;
            float width = 0;
        };
        std::vector<Line> lines;
        float x = 0;
        size_t begin = 0;
        uint32_t row = 0;
        const Glyph *previous = nullptr;
        const auto newline = [&] {
            lines.push_back({begin, result.glyphs.size(), x});
            begin = result.glyphs.size();
            x = 0;
            ++row;
            previous = nullptr;
        };
        for (auto c : runes) {
            if (c == '\n') {
                newline();
                continue;
            }
            if (c == '\t') {
                const auto step =
                    std::max(1.f, glyph(style.font, style.fontSize, ' ', style.rasterMode).advance *
                                      style.tabSpaces * scale);
                float next = (std::floor(x / step) + 1) * step;
                if (style.maxWidth > 0 && x > 0 && next > style.maxWidth) {
                    newline();
                    next = step;
                }
                x = next;
                previous = nullptr;
                continue;
            }
            auto &g = glyph(style.font, style.fontSize, c, style.rasterMode);
            float k = kerning(previous, g) * scale;
            if (style.maxWidth > 0 && x > 0 && x + k + g.advance * scale > style.maxWidth) {
                newline();
                k = 0;
            }
            x += k;
            if (g.width && g.height)
                result.glyphs.push_back({g.key, x + g.left * scale,
                                         float(row) * m.lineHeight + m.ascent - g.top * scale,
                                         scale});
            x += g.advance * scale;
            previous = &g;
        }
        newline();
        m.lines = uint32_t(lines.size());
        m.size.y = m.lineHeight * m.lines;
        m.size.x = style.maxWidth;
        for (const auto &line : lines)
            m.size.x = std::max(m.size.x, line.width);
        bool first = true;
        for (const auto &line : lines) {
            const float offset =
                (m.size.x - line.width) * (style.alignment == TextAlignment::Left     ? 0.f
                                           : style.alignment == TextAlignment::Center ? .5f
                                                                                      : 1.f);
            for (size_t i = line.begin; i < line.end; ++i) {
                auto &p = result.glyphs[i];
                p.x += offset;
                const auto &g = glyphs.at(p.key);
                const float padding =
                    style.rasterMode == TextRasterMode::Sdf ? float(config.sdfSpread) * p.scale : 0;
                const Vec2 low{p.x + padding, p.y + padding},
                    high{p.x + g.width * p.scale - padding, p.y + g.height * p.scale - padding};
                if (first) {
                    m.inkMinimum = low;
                    m.inkMaximum = high;
                    first = false;
                } else {
                    m.inkMinimum = {std::min(m.inkMinimum.x, low.x),
                                    std::min(m.inkMinimum.y, low.y)};
                    m.inkMaximum = {std::max(m.inkMaximum.x, high.x),
                                    std::max(m.inkMaximum.y, high.y)};
                }
            }
        }
        m.glyphs = uint32_t(result.glyphs.size());
        return result;
    }
    void flush() {
        for (auto &p : pages)
            if (p.dirty) {
                TextureHandle texture;
                if (p.texture && config.incrementalAtlas) {
                    const auto r = p.dirtyRegion;
                    const auto channels = config.compactAtlas ? 1u : 4u;
                    std::vector<uint8_t> pixels(size_t(r.width) * r.height * channels);
                    for (uint32_t y = 0; y < r.height; ++y)
                        std::copy_n(p.image.pixels.begin() +
                                        (size_t(r.y + y) * config.atlasSize + r.x) * channels,
                                    size_t(r.width) * channels,
                                    pixels.begin() + size_t(y) * r.width * channels);
                    texture = assets.updateTexture(p.texture, r, pixels);
                    dirtyPixelBytes += pixels.size();
                } else {
                    texture = assets.createTexture(p.image);
                    dirtyPixelBytes += p.image.pixels.size();
                }

                std::vector<std::pair<GlyphKey, SpriteHandle>> next;
                try {
                    for (const auto &key : p.glyphs) {
                        const auto &g = glyphs.at(key);
                        SpriteDesc desc;
                        desc.texture = texture;
                        desc.rect = g.rect;
                        desc.originalSize = {float(g.width), float(g.height)};
                        desc.pivot = {0, 0};
                        desc.pixelsPerUnit = 1;
                        desc.distanceFieldRange =
                            std::get<3>(key) == TextRasterMode::Sdf ? float(config.sdfSpread) : 0;
                        next.emplace_back(key, assets.createSprite(desc));
                    }
                } catch (...) {
                    for (const auto &[key, s] : next) {
                        (void)key;
                        assets.destroySprite(s);
                    }
                    assets.destroyTexture(texture);
                    throw;
                }
                for (const auto &[key, s] : next) {
                    auto &g = glyphs.at(key);
                    if (g.sprite)
                        assets.destroySprite(g.sprite);
                    g.sprite = s;
                }
                if (p.texture)
                    assets.destroyTexture(p.texture);
                p.texture = texture;
                p.dirty = false;
                p.dirtyRegion = {};
                ++pageRevisions;
            }
    }
};
TextSystem::TextSystem(const TextSystemConfig &c) : impl_(std::make_unique<Impl>(c)) {}
TextSystem::~TextSystem() = default;
TextSystem::TextSystem(TextSystem &&) noexcept = default;
TextSystem &TextSystem::operator=(TextSystem &&) noexcept = default;
TextSystem::Impl &TextSystem::checked() const {
    if (!impl_)
        throw std::logic_error("TextSystem has been moved from");
    if (impl_->thread != std::this_thread::get_id())
        throw std::logic_error("TextSystem requires its creating thread");
    return *impl_;
}
TextFontId TextSystem::loadFont(const std::filesystem::path &path, int faceIndex) {
    auto &s = checked();
    require(faceIndex >= 0, "Invalid text font face index");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open text font file");
    const auto size = file.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024)
        throw std::runtime_error("Text font file exceeds size limit or is empty");
    auto font = std::make_unique<Font>();
    font->bytes.resize(size_t(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(font->bytes.data()), std::streamsize(size)))
        throw std::runtime_error("Cannot read text font");
    if (FT_New_Memory_Face(s.library.value, font->bytes.data(), FT_Long(font->bytes.size()),
                           faceIndex, &font->face) ||
        FT_Select_Charmap(font->face, FT_ENCODING_UNICODE))
        throw std::runtime_error("Cannot load a Unicode text font face");
    const auto id = nextId++;
    s.fonts.emplace(id, std::move(font));
    return id;
}
void TextSystem::setFallbackFonts(std::span<const TextFontId> fonts) {
    auto &s = checked();
    std::set<TextFontId> ids;
    for (auto id : fonts) {
        s.font(id);
        require(ids.insert(id).second, "Duplicate text fallback font");
    }
    std::vector<TextFontId> next(fonts.begin(), fonts.end());
    if (next == s.fallbacks)
        return;
    s.fallbacks = std::move(next);
    for (auto &[id, label] : s.labels) {
        (void)id;
        label.layout.reset();
    }
}
TextMetrics TextSystem::measure(const TextStyle &style, const std::string &text) {
    auto &s = checked();
    s.begin();
    s.pin(style, text);
    return s.layout(style, text).metrics;
}
void TextSystem::prewarm(const TextStyle &style, const std::string &text) {
    auto &s = checked();
    s.begin();
    s.pin(style, text);
    (void)s.layout(style, text);
    s.flush();
}
void TextSystem::resetGlyphCache() {
    auto &s = checked();
    s.glyphs.clear();
    s.pages.clear();
    s.assets = AssetStore{};
    for (auto &[id, label] : s.labels) {
        (void)id;
        label.layout.reset();
    }
    s.publishedLabels = s.publishedGlyphs = 0;
}
uint32_t TextSystem::trimCache(uint32_t targetPages) {
    auto &s = checked();
    s.begin();
    for (const auto &[id, label] : s.labels) {
        (void)id;
        if (label.desc.visible)
            s.pin(label.desc.style, label.desc.text);
    }
    uint32_t released = 0;
    auto active = [&] {
        return uint32_t(std::count_if(s.pages.begin(), s.pages.end(),
                                      [](const auto &p) { return p.image.width > 0; }));
    };
    while (active() > targetPages) {
        auto victim = s.coldPage();
        if (victim == SIZE_MAX)
            break;
        s.erasePage(victim);
        ++released;
    }
    return released;
}
void TextSystem::unloadFont(TextFontId id) {
    auto &s = checked();
    s.font(id);
    require(std::find(s.fallbacks.begin(), s.fallbacks.end(), id) == s.fallbacks.end(),
            "Font is used as fallback");
    for (const auto &[labelId, label] : s.labels) {
        (void)labelId;
        require(label.desc.style.font != id, "Font is used by a label");
    }
    resetGlyphCache();
    s.fonts.erase(id);
}
WorldTextId TextSystem::create(const WorldTextDesc &desc) {
    auto &s = checked();
    s.validate(desc);
    if (s.labels.size() >= s.config.maxLabels)
        throw std::length_error("World text label capacity exceeded");
    const auto id = nextId++;
    s.labels.emplace(id, Label{desc, {}});
    return id;
}
void TextSystem::setDescription(WorldTextId id, const WorldTextDesc &desc) {
    auto &s = checked();
    s.validate(desc);
    Label next{desc, {}};
    s.label(id) = std::move(next);
}
WorldTextDesc TextSystem::description(WorldTextId id) const {
    return checked().label(id).desc;
}
void TextSystem::setText(WorldTextId id, const std::string &text) {
    auto &label = checked().label(id);
    decode(text);
    if (label.desc.text == text)
        return;
    label.desc.text = text;
    label.layout.reset();
}
void TextSystem::setTransform(WorldTextId id, const Mat4 &m) {
    require(m.isAffine() && bool(m.inverse()), "Text transform must be invertible affine");
    checked().label(id).desc.transform = m;
}
void TextSystem::setColor(WorldTextId id, Vec4 c) {
    colorValid(c);
    checked().label(id).desc.color = c;
}
void TextSystem::setVisible(WorldTextId id, bool visible) {
    checked().label(id).desc.visible = visible;
}
void TextSystem::destroy(WorldTextId id) {
    auto &s = checked();
    s.label(id);
    s.labels.erase(id);
}
void TextSystem::clear() {
    checked().labels.clear();
}
SpriteBatch TextSystem::batch(const Scene &scene, NodeHandle camera, uint32_t width,
                              uint32_t height) {
    auto &s = checked();
    require(width > 0 && height > 0 && width <= 16384 && height <= 16384,
            "Invalid world text viewport");
    require(scene.valid(camera) && scene.effectiveVisible(camera),
            "Invalid/hidden text view camera");
    const auto component = scene.component<Camera>(camera);
    require(bool(component), "Text view node has no Camera");
    const auto cam = *component;
    const auto cameraWorld = scene.worldTransform(camera);
    const auto inverse = cameraWorld.inverse();
    require(bool(inverse), "Text view camera is singular");
    const float aspect = cam.aspectRatio > 0 ? cam.aspectRatio : float(width) / height;
    if (cam.projection == Camera::Projection::Perspective)
        Mat4::perspective(cam.verticalFov, aspect, cam.nearPlane, cam.farPlane);
    else {
        const auto h = cam.orthographicHeight * .5f;
        Mat4::orthographic(-h * aspect, h * aspect, -h, h, cam.nearPlane, cam.farPlane);
    }
    struct Ready {
        WorldTextId id;
        Label *label;
        Mat4 basis;
    };
    std::vector<Ready> ready;
    uint64_t count = 0;
    for (auto &[id, label] : s.labels) {
        (void)id;
        const auto &d = label.desc;
        if (!d.visible || !(d.layerMask & cam.cullingMask) ||
            (d.node && (!scene.valid(d.node) || !scene.effectiveVisible(d.node))))
            continue;
        const auto world = d.node ? scene.worldTransform(d.node) * d.transform : d.transform;
        if (!world.isAffine() || !world.inverse())
            continue;
        const auto origin = world.transformPoint({});
        Mat4 basis;
        if (d.sizeMode == TextSizeMode::ScreenPixels) {
            const float depth = -inverse->transformPoint(origin).z;
            if (depth <= 0 && cam.projection == Camera::Projection::Perspective)
                continue;
            const float unit = cam.projection == Camera::Projection::Perspective
                                   ? 2 * depth * std::tan(cam.verticalFov * .5f) / height
                                   : cam.orthographicHeight / height;
            const auto right = column(cameraWorld, 0) * (unit * aspect * height / width),
                       up = column(cameraWorld, 1) * unit;
            basis = axes(origin, right, up, normalized(cross(right, up)));
        } else if (d.billboard == BillboardMode::Fixed)
            basis = world * Mat4::scaling({1 / d.pixelsPerUnit, 1 / d.pixelsPerUnit, 1});
        else {
            Vec3 right, up, normal;
            if (d.billboard == BillboardMode::Upright) {
                normal = {cameraWorld(0, 2), 0, cameraWorld(2, 2)};
                normal = dot(normal, normal) > 1e-10f ? normalized(normal) : Vec3{0, 0, 1};
                up = {0, 1, 0};
                right = cross(up, normal);
            } else {
                right = normalized(column(cameraWorld, 0));
                normal = normalized(cross(right, column(cameraWorld, 1)));
                up = cross(normal, right);
            }
            right = right * (std::sqrt(dot(column(world, 0), column(world, 0))) / d.pixelsPerUnit);
            up = up * (std::sqrt(dot(column(world, 1), column(world, 1))) / d.pixelsPerUnit);
            if (world.determinant3x3() < 0)
                right = right * (-1);
            basis = axes(origin, right, up, normal);
        }
        require(basis.isAffine() && bool(basis.inverse()),
                "World text geometry overflow/singularity");
        ready.push_back({id, &label, basis});
    }
    s.begin();
    for (const auto &r : ready)
        s.pin(r.label->desc.style, r.label->desc.text);
    for (auto &r : ready) {
        if (!r.label->layout)
            r.label->layout = s.layout(r.label->desc.style, r.label->desc.text);
        count += r.label->layout->glyphs.size();
        if (count > s.config.maxBatchGlyphs)
            throw std::length_error("World text batch capacity exceeded");
    }
    s.flush();
    std::vector<BatchSprite> sprites;
    sprites.reserve(size_t(count));
    uint32_t labels = 0;
    for (const auto &r : ready) {
        const auto &d = r.label->desc;
        const auto &layout = *r.label->layout;
        if (layout.glyphs.empty())
            continue;
        ++labels;
        for (const auto &p : layout.glyphs) {
            auto &g = s.glyphs.at(p.key);
            g.lastUse = s.clock;
            if (g.page != SIZE_MAX)
                s.pages[g.page].lastUse = s.clock;
            const float x = p.x - d.anchor.x * layout.metrics.size.x,
                        y = layout.metrics.size.y - p.y - g.height * p.scale -
                            d.anchor.y * layout.metrics.size.y;
            SpriteRenderer c;
            c.sprite = g.sprite;
            c.billboard = BillboardMode::Fixed;
            c.tint = d.color;
            c.layerMask = d.layerMask;
            c.sortingLayer = d.sortingLayer;
            c.sortingOrder = d.sortingOrder;
            c.sortOffset = {-x / p.scale, -y / p.scale, 0};
            c.outlineColor = d.style.outlineColor;
            c.outlineWidth = d.style.outlineColor.w > 0 ? d.style.outlineWidth / p.scale : 0;
            c.edgeSoftness = d.style.edgeSoftness;
            c.depthTest = d.depthTest;
            sprites.push_back(
                {c, r.basis * Mat4::translation({x, y, 0}) * Mat4::scaling({p.scale, p.scale, 1}),
                 r.id});
        }
    }
    SpriteBatch result;
    result.replace(s.assets, sprites);
    s.publishedLabels = labels;
    s.publishedGlyphs = uint32_t(sprites.size());
    return result;
}
TextStats TextSystem::statistics() const {
    const auto &s = checked();
    uint64_t fontBytes = 0;
    for (const auto &[id, font] : s.fonts) {
        (void)id;
        fontBytes += font->bytes.size();
    }
    return {uint32_t(s.fonts.size()),
            uint32_t(s.labels.size()),
            uint32_t(s.glyphs.size()),
            uint32_t(std::count_if(s.pages.begin(), s.pages.end(),
                                   [](const auto &p) { return p.image.width > 0; })),
            s.publishedLabels,
            s.publishedGlyphs,
            uint64_t(std::count_if(s.pages.begin(), s.pages.end(),
                                   [](const auto &p) { return p.image.width > 0; })) *
                s.config.atlasSize * s.config.atlasSize * (s.config.compactAtlas ? 1 : 4),
            s.hits,
            s.misses,
            s.evictedGlyphs,
            s.evictedPages,
            s.pageRevisions,
            s.dirtyPixelBytes,
            fontBytes};
}
} // namespace renderer
