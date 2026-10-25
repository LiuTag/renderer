/** @file @brief 无 GPU 验证压缩误差、颜色空间/mip、数组、DDS 长度与保留的原始载荷。 */
#include <algorithm>
#include <fstream>
#include <iostream>
#include <renderer/texture.hpp>
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
    require(b, "Expected texture validation failure");
}
TextureDesc solid(uint32_t w, uint32_t h, std::array<uint8_t, 4> color) {
    TextureDesc d{w, h, std::vector<uint8_t>(size_t(w) * h * 4), false};
    for (size_t i = 0; i < d.pixels.size(); i += 4)
        std::copy(color.begin(), color.end(), d.pixels.begin() + ptrdiff_t(i));
    return d;
}
} // namespace
int main() {
    try {
        auto dir = std::filesystem::path(RENDERER_TEST_OUTPUT);
        std::filesystem::create_directories(dir);
        std::array<TextureDesc, 3> layers{solid(7, 9, {255, 0, 0, 255}),
                                          solid(7, 9, {0, 255, 0, 96}),
                                          solid(7, 9, {0, 0, 255, 255})};
        auto array = texture::makeArray(layers);
        require(array.layers == 3 && array.arrayTexture, "Array metadata lost");
        auto packed = texture::compress(array, TextureCompression::BC3);
        require(packed.data.size() < array.pixels.size() && packed.pixels.empty() &&
                    packed.mipLevels == 4,
                "Compressed data/mips not retained efficiently");
        auto green = texture::decode(packed, 1);
        require(green.pixels[1] > 245 && std::abs(int(green.pixels[3]) - 96) <= 2,
                "BC3 color or alpha decoding failed");
        require(std::abs(texture::alpha(packed, 3, 4, 1) - 96.f / 255) < .02f,
                "Per-block Alpha mismatch");
        auto file = dir / std::filesystem::path(u8"精灵数组🌲.dds");
        texture::saveDds(packed, file);
        auto read = texture::loadDds(file);
        require(read.layers == 3 && read.mipLevels == packed.mipLevels &&
                    read.data == packed.data && read.origin == TextureOrigin::TopLeft,
                "DDS re-encoded or changed layer/mip order");
        auto tiny = texture::decode(read, 2, 3);
        require(tiny.width == 1 && tiny.height == 1 && tiny.pixels[2] > 245,
                "Tail mip data is invalid");
        AssetStore assets;
        auto handle = assets.createTexture(read);
        require(assets.memoryStatistics().textureBytes == read.data.size(),
                "CPU compressed accounting includes an RGBA copy");
        throws([&] { texture::compress(array, TextureCompression::BC1); });
        auto opaque = layers[0];
        auto bc1 = texture::compress(opaque, TextureCompression::BC1);
        require(texture::decode(bc1).pixels[0] > 245, "BC1 roundtrip failed");
        auto channel = solid(8, 8, {80, 180, 0, 255});
        for (auto f : {TextureCompression::BC4, TextureCompression::BC5}) {
            auto p = texture::compress(channel, f, false);
            auto d = texture::decode(p);
            require(std::abs(int(d.pixels[0]) - 80) < 4, "BC4/5 red channel wrong");
            if (f == TextureCompression::BC5)
                require(std::abs(int(d.pixels[1]) - 180) < 4, "BC5 green channel wrong");
        }
        TextureDesc bc7;
        bc7.width = bc7.height = 4;
        bc7.srgb = false;
        bc7.compression = TextureCompression::BC7;
        bc7.origin = TextureOrigin::TopLeft;
        bc7.data.resize(16);
        bc7.data[0] = 0x40;
        auto decoded7 = texture::decode(bc7);
        require(
            std::all_of(decoded7.pixels.begin(), decoded7.pixels.end(), [](auto v) { return !v; }),
            "BC7 mode 6 decoder mismatch");
        texture::saveDds(bc7, dir / "bc7.dds");
        require(texture::loadDds(dir / "bc7.dds").data == bc7.data, "BC7 DDS payload changed");
        texture::saveDds(array, dir / "raw.dds");
        auto raw = texture::loadDds(dir / "raw.dds");
        require(texture::decode(raw, 1).pixels == layers[1].pixels, "RGBA DDS row/array mismatch");
        auto invalid = packed;
        invalid.data.pop_back();
        throws([&] { assets.createTexture(invalid); });
        invalid = packed;
        invalid.layers = 2049;
        throws([&] { texture::validate(invalid); });
        {
            std::ofstream truncated(dir / "short.dds", std::ios::binary);
            truncated << "DDS ";
        }
        throws([&] { texture::loadDds(dir / "short.dds"); });
        auto flipped = solid(4, 8, {255, 0, 0, 255});
        for (size_t i = 4 * 4 * 4; i < flipped.pixels.size(); i += 4) {
            flipped.pixels[i] = 0;
            flipped.pixels[i + 2] = 255;
        }
        auto f = texture::decode(texture::compress(flipped, TextureCompression::BC1, false));
        require(f.pixels[0] > 240 && f.pixels[(7 * 4) * 4 + 2] > 240,
                "Compressed row origin flipped");
        TextureDesc srgb{2, 1, {0, 0, 0, 255, 255, 255, 255, 255}, true};
        auto mip = texture::decode(texture::mipmaps(srgb), 0, 1);
        require(mip.pixels[0] >= 187 && mip.pixels[0] <= 189,
                "sRGB mip was filtered in encoded space");
        srgb.pixels = {255, 0, 0, 255, 0, 255, 0, 0};
        mip = texture::decode(texture::mipmaps(srgb), 0, 1);
        require(mip.pixels[0] == 255 && mip.pixels[1] == 0 && mip.pixels[3] == 128,
                "Mip Alpha produced transparent edge color bleed");
        auto rawMips = texture::mipmaps(array);
        texture::saveDds(rawMips, dir / "raw-mips.dds");
        require(texture::decode(texture::loadDds(dir / "raw-mips.dds"), 2, 3).pixels[2] == 255,
                "Raw array mip DDS order mismatch");
        std::cout << "Texture formats, compression, arrays and DDS passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
