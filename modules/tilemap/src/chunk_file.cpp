/** @file @brief RTile 纯数据文件的限长读取、校验和及临时文件发布，不访问渲染资源。 */
#include "chunk_data.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
namespace renderer {
namespace {
constexpr std::array<uint8_t, 8> magic{'R', 'T', 'I', 'L', 'E', 0, 1, 0};
void append(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (i * 8)));
}
uint32_t number(std::span<const uint8_t> bytes, size_t offset) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= uint32_t(bytes[offset + i]) << (i * 8);
    return v;
}
uint32_t checksum(std::span<const uint8_t> bytes) {
    uint32_t value = 2166136261u;
    for (const auto b : bytes)
        value = (value ^ b) * 16777619u;
    return value;
}
} // namespace
TileChunkData readTileChunk(const std::filesystem::path &path, std::stop_token stop) {
    tile_detail::cancelled(stop);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open tile chunk file");
    const auto length = input.tellg();
    constexpr size_t maxLength = 24 + 128 * 128 * 12 + tile_detail::maxPayload;
    if (length < 24 || length > std::streamoff(maxLength))
        throw std::runtime_error("Invalid tile chunk file length");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Truncated tile chunk file");
    tile_detail::cancelled(stop);
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()))
        throw std::runtime_error("Unsupported tile chunk file version");
    TileChunkData data;
    data.chunkSize = number(bytes, 8);
    const auto count = number(bytes, 12), payload = number(bytes, 16);
    if (count > 128 * 128 || payload > tile_detail::maxPayload ||
        24 + uint64_t(count) * 12 + payload != bytes.size())
        throw std::runtime_error("Invalid tile chunk file header");
    if (checksum(std::span(bytes).first(bytes.size() - 4)) != number(bytes, bytes.size() - 4))
        throw std::runtime_error("Tile chunk checksum mismatch");
    data.tiles.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        tile_detail::cancelled(stop);
        const auto offset = 20 + size_t(i) * 12;
        data.tiles.push_back(
            {number(bytes, offset), number(bytes, offset + 4), number(bytes, offset + 8)});
    }
    data.payload.assign(bytes.begin() + 20 + size_t(count) * 12, bytes.end() - 4);
    tile_detail::validate(data);
    return data;
}
void writeTileChunk(const std::filesystem::path &path, const TileChunkData &data,
                    std::stop_token stop) {
    tile_detail::cancelled(stop);
    tile_detail::validate(data);
    std::vector<uint8_t> bytes(magic.begin(), magic.end());
    bytes.reserve(24 + data.tiles.size() * 12 + data.payload.size());
    append(bytes, data.chunkSize);
    append(bytes, uint32_t(data.tiles.size()));
    append(bytes, uint32_t(data.payload.size()));
    for (const auto &tile : data.tiles) {
        tile_detail::cancelled(stop);
        append(bytes, tile.x);
        append(bytes, tile.z);
        append(bytes, tile.type);
    }
    bytes.insert(bytes.end(), data.payload.begin(), data.payload.end());
    append(bytes, checksum(bytes));
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    static std::atomic<uint64_t> next{0};
    auto temporary = path;
    temporary += ".tmp-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(next++);
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output || !output.write(reinterpret_cast<const char *>(bytes.data()),
                                         std::streamsize(bytes.size())))
                throw std::runtime_error("Cannot write tile chunk file");
            output.close();
            if (!output)
                throw std::runtime_error("Cannot close tile chunk file");
        }
        tile_detail::cancelled(stop);
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}
} // namespace renderer
