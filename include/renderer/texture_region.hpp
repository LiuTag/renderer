/** @file @brief 纹理局部更新的像素矩形，坐标遵循上传载荷的行序。 */
#ifndef RENDERER_INCLUDE_RENDERER_TEXTURE_REGION_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_TEXTURE_REGION_HPP_INCLUDED
#include <cstdint>
namespace renderer {
/** @brief 正宽高的像素区域；范围由对应纹理或 mip 校验。 */
struct TextureRegion {
    uint32_t x = 0, y = 0, width = 0, height = 0;
};
} // namespace renderer
#endif
