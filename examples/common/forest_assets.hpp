/** @file @brief 森林演示和精灵性能测试共用的程序图集及元数据，不依赖外部素材。 */
#ifndef RENDERER_EXAMPLES_COMMON_FOREST_ASSETS_HPP_INCLUDED
#define RENDERER_EXAMPLES_COMMON_FOREST_ASSETS_HPP_INCLUDED
#include <algorithm>
#include <array>
#include <cmath>
#include <renderer/sprite.hpp>
#include <stdexcept>
#include <string>
#include <vector>
namespace forest_assets {
using namespace renderer;
using Color = std::array<uint8_t, 4>;
/** @brief 测试归一化像素是否位于椭圆内。@param x 横坐标。@param y 纵坐标。
 * @param cx 中心横坐标。@param cy 中心纵坐标。@param rx 横半径。@param ry 纵半径。
 * @return 点位于椭圆内部时为 true。 */
inline bool ellipse(float x, float y, float cx, float cy, float rx, float ry) {
    x = (x - cx) / rx;
    y = (y - cy) / ry;
    return x * x + y * y < 1;
}
/** @brief 生成一个演示图块的颜色。@param sprite 图块下标。@param x 归一化横坐标。
 * @param y 归一化纵坐标。@return 直通 Alpha 的 RGBA8 像素。 */
inline Color paint(int sprite, float x, float y) {
    const Color ink{42, 42, 37, 255}, bark{114, 77, 47, 255}, leaf{84, 117, 62, 255};
    Color color{};
    if (sprite < 16) {
        const int direction = sprite / 4, frame = sprite % 4;
        const float sway = frame == 1 ? -.018f : frame == 3 ? .018f : 0;
        x -= sway;
        const float stride = frame == 1 ? .045f : frame == 3 ? -.045f : 0;
        if ((x > .37f + stride && x < .46f + stride && y > .06f && y < .29f) ||
            (x > .54f - stride && x < .63f - stride && y > .06f && y < .29f))
            color = ink;
        if (ellipse(x, y, .5f, .40f, .18f, .22f))
            color = ink;
        if (ellipse(x, y, .5f, .41f, .145f, .19f))
            color = {166, 74, 57, 255};
        if (y > .42f && y < .46f && x > .33f && x < .67f)
            color = {217, 167, 72, 255};
        if (ellipse(x, y, .5f, .72f, .205f, .23f))
            color = ink;
        if (ellipse(x, y, .5f, .715f, .17f, .195f))
            color = direction == 2 ? Color{74, 55, 44, 255} : Color{226, 191, 145, 255};
        if (y > .84f && ellipse(x, y, .5f, .76f, .19f, .20f))
            color = ink;
        if (direction != 2) {
            const float eye = direction == 1 ? .6f : direction == 3 ? .4f : .43f;
            if (ellipse(x, y, eye, .73f, .022f, .035f) ||
                (direction == 0 && ellipse(x, y, .57f, .73f, .022f, .035f)))
                color = ink;
        }
    } else if (sprite < 19) {
        const float variant = float(sprite - 16);
        if (x > .45f && x < .56f && y > .06f && y < .56f)
            color = ink;
        if (x > .48f && x < .53f && y > .07f && y < .56f)
            color = bark;
        const auto canopy = [&](float margin) {
            return ellipse(x, y, .5f, .69f, .32f + margin, .25f + margin) ||
                   ellipse(x, y, .31f, .54f, .22f + margin, .19f + margin) ||
                   ellipse(x, y, .68f, .55f, .22f + margin, .20f + margin);
        };
        if (canopy(.018f))
            color = ink;
        if (canopy(0))
            color = {uint8_t(leaf[0] + variant * 12), uint8_t(leaf[1] - variant * 7),
                     uint8_t(leaf[2] - variant * 9), 255};
        if (canopy(-.03f) && y > .66f && x < .55f)
            color = {uint8_t(116 + variant * 17), uint8_t(144 - variant * 9), 70, 255};
        if ((std::sin(x * 110 + y * 23) > .96f || std::sin(y * 120 - x * 39) > .98f) &&
            canopy(-.045f)) {
            color[0] = uint8_t(color[0] * .8f);
            color[1] = uint8_t(color[1] * .8f);
            color[2] = uint8_t(color[2] * .8f);
        }
    } else if (sprite < 21) {
        const float xx = (x - .5f) / .38f, yy = (y - .32f) / .27f;
        if (xx * xx + yy * yy < 1 && y > .06f)
            color = ink;
        if (xx * xx + yy * yy < .83f && y > .085f)
            color = {137, 141, 132, 255};
        if (xx * xx + yy * yy < .65f && x < .53f && y > .3f)
            color = {177, 180, 164, 255};
        if (color[3] && std::abs(y - (.57f - x * .4f)) < .009f && x > .48f)
            color = ink;
    } else if (sprite == 21) {
        const float d = (x - .5f) * (x - .5f) / .19f + (y - .5f) * (y - .5f) / .19f;
        if (d < 1)
            color = {25, 28, 21, uint8_t(95 * (1 - d) * (1 - d))};
    } else if (sprite == 22) {
        if (ellipse(x, y, .5f, .865f, .28f, .045f) ||
            (x > .35f && x < .65f && y > .87f && y < .97f))
            color = ink;
        if ((x > .37f && x < .63f && y > .88f && y < .955f) ||
            ellipse(x, y, .5f, .866f, .25f, .025f))
            color = {208, 158, 69, 255};
    } else if (sprite == 23) {
        const float d = std::sqrt((x - .5f) * (x - .5f) + (y - .5f) * (y - .5f));
        if (d > .30f && d < .38f)
            color = {245, 213, 112, 210};
    } else {
        if (ellipse(x, y, .5f, .18f, .018f, .12f))
            color = {65, 95, 46, 255};
        for (int i = 0; i < 5; ++i) {
            const float a = float(i) * radians(72);
            if (ellipse(x, y, .5f + std::cos(a) * .09f, .36f + std::sin(a) * .09f, .065f, .065f))
                color = {220, 198, 146, 255};
        }
        if (ellipse(x, y, .5f, .36f, .04f, .04f))
            color = {179, 126, 51, 255};
    }
    return color;
}
/** @brief 计算图集列数并限制内存规模。@param images 图块数，25～1024。@return 向上取整的平方根。 */
inline uint32_t columns(uint32_t images) {
    if (images < 25 || images > 1024)
        throw std::invalid_argument("Forest atlas images must be in [25,1024]");
    return uint32_t(std::ceil(std::sqrt(double(images))));
}
/** @brief 生成森林图集；扩展图块保留同类几何/Alpha，只改变配色和物理位置。
 * @param images 图块种类数，25～1024，默认保留交互森林的原始图集。
 * @return 左下行序 RGBA8 图集；每格 100×100，其中内容 96×96。
 */
inline TextureDesc makeAtlas(uint32_t images = 25) {
    const auto cols = columns(images);
    if (images != 25) {
        const auto original = makeAtlas();
        const auto width = cols * 100, height = ((images + cols - 1) / cols) * 100;
        TextureDesc result{width, height, std::vector<uint8_t>(size_t(width) * height * 4), true};
        result.sampling = original.sampling;
        for (uint32_t image = 0; image < images; ++image) {
            const auto base = image % 25, variant = image / 25;
            for (uint32_t y = 0; y < 96; ++y)
                for (uint32_t x = 0; x < 96; ++x) {
                    const auto from =
                        (size_t(base / 5 * 100 + 2 + y) * 500 + base % 5 * 100 + 2 + x) * 4;
                    const auto to =
                        (size_t(image / cols * 100 + 2 + y) * width + image % cols * 100 + 2 + x) *
                        4;
                    for (size_t channel = 0; channel < 4; ++channel) {
                        const float factor =
                            variant && channel < 3
                                ? .85f +
                                      float((variant * (37 + uint32_t(channel) * 16)) % 101) * .003f
                                : 1;
                        result.pixels[to + channel] = uint8_t(std::clamp(
                            float(original.pixels[from + channel]) * factor, 0.0f, 255.0f));
                    }
                }
        }
        return result;
    }
    TextureDesc t{500, 500, std::vector<uint8_t>(500 * 500 * 4), true};
    t.sampling.mipmapped = false; // 示例采用留边且不生成 mip 的小图集；生产素材应离线外扩边缘。
    t.sampling.addressU = t.sampling.addressV = TextureAddress::Clamp;
    for (int sprite = 0; sprite < 25; ++sprite)
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 96; ++x) {
                std::array<float, 4> sum{};
                // 2×2 覆盖采样，先累积预乘颜色再还原直通 Alpha，减少轮廓黑边。
                for (float sy : {.25f, .75f})
                    for (float sx : {.25f, .75f}) {
                        const auto c = paint(sprite, (float(x) + sx) / 96, (float(y) + sy) / 96);
                        for (int k = 0; k < 3; ++k)
                            sum[k] += float(c[k]) * float(c[3]) / 255;
                        sum[3] += float(c[3]);
                    }
                const auto offset =
                    (size_t(sprite / 5 * 100 + 2 + y) * 500 + size_t(sprite % 5 * 100 + 2 + x)) * 4;
                if (sum[3] > 0)
                    for (int k = 0; k < 3; ++k)
                        t.pixels[offset + k] =
                            uint8_t(std::clamp(sum[k] * 255 / sum[3], 0.0f, 255.0f));
                t.pixels[offset + 3] = uint8_t(sum[3] / 4);
            }
    return t;
}
/** @brief 构建与图集对应的条目、脚底锚点和尺寸。@param images 图块数，25～1024。
 * @return 与 makeAtlas(images) 一一对应的命名图块。
 */
inline std::vector<SpriteAtlasEntry> makeEntries(uint32_t images = 25) {
    const auto cols = columns(images);
    std::vector<SpriteAtlasEntry> entries;
    for (uint32_t i = 0; i < images; ++i) {
        const auto base = i % 25;
        SpriteDesc d{.rect = {i % cols * 100 + 2, i / cols * 100 + 2, 96, 96},
                     .pivot = {.5f, .06f},
                     .pixelsPerUnit = base >= 16 && base < 19 ? 36.0f : 64.0f};
        if (base == 21 || base == 23)
            d.pivot = {.5f, .5f};
        entries.push_back({std::to_string(i), d});
    }
    return entries;
}
} // namespace forest_assets
#endif // RENDERER_EXAMPLES_COMMON_FOREST_ASSETS_HPP_INCLUDED
