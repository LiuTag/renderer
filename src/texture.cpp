/** @file @brief 实现有界 DDS/块压缩/数组及逐块 Alpha 解码；GPU 存储不附带整图 RGBA 副本。 */
#include "renderer/texture.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_DXT_IMPLEMENTATION
#include <stb/stb_dxt.h>
#define BCDEC_IMPLEMENTATION
#include <bcdec/bcdec.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace renderer::texture {
namespace {
void need(bool ok, const char *s) {
    if (!ok)
        throw std::invalid_argument(s);
}
constexpr uint64_t limit = 1ull << 30;
uint32_t blocks(TextureCompression f) {
    switch (f) {
    case TextureCompression::None:
        return 0;
    case TextureCompression::BC1:
    case TextureCompression::BC4:
        return 8;
    case TextureCompression::BC2:
    case TextureCompression::BC3:
    case TextureCompression::BC5:
    case TextureCompression::BC7:
        return 16;
    }
    throw std::invalid_argument("Invalid compression format");
}
uint64_t chainSize(const TextureDesc &d) {
    uint64_t size = 0;
    for (uint32_t m = 0; m < d.mipLevels; ++m)
        size += byteSize(d.compression, std::max(1u, d.width >> m), std::max(1u, d.height >> m),
                         d.channels);
    return size;
}
void block(const uint8_t *source, TextureCompression format, uint8_t *out) {
    switch (format) {
    case TextureCompression::BC1:
        bcdec_bc1(source, out, 16);
        break;
    case TextureCompression::BC2:
        bcdec_bc2(source, out, 16);
        break;
    case TextureCompression::BC3:
        bcdec_bc3(source, out, 16);
        break;
    case TextureCompression::BC7:
        bcdec_bc7(source, out, 16);
        break;
    case TextureCompression::BC4: {
        alignas(16) uint8_t r[16];
        bcdec_bc4(source, r, 4);
        for (int i = 0; i < 16; ++i) {
            out[i * 4] = r[i];
            out[i * 4 + 1] = out[i * 4 + 2] = 0;
            out[i * 4 + 3] = 255;
        }
        break;
    }
    case TextureCompression::BC5: {
        alignas(16) uint8_t rg[32];
        bcdec_bc5(source, rg, 8);
        for (int i = 0; i < 16; ++i) {
            out[i * 4] = rg[i * 2];
            out[i * 4 + 1] = rg[i * 2 + 1];
            out[i * 4 + 2] = 0;
            out[i * 4 + 3] = 255;
        }
        break;
    }
    default:
        throw std::invalid_argument("Expected compressed block");
    }
}
float linear(float c) {
    return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
}
float encoded(float c) {
    return c <= .0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - .055f;
}
TextureDesc downsample(const TextureDesc &d) {
    TextureDesc out;
    out.width = std::max(1u, d.width / 2);
    out.height = std::max(1u, d.height / 2);
    out.srgb = d.srgb;
    out.pixels.resize(size_t(out.width) * out.height * 4);
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x) {
            float sum[3]{}, a = 0;
            unsigned count = 0;
            // 面积盒过滤保留奇数边缘像素，RGB 按线性预乘计算，再恢复普通 Alpha。
            const uint32_t x0 = x * d.width / out.width, x1 = (x + 1) * d.width / out.width,
                           y0 = y * d.height / out.height, y1 = (y + 1) * d.height / out.height;
            for (uint32_t sy = y0; sy < y1; ++sy)
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    auto i = (size_t(sy) * d.width + sx) * 4;
                    float alpha = d.pixels[i + 3] / 255.f;
                    a += alpha;
                    ++count;
                    for (unsigned c = 0; c < 3; ++c) {
                        float v = d.pixels[i + c] / 255.f;
                        sum[c] += (d.srgb ? linear(v) : v) * alpha;
                    }
                }
            auto p = (size_t(y) * out.width + x) * 4;
            for (unsigned c = 0; c < 3; ++c) {
                float v = a > 0 ? sum[c] / a : 0;
                out.pixels[p + c] =
                    uint8_t(std::lround(std::clamp(d.srgb ? encoded(v) : v, 0.f, 1.f) * 255));
            }
            out.pixels[p + 3] = uint8_t(std::lround(a / float(count) * 255));
        }
    return out;
}
uint32_t read32(std::span<const uint8_t> b, size_t offset) {
    need(offset + 4 <= b.size(), "Truncated DDS header");
    return uint32_t(b[offset]) | (uint32_t(b[offset + 1]) << 8) | (uint32_t(b[offset + 2]) << 16) |
           (uint32_t(b[offset + 3]) << 24);
}
void write32(std::vector<uint8_t> &b, size_t offset, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b[offset + i] = uint8_t(v >> (i * 8));
}
uint32_t dxgi(const TextureDesc &d) {
    if (d.channels == TextureChannels::Alpha8)
        return 65;
    switch (d.compression) {
    case TextureCompression::None:
        return d.srgb ? 29 : 28;
    case TextureCompression::BC1:
        return d.srgb ? 72 : 71;
    case TextureCompression::BC2:
        return d.srgb ? 75 : 74;
    case TextureCompression::BC3:
        return d.srgb ? 78 : 77;
    case TextureCompression::BC4:
        return 80;
    case TextureCompression::BC5:
        return 83;
    case TextureCompression::BC7:
        return d.srgb ? 99 : 98;
    }
    throw std::invalid_argument("Invalid DDS compression");
}
} // namespace
uint64_t byteSize(TextureCompression f, uint32_t w, uint32_t h, TextureChannels channels) {
    need(w && h && w <= 16384 && h <= 16384, "Invalid texture size");
    auto b = blocks(f);
    return b ? uint64_t((w + 3) / 4) * ((h + 3) / 4) * b
             : uint64_t(w) * h * (channels == TextureChannels::Alpha8 ? 1 : 4);
}
void validate(const TextureDesc &d) {
    need(d.width && d.height && d.width <= 16384 && d.height <= 16384 && d.layers &&
             d.layers <= 2048 && d.mipLevels &&
             d.mipLevels <= uint32_t(std::bit_width(std::max(d.width, d.height))),
         "Invalid texture dimensions/layers/mips");
    need(d.origin == TextureOrigin::BottomLeft || d.origin == TextureOrigin::TopLeft,
         "Invalid texture origin");
    need(d.channels == TextureChannels::Rgba8 || d.channels == TextureChannels::Alpha8,
         "Invalid texture channels");
    need(d.channels != TextureChannels::Alpha8 ||
             (!d.srgb && d.compression == TextureCompression::None),
         "Alpha8 must be linear and uncompressed");
    (void)blocks(d.compression);
    need(!((d.compression == TextureCompression::BC4 || d.compression == TextureCompression::BC5) &&
           d.srgb),
         "BC4/BC5 require linear color space");
    if (d.renderTexture.size().first) {
        need(bool(d.renderTexture) && d.renderTexture.size() == std::pair{d.width, d.height} &&
                 d.pixels.empty() && d.data.empty() && d.layers == 1 && d.mipLevels == 1 &&
                 !d.arrayTexture && d.channels == TextureChannels::Rgba8 &&
                 d.compression == TextureCompression::None && d.srgb &&
                 d.origin == TextureOrigin::BottomLeft,
             "Invalid live GPU texture description");
        return;
    }
    const uint64_t size = chainSize(d) * d.layers;
    need(size <= limit, "Texture payload exceeds 1 GiB");
    if (d.data.empty())
        need(d.compression == TextureCompression::None && d.mipLevels == 1 &&
                 d.pixels.size() == size,
             "RGBA8 base data size mismatch");
    else
        need(d.pixels.empty() && d.data.size() == size,
             "Texture mip/layer payload length mismatch");
}
std::span<const uint8_t> subresource(const TextureDesc &d, uint32_t layer, uint32_t mip) {
    validate(d);
    need(!d.renderTexture.size().first && layer < d.layers && mip < d.mipLevels,
         "Invalid texture subresource");
    const auto &data = d.data.empty() ? d.pixels : d.data;
    uint64_t offset = layer * chainSize(d);
    for (uint32_t i = 0; i < mip; ++i)
        offset += byteSize(d.compression, std::max(1u, d.width >> i), std::max(1u, d.height >> i),
                           d.channels);
    auto count = byteSize(d.compression, std::max(1u, d.width >> mip),
                          std::max(1u, d.height >> mip), d.channels);
    return std::span(data).subspan(size_t(offset), size_t(count));
}
TextureDesc decode(const TextureDesc &d, uint32_t layer, uint32_t mip) {
    auto bytes = subresource(d, layer, mip);
    TextureDesc out;
    out.width = std::max(1u, d.width >> mip);
    out.height = std::max(1u, d.height >> mip);
    out.srgb = d.srgb;
    out.sampling = d.sampling;
    out.sampling.mipmapped = false;
    out.pixels.resize(size_t(out.width) * out.height * 4);
    if (d.channels == TextureChannels::Alpha8) {
        for (uint32_t y = 0; y < out.height; ++y)
            for (uint32_t x = 0; x < out.width; ++x) {
                auto row = d.origin == TextureOrigin::TopLeft ? out.height - 1 - y : y;
                auto at = (size_t(y) * out.width + x) * 4;
                out.pixels[at] = out.pixels[at + 1] = out.pixels[at + 2] = 255;
                out.pixels[at + 3] = bytes[size_t(row) * out.width + x];
            }
    } else if (d.compression == TextureCompression::None) {
        for (uint32_t y = 0; y < out.height; ++y) {
            auto row = d.origin == TextureOrigin::TopLeft ? out.height - 1 - y : y;
            std::memcpy(out.pixels.data() + size_t(y) * out.width * 4,
                        bytes.data() + size_t(row) * out.width * 4, size_t(out.width) * 4);
        }
    } else {
        const uint32_t stride = (out.width + 3) / 4, b = blocks(d.compression);
        alignas(16) uint8_t rgba[64];
        for (uint32_t by = 0; by < (out.height + 3) / 4; ++by)
            for (uint32_t bx = 0; bx < stride; ++bx) {
                block(bytes.data() + (size_t(by) * stride + bx) * b, d.compression, rgba);
                for (uint32_t y = 0; y < 4 && by * 4 + y < out.height; ++y)
                    for (uint32_t x = 0; x < 4 && bx * 4 + x < out.width; ++x) {
                        auto row = by * 4 + y;
                        if (d.origin == TextureOrigin::TopLeft)
                            row = out.height - 1 - row;
                        std::memcpy(out.pixels.data() + (size_t(row) * out.width + bx * 4 + x) * 4,
                                    rgba + (y * 4 + x) * 4, 4);
                    }
            }
    }
    return out;
}
TextureDesc makeArray(std::span<const TextureDesc> images) {
    need(!images.empty() && images.size() <= 2048, "Invalid texture array length");
    TextureDesc out;
    out.width = images[0].width;
    out.height = images[0].height;
    out.srgb = images[0].srgb;
    out.sampling = images[0].sampling;
    out.layers = uint32_t(images.size());
    out.arrayTexture = true;
    need(uint64_t(out.width) * out.height * out.layers * 4 <= limit,
         "Texture array exceeds CPU limit");
    for (const auto &image : images) {
        need(image.layers == 1 && image.width == out.width && image.height == out.height &&
                 image.srgb == out.srgb,
             "Array images must share dimensions/color space");
        auto raw = decode(image);
        out.pixels.insert(out.pixels.end(), raw.pixels.begin(), raw.pixels.end());
    }
    validate(out);
    return out;
}
TextureDesc mipmaps(const TextureDesc &d) {
    validate(d);
    TextureDesc out;
    out.width = d.width;
    out.height = d.height;
    out.layers = d.layers;
    out.arrayTexture = d.arrayTexture;
    out.srgb = d.srgb;
    out.sampling = d.sampling;
    out.mipLevels = uint32_t(std::bit_width(std::max(d.width, d.height)));
    need(chainSize(out) * out.layers <= limit, "RGBA mip payload exceeds 1 GiB");
    out.data.reserve(size_t(chainSize(out) * out.layers));
    for (uint32_t layer = 0; layer < d.layers; ++layer) {
        auto raw = decode(d, layer);
        for (uint32_t mip = 0; mip < out.mipLevels; ++mip) {
            out.data.insert(out.data.end(), raw.pixels.begin(), raw.pixels.end());
            if (mip + 1 < out.mipLevels)
                raw = downsample(raw);
        }
    }
    validate(out);
    return out;
}
TextureDesc compress(const TextureDesc &d, TextureCompression format, bool mipmaps) {
    validate(d);
    need(format == TextureCompression::BC1 || format == TextureCompression::BC3 ||
             format == TextureCompression::BC4 || format == TextureCompression::BC5,
         "CPU encoding supports BC1/3/4/5");
    TextureDesc out;
    out.width = d.width;
    out.height = d.height;
    out.layers = d.layers;
    out.arrayTexture = d.arrayTexture;
    out.compression = format;
    out.srgb = d.srgb;
    out.sampling = d.sampling;
    out.origin = TextureOrigin::TopLeft;
    need(!((format == TextureCompression::BC4 || format == TextureCompression::BC5) && d.srgb),
         "BC4/5 require linear source");
    out.mipLevels = mipmaps ? uint32_t(std::bit_width(std::max(d.width, d.height))) : 1;
    need(chainSize(out) * out.layers <= limit, "Compressed payload too large");
    out.data.reserve(size_t(chainSize(out) * out.layers));
    for (uint32_t layer = 0; layer < d.layers; ++layer) {
        auto raw = decode(d, layer);
        if (format == TextureCompression::BC1)
            for (size_t i = 3; i < raw.pixels.size(); i += 4)
                need(raw.pixels[i] == 255,
                     "BC1 CPU encoder requires opaque source; use BC3 for alpha");
        for (uint32_t mip = 0; mip < out.mipLevels; ++mip) {
            for (uint32_t by = 0; by < (raw.height + 3) / 4; ++by)
                for (uint32_t bx = 0; bx < (raw.width + 3) / 4; ++bx) {
                    alignas(16) uint8_t rgba[64], packed[16], rg[32];
                    for (uint32_t y = 0; y < 4; ++y)
                        for (uint32_t x = 0; x < 4; ++x) {
                            auto sy = raw.height - 1 - std::min(by * 4 + y, raw.height - 1),
                                 sx = std::min(bx * 4 + x, raw.width - 1);
                            std::memcpy(rgba + (y * 4 + x) * 4,
                                        raw.pixels.data() + (size_t(sy) * raw.width + sx) * 4, 4);
                        }
                    if (format == TextureCompression::BC4) {
                        for (unsigned i = 0; i < 16; ++i)
                            rg[i] = rgba[i * 4];
                        stb_compress_bc4_block(packed, rg);
                    } else if (format == TextureCompression::BC5) {
                        for (unsigned i = 0; i < 16; ++i) {
                            rg[i * 2] = rgba[i * 4];
                            rg[i * 2 + 1] = rgba[i * 4 + 1];
                        }
                        stb_compress_bc5_block(packed, rg);
                    } else
                        stb_compress_dxt_block(packed, rgba, format == TextureCompression::BC3,
                                               STB_DXT_HIGHQUAL);
                    out.data.insert(out.data.end(), packed, packed + blocks(format));
                }
            if (mip + 1 < out.mipLevels)
                raw = downsample(raw);
        }
    }
    validate(out);
    return out;
}
float alpha(const TextureDesc &d, uint32_t x, uint32_t y, uint32_t layer) {
    need(x < d.width && y < d.height && layer < d.layers, "Alpha sample outside texture");
    if (d.renderTexture.size().first)
        return 1;
    auto bytes = subresource(d, layer);
    if (d.origin == TextureOrigin::TopLeft)
        y = d.height - 1 - y;
    if (d.compression == TextureCompression::None)
        return bytes[d.channels == TextureChannels::Alpha8 ? size_t(y) * d.width + x
                                                           : (size_t(y) * d.width + x) * 4 + 3] /
               255.f;
    alignas(16) uint8_t rgba[64];
    block(bytes.data() + (size_t(y / 4) * ((d.width + 3) / 4) + x / 4) * blocks(d.compression),
          d.compression, rgba);
    return rgba[((y % 4) * 4 + x % 4) * 4 + 3] / 255.f;
}
TextureDesc loadDds(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    need(bool(file), "Cannot open DDS");
    auto n = file.tellg();
    need(n >= 128 && n <= std::streamoff(limit + 148), "DDS size out of range");
    std::vector<uint8_t> b(size_t(n), 0);
    file.seekg(0);
    need(bool(file.read(reinterpret_cast<char *>(b.data()), n)), "Cannot read DDS");
    need(read32(b, 0) == 0x20534444 && read32(b, 4) == 124 && read32(b, 76) == 32,
         "Invalid DDS header");
    need(!(read32(b, 112) & 0x20fe00) && read32(b, 24) <= 1,
         "DDS cubes/volume textures are unsupported");
    TextureDesc d;
    d.height = read32(b, 12);
    d.width = read32(b, 16);
    d.mipLevels = std::max(1u, read32(b, 28));
    d.origin = TextureOrigin::TopLeft;
    uint32_t offset = 128;
    auto fourcc = read32(b, 84);
    if (fourcc == 0x30315844) {
        need(b.size() >= 148 && read32(b, 132) == 3 && !(read32(b, 136) & 4),
             "Unsupported DDS DX10 resource");
        offset = 148;
        d.layers = read32(b, 140);
        d.arrayTexture = d.layers > 1;
        auto f = read32(b, 128);
        d.srgb = f == 29 || f == 72 || f == 75 || f == 78 || f == 99;
        switch (f) {
        case 65:
            d.channels = TextureChannels::Alpha8;
            d.compression = TextureCompression::None;
            break;
        case 28:
        case 29:
            d.compression = TextureCompression::None;
            break;
        case 71:
        case 72:
            d.compression = TextureCompression::BC1;
            break;
        case 74:
        case 75:
            d.compression = TextureCompression::BC2;
            break;
        case 77:
        case 78:
            d.compression = TextureCompression::BC3;
            break;
        case 80:
            d.compression = TextureCompression::BC4;
            break;
        case 83:
            d.compression = TextureCompression::BC5;
            break;
        case 98:
        case 99:
            d.compression = TextureCompression::BC7;
            break;
        default:
            throw std::invalid_argument("Unsupported DDS DXGI format");
        }
    } else {
        d.srgb = false;
        switch (fourcc) {
        case 0x31545844:
            d.compression = TextureCompression::BC1;
            break;
        case 0x33545844:
            d.compression = TextureCompression::BC2;
            break;
        case 0x35545844:
            d.compression = TextureCompression::BC3;
            break;
        case 0x31495441:
        case 0x55344342:
            d.compression = TextureCompression::BC4;
            break;
        case 0x32495441:
        case 0x55354342:
            d.compression = TextureCompression::BC5;
            break;
        case 0:
            need(read32(b, 88) == 32 && read32(b, 92) == 255 && read32(b, 96) == 0xff00 &&
                     read32(b, 100) == 0xff0000 && read32(b, 104) == 0xff000000,
                 "Only RGBA8 legacy DDS is supported");
            break;
        default:
            throw std::invalid_argument("Unsupported DDS FourCC");
        }
    }
    need(d.mipLevels <= 15, "Invalid DDS mip count");
    b.erase(b.begin(), b.begin() + offset);
    d.data = std::move(b);
    validate(d);
    return d;
}
void saveDds(const TextureDesc &d, const std::filesystem::path &path) {
    validate(d);
    need(!d.renderTexture.size().first, "Cannot save GPU-only texture as DDS");
    need(d.compression == TextureCompression::None || d.origin == TextureOrigin::TopLeft,
         "Compressed DDS requires top-left payload; encode with texture::compress");
    std::vector<uint8_t> b(148, 0);
    write32(b, 0, 0x20534444);
    write32(b, 4, 124);
    write32(b, 8,
            0x1007 | (d.compression == TextureCompression::None ? 8 : 0x80000) |
                (d.mipLevels > 1 ? 0x20000 : 0));
    write32(b, 12, d.height);
    write32(b, 16, d.width);
    write32(b, 20,
            d.compression == TextureCompression::None
                ? d.width * (d.channels == TextureChannels::Alpha8 ? 1 : 4)
                : uint32_t(byteSize(d.compression, d.width, d.height, d.channels)));
    write32(b, 28, d.mipLevels);
    write32(b, 76, 32);
    write32(b, 80, 4);
    write32(b, 84, 0x30315844);
    write32(b, 108, 0x1000 | (d.mipLevels > 1 ? 0x400008 : 0));
    write32(b, 128, dxgi(d));
    write32(b, 132, 3);
    write32(b, 140, d.layers);
    for (uint32_t l = 0; l < d.layers; ++l)
        for (uint32_t m = 0; m < d.mipLevels; ++m) {
            auto bytes = subresource(d, l, m);
            if (d.origin == TextureOrigin::TopLeft)
                b.insert(b.end(), bytes.begin(), bytes.end());
            else {
                const uint32_t w = std::max(1u, d.width >> m), h = std::max(1u, d.height >> m);
                const auto stride = size_t(w) * (d.channels == TextureChannels::Alpha8 ? 1 : 4);
                for (uint32_t y = 0; y < h; ++y)
                    b.insert(b.end(), bytes.begin() + size_t(h - 1 - y) * stride,
                             bytes.begin() + size_t(h - y) * stride);
            }
        }
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out || !out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size())))
        throw std::runtime_error("Cannot write DDS");
}
} // namespace renderer::texture
