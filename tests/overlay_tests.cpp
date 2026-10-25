/** @file @brief 无图形设备检查通用覆盖层的输入验证、资源保留、复制和独立纹理描述。 */
#include <iostream>
#include <limits>
#include <renderer/overlay.hpp>
#include <stdexcept>
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
    require(caught, "Expected overlay failure");
}
} // namespace
int main() {
    try {
        OverlaySnapshot kept;
        {
            AssetStore assets;
            auto texture = assets.createTexture({1, 1, {128, 0, 0, 128}, false});
            auto pixels = assets.textureDescription(texture);
            pixels.pixels[0] = 0;
            require(assets.textureDescription(texture).pixels[0] == 128,
                    "Texture copy leaked mutation");
            OverlayDesc desc{128,
                             64,
                             {{{0, 0}, {0, 0}, {.5f, 0, 0, .5f}},
                              {{64, 0}, {1, 0}, {.5f, 0, 0, .5f}},
                              {{0, 64}, {0, 1}, {.5f, 0, 0, .5f}}},
                             {0, 1, 2},
                             {{0, 3, texture, -2, -2, 130, 66}}};
            kept = OverlaySnapshot::create(assets, desc);
            assets.destroyTexture(texture);
            throws([&] { OverlaySnapshot::create(assets, desc); });
            desc.draws[0].texture = {};
            auto bad = desc;
            bad.width = 0;
            throws([&] { OverlaySnapshot::create(assets, bad); });
            bad = desc;
            bad.indices[0] = 3;
            throws([&] { OverlaySnapshot::create(assets, bad); });
            bad = desc;
            bad.draws[0].firstIndex = UINT32_MAX;
            throws([&] { OverlaySnapshot::create(assets, bad); });
            bad = desc;
            bad.draws[0].indexCount = 2;
            throws([&] { OverlaySnapshot::create(assets, bad); });
            bad = desc;
            bad.vertices[0].color.x = .6f;
            throws([&] { OverlaySnapshot::create(assets, bad); });
            bad = desc;
            bad.vertices[0].color.w = std::numeric_limits<float>::quiet_NaN();
            throws([&] { OverlaySnapshot::create(assets, bad); });
        }
        auto copy = kept;
        kept = {};
        require(copy.indices().size() == 3 && copy.size() == std::pair{128u, 64u},
                "Overlay snapshot lost data");
        OverlaySnapshot moved = std::move(copy);
        require(moved.vertices().size() == 3, "Overlay move failed");
        std::cout << "Overlay validation and independent resource lifetime passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
