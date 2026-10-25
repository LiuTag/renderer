/** @file @brief 仅用 CPU 将普通图片/数组离线编码为带 mip 的 DDS，支持 Unicode 路径。 */
#include <array>
#include <iostream>
#include <renderer/texture.hpp>
using namespace renderer;
namespace {
std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
int run(const std::vector<std::string> &args) {
    TextureCompression format = TextureCompression::BC3;
    bool linear = false, mips = true, self = false;
    std::filesystem::path output = "texture.dds";
    std::vector<std::filesystem::path> inputs;
    for (size_t i = 1; i < args.size(); ++i) {
        auto value = [&]() {
            if (i + 1 >= args.size())
                throw std::invalid_argument("Missing option value");
            return args[++i];
        };
        const auto &a = args[i];
        if (a == "--format") {
            auto f = value();
            if (f == "bc1")
                format = TextureCompression::BC1;
            else if (f == "bc3")
                format = TextureCompression::BC3;
            else if (f == "bc4")
                format = TextureCompression::BC4;
            else if (f == "bc5")
                format = TextureCompression::BC5;
            else if (f == "rgba8")
                format = TextureCompression::None;
            else
                throw std::invalid_argument("Encoding supports rgba8/bc1/bc3/bc4/bc5");
        } else if (a == "--linear")
            linear = true;
        else if (a == "--no-mips")
            mips = false;
        else if (a == "--self-test")
            self = true;
        else if (a == "--output")
            output = path(value());
        else if (a == "--help") {
            std::cout << "renderer_texture_pack [--format bc1|bc3|bc4|bc5|rgba8] [--linear] "
                         "[--no-mips] --output FILE image1 image2 ...\n";
            return 0;
        } else if (a.starts_with("--"))
            throw std::invalid_argument("Unknown option: " + a);
        else
            inputs.push_back(path(a));
    }
    AssetStore assets;
    std::vector<TextureDesc> images;
    if (self) {
        for (unsigned c = 0; c < 2; ++c) {
            TextureDesc d{16, 16, std::vector<uint8_t>(16 * 16 * 4, 0), !linear};
            for (size_t i = 0; i < d.pixels.size(); i += 4) {
                d.pixels[i + c] = 255;
                d.pixels[i + 3] = 255;
            }
            images.push_back(std::move(d));
        }
    } else
        for (const auto &input : inputs) {
            auto h = assets.loadTexture(input, !linear);
            images.push_back(texture::decode(assets.textureDescription(h)));
            assets.destroyTexture(h);
        }
    if (images.empty())
        throw std::invalid_argument("At least one image is required");
    auto source = images.size() == 1 ? std::move(images[0]) : texture::makeArray(images);
    auto result = format == TextureCompression::None
                      ? (mips ? texture::mipmaps(source) : std::move(source))
                      : texture::compress(source, format, mips);
    texture::saveDds(result, output);
    auto loaded = texture::loadDds(output);
    if (self && (loaded.layers != 2 || loaded.mipLevels != result.mipLevels ||
                 loaded.compression != result.compression))
        throw std::runtime_error("DDS tool roundtrip failed");
    std::cout << loaded.width << 'x' << loaded.height << ", " << loaded.layers << " layers, "
              << loaded.mipLevels << " mips, " << loaded.data.size() << " payload bytes\n";
    return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            auto s = std::filesystem::path(argv[i]).u8string();
            args.emplace_back(s.begin(), s.end());
        }
        return run(args);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char **argv) {
    try {
        return run({argv, argv + argc});
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
