/** @file @brief 声明块压缩、DDS、纹理数组、显式 mip 和 CPU 解码工具，不需要图形设备。 */
#ifndef RENDERER_INCLUDE_RENDERER_TEXTURE_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_TEXTURE_HPP_INCLUDED
#include "renderer/assets.hpp"
#include <span>
namespace renderer::texture {
/** @brief 计算子资源字节数。@param format 压缩类型。@param width 正宽。@param height 正高。@return
 * RGBA8、Alpha8 或块载荷字节。@param channels 未压缩通道格式。 */
uint64_t byteSize(TextureCompression format, uint32_t width, uint32_t height,
                  TextureChannels channels = TextureChannels::Rgba8);
/** @brief 验证描述及容量。@param description 源描述。@return 无；层数 [1,2048]，载荷不超过 1 GiB。
 */
void validate(const TextureDesc &description);
/** @brief 读取子资源载荷。@param source 有效描述。@param layer 数组层。@param mip mip 级别。@return
 * 源描述存活期间有效的字节范围。 */
std::span<const uint8_t> subresource(const TextureDesc &source, uint32_t layer = 0,
                                     uint32_t mip = 0);
/** @brief 解码为左下原点 RGBA8。@param source CPU 描述。@param layer 数组层。@param mip mip
 * 级别。@return 独立单层单 mip 描述，保留颜色空间。 */
TextureDesc decode(const TextureDesc &source, uint32_t layer = 0, uint32_t mip = 0);
/** @brief 对所有层离线编码。@param source CPU 数据。@param format BC1/3/4/5，BC1 要求不透明。@param
 * mipmaps 是否生成完整颜色空间正确的 mip 链。@return TopLeft 压缩载荷，无整图解码副本。 */
TextureDesc compress(const TextureDesc &source, TextureCompression format, bool mipmaps = true);
/** @brief 构建 RGBA8 全 mip 链。@param source CPU 单层或数组描述。@return 左下原点的显式层/mip
 * 载荷，sRGB 和 Alpha 正确过滤。 */
TextureDesc mipmaps(const TextureDesc &source);
/** @brief 构建尺寸/颜色空间相同的数组。@param images 非空单层图片。@return 左下原点 RGBA8
 * 数组，可继续压缩。 */
TextureDesc makeArray(std::span<const TextureDesc> images);
/** @brief 读取二维/数组 DDS。@param path Unicode 路径。@return RGBA8 或 BC1/2/3/4/5/7
 * 原始载荷，拒绝立方体/体纹理/截断。 */
TextureDesc loadDds(const std::filesystem::path &path);
/** @brief 写 DX10 DDS，保留 mip/层/颜色空间。@param source CPU 描述，压缩数据须 TopLeft。@param
 * path Unicode 输出路径。@return 无，不二次压缩。 */
void saveDds(const TextureDesc &source, const std::filesystem::path &path);
/** @brief 读取逻辑左下像素的 Alpha，只解码所在块。@param source 有效描述。@param x 横向像素。@param
 * y 纵向像素。@param layer 数组层。@return [0,1] 覆盖率，实时输出为 1。 */
float alpha(const TextureDesc &source, uint32_t x, uint32_t y, uint32_t layer = 0);
} // namespace renderer::texture
#endif
