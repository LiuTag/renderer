/** @file @brief 将左上原点 RGBA 图像编码为 PNG，并通过 Unicode 文件路径保存。 */
#include "renderer/renderer.hpp"
#include <fstream>
#include <limits>
#include <stdexcept>

// 实现和符号仅存在于本翻译单元，不要求 SDK 使用方链接第三方 PNG 库。
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4267 4505 4996)
#endif
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace renderer {
void Image::savePng(const std::filesystem::path &path) const {
    constexpr auto limit = std::numeric_limits<int>::max();
    if (!width || !height || width > uint32_t(limit / 4) || height > uint32_t(limit))
        throw std::invalid_argument("Invalid PNG dimensions");
    const uint64_t bytes = uint64_t(width) * height * 4;
    // stb 编码器使用 int 索引，验证上界避免乘法溢出和过大输入。
    const uint64_t filteredBytes = (uint64_t(width) * 4 + 1) * height;
    if (bytes != pixels.size() || filteredBytes > uint64_t(limit / 2) || path.empty())
        throw std::invalid_argument("Invalid PNG dimensions, pixels or output path");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        throw std::runtime_error("Cannot open PNG output");
    const auto write = [](void *context, void *data, int size) {
        static_cast<std::ofstream *>(context)->write(static_cast<const char *>(data), size);
    };
    const int encoded =
        stbi_write_png_to_func(write, &stream, static_cast<int>(width), static_cast<int>(height), 4,
                               pixels.data(), static_cast<int>(width * 4));
    stream.close();
    if (!encoded || !stream)
        throw std::runtime_error("Failed to encode or write PNG output");
}
} // namespace renderer
