/** @file @brief 不启动图形设备，验证 PNG 的尺寸、行序、Alpha、Unicode 路径和失败行为。 */
#include "internal/assets.hpp"
#include "renderer/renderer.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace renderer;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void throws(F action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected image validation or IO failure");
}
} // namespace
int main() {
    try {
        const auto path = std::filesystem::path(L"image-tests/中文目录/透明图像.png");
        Image image{3, 2, {255, 0, 0, 255, 0,  255, 0,  128, 0,  0,  255, 0,
                           2,   3, 4, 5,   21, 22,  23, 24,  81, 82, 83,  84}};
        image.savePng(path);
        std::ifstream file(path, std::ios::binary);
        unsigned char signature[8]{};
        file.read(reinterpret_cast<char *>(signature), 8);
        require(file.good() && signature[0] == 137 && signature[1] == 'P' && signature[2] == 'N' &&
                    signature[3] == 'G',
                "Missing PNG signature");
        AssetStore assets;
        auto texture = assets.loadTexture(path, false);
        auto material = assets.createMaterial({.baseColorTexture = texture});
        const auto &decoded = detail::AssetAccess::material(assets, material)->texture->description;
        require(decoded.width == 3 && decoded.height == 2, "PNG dimensions changed");
        for (size_t y = 0; y < 2; ++y)
            for (size_t x = 0; x < 12; ++x)
                require(decoded.pixels[(1 - y) * 12 + x] == image.pixels[y * 12 + x],
                        "PNG row order, color or alpha changed");
        throws([&] { Image{}.savePng(path); });
        throws([&] { Image{1, 2, {1, 2, 3, 4}}.savePng(path); });
        throws([&] { Image{0x80000000u, 0x80000000u, {}}.savePng(path); });
        throws([&] { image.savePng({}); });
        throws([&] { image.savePng(path.parent_path()); });
        throws([&] { image.savePng(path / "child.png"); });
        image.savePng(path); // 重复写入有效输出，验证失败后无全局编码状态污染。
        std::cout << "PNG RGBA, row order, Unicode and IO checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
