/**
 * @file
 * @brief 定义带类型隔离、所有者和代际校验信息的轻量资源句柄。
 */
#ifndef RENDERER_INCLUDE_RENDERER_HANDLE_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_HANDLE_HPP_INCLUDED

#include <cstdint>
#include <limits>

namespace renderer {

// Tag 隔离不同资源类型；owner 隔离不同容器，generation 防止槽位复用后误用旧句柄。
// 转成 bool 仅判断是否为空，实际有效性仍由所属 Scene、AssetStore 或 Device 校验。
/**
 * @brief 使用 Tag 隔离类型、owner 隔离容器、generation 防止旧句柄命中复用槽位的值类型。
 * @tparam Tag 用于区分资源种类的标签类型，无需完整定义。
 */
template <class Tag> struct Handle {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint64_t generation = 0;
    uint64_t owner = 0;
    /**
     * @brief 判断句柄是否非空，不验证资源是否仍存活。
     * @return owner 非零时为 true，否则为 false。
     */
    explicit operator bool() const {
        return owner != 0;
    }
    /**
     * @brief 比较索引、代际与所有者是否全部相等。
     * @param other 另一个同类型句柄。
     * @return 三个字段完全一致时为 true。
     */
    bool operator==(const Handle &other) const = default;
};

} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_HANDLE_HPP_INCLUDED
