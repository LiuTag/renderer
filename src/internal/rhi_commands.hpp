/**
 * @file
 * @brief 定义 CPU 录制命令的数据载荷及不可变命令列表实现，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_RHI_COMMANDS_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_RHI_COMMANDS_HPP_INCLUDED
#include "renderer/rhi.hpp"
#include <variant>

namespace renderer::rhi::detail {
/**
 * @brief 开始动态渲染作用域的命令载荷。
 */
struct Begin {
    RenderingDesc desc;
};
/**
 * @brief 结束动态渲染作用域的命令载荷。
 */
struct End {};
/**
 * @brief 绑定管线的命令载荷。
 */
struct Pipeline {
    PipelineHandle handle;
};
/**
 * @brief 绑定资源组的命令载荷。
 */
struct Group {
    BindingGroupHandle handle;
};
/**
 * @brief 绑定顶点流、缓冲及偏移的命令载荷。
 */
struct VertexBuffer {
    uint32_t binding;
    BufferHandle handle;
    size_t offset;
};
/**
 * @brief 绑定索引缓冲及偏移的命令载荷。
 */
struct IndexBuffer {
    BufferHandle handle;
    size_t offset;
};
/**
 * @brief 设置视口尺寸的命令载荷。
 */
struct Viewport {
    uint32_t width, height;
};
/**
 * @brief 设置裁剪矩形的命令载荷。
 */
struct Scissor {
    int x, y;
    uint32_t width, height;
};
/**
 * @brief 非索引直接绘制的命令载荷。
 */
struct Draw {
    uint32_t count, instances, first, firstInstance;
};
/**
 * @brief 索引直接绘制的命令载荷。
 */
struct DrawIndexed {
    uint32_t count, instances, first;
    int32_t base;
    uint32_t firstInstance;
};
/**
 * @brief 索引间接绘制或间接计算调度的参数缓冲命令载荷。
 */
struct Indirect {
    BufferHandle buffer;
    size_t offset;
    uint32_t count, stride;
    bool dispatch;
};
/**
 * @brief 直接计算工作组调度的命令载荷。
 */
struct Dispatch {
    uint32_t x, y, z;
};
/**
 * @brief 缓冲区域拷贝的命令载荷。
 */
struct Copy {
    BufferHandle source, destination;
    size_t size, sourceOffset, destinationOffset;
};
/**
 * @brief 显式资源访问转换的命令载荷。
 */
struct Barriers {
    std::vector<ResourceBarrier> barriers;
};
/**
 * @brief 记录查询池时间戳的命令载荷。
 */
struct Timestamp {
    QueryHandle query;
    uint32_t index;
};
/**
 * @brief 将纹理拷贝到窗口默认帧缓冲的命令载荷。
 */
struct Present {
    TextureHandle texture;
};
/**
 * @brief 纹理区域拷贝的命令载荷。
 */
struct CopyTexture {
    TextureHandle source, destination;
    TextureCopy region;
};
using Command =
    std::variant<Begin, End, Pipeline, Group, VertexBuffer, IndexBuffer, Viewport, Scissor, Draw,
                 DrawIndexed, Indirect, Dispatch, Copy, Barriers, Timestamp, Present, CopyTexture>;
} // namespace renderer::rhi::detail
namespace renderer::rhi {
/**
 * @brief 不可变命令列表的 CPU 命令记录存储。
 */
struct CommandList::Impl {
    std::vector<detail::Command> commands;
};
} // namespace renderer::rhi

#endif // RENDERER_SRC_INTERNAL_RHI_COMMANDS_HPP_INCLUDED
