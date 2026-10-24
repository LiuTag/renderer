/**
 * @file
 * @brief 声明渲染图的逻辑资源、Pass 依赖、编译与执行接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_RENDER_GRAPH_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_RENDER_GRAPH_HPP_INCLUDED
#include "renderer/rhi.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace renderer {
struct GraphResourceTag;
using GraphResource = Handle<GraphResourceTag>;
/**
 * @brief 图访问的 mip 或缓冲字节子范围，数量为零表示从起点到资源末尾。
 */
struct GraphRange {
    uint32_t firstMip = 0, mipCount = 0; // 纹理 mip 范围，mipCount=0 表示到末尾。
    size_t offset = 0, size = 0;         // 缓冲字节范围，size=0 表示到末尾。
};
/**
 * @brief 一个 Pass 对逻辑资源的访问方式、流水线阶段与范围声明。
 */
struct GraphAccess {
    GraphResource resource;
    rhi::Access access = rhi::Access::SampledRead;
    rhi::Stage stages = rhi::Stage::Fragment;
    GraphRange range;
};
/**
 * @brief 编译后 Pass 的名称及屏障数量。
 */
struct GraphPassInfo {
    std::string name;
    uint32_t barriers = 0;
};
/**
 * @brief 渲染图 Pass 裁剪及逻辑/物理资源复用统计。
 */
struct GraphStatistics {
    uint32_t declaredPasses = 0, executedPasses = 0, culledPasses = 0, logicalResources = 0,
             physicalResources = 0;
};
// 仅在 Pass 回调期间使用；只能解析该 Pass 已声明的图资源。
/**
 * @brief 以 PImpl 提供仅在当前 Pass 回调期间有效、受声明范围约束的资源解析器。
 */
class GraphResources {
  public:
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~GraphResources();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    GraphResources(GraphResources &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    GraphResources &operator=(GraphResources &&other) noexcept;
    /**
     * @brief 在 Pass 回调中解析当前 Pass 已声明的纹理。
     * @param resource 属于本图、类型为纹理的逻辑资源。
     * @return 本次执行对应的 RHI 纹理句柄。
     */
    rhi::TextureHandle texture(GraphResource resource) const;
    /**
     * @brief 在 Pass 回调中解析当前 Pass 已声明的缓冲。
     * @param resource 属于本图、类型为缓冲的逻辑资源。
     * @return 本次执行对应的 RHI 缓冲句柄。
     */
    rhi::BufferHandle buffer(GraphResource resource) const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    /**
     * @brief 由编译图构建当前 Pass 的资源解析器。
     * @param impl 资源映射与访问限制的独占状态。
     * @return 无（构造函数）。
     */
    explicit GraphResources(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class CompiledRenderGraph;
};
/**
 * @brief Pass 命令录制回调，无返回值；两个参数仅在本次回调期间有效。
 * @param encoder 用于录制本 Pass 命令的编码器，不应在回调中调用 finish。
 * @param resources 仅允许解析本 Pass 已声明资源的映射器。
 */
using GraphCallback =
    std::function<void(rhi::CommandEncoder &encoder, const GraphResources &resources)>;
// 保存裁剪/排序/屏障及物理复用方案，首次执行后缓存 RHI 资源。
// 回调捕获的外部对象须在执行期间有效；执行和析构应在设备线程进行。
/**
 * @brief 以 PImpl 保存编译结果，首次执行绑定设备并缓存可反复复用的临时资源。
 */
class CompiledRenderGraph {
  public:
    /**
     * @brief 创建空的编译图。
     * @return 无（构造函数）。
     */
    CompiledRenderGraph();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~CompiledRenderGraph();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    CompiledRenderGraph(CompiledRenderGraph &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    CompiledRenderGraph &operator=(CompiledRenderGraph &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    CompiledRenderGraph(const CompiledRenderGraph &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    CompiledRenderGraph &operator=(const CompiledRenderGraph &other) = delete;
    /**
     * @brief 读取逻辑分析及物理复用统计。
     * @return 声明、执行、剔除的 Pass 数与资源数量。
     */
    GraphStatistics statistics() const;
    /**
     * @brief 读取实际执行顺序与各 Pass 屏障数量。
     * @return 按执行顺序排列的 Pass 信息副本。
     */
    std::vector<GraphPassInfo> passes() const;
    // 首次执行绑定设备并分配临时资源，后续不可切换到其他设备。
    // 内部共享设备状态，因此 Device 包装对象移动不会使编译图失效。
    /**
     * @brief 在设备线程执行编译图；首次分配资源并绑定设备，后续复用且不可换设备。
     * @param device 执行命令的 RHI 设备，包装对象可移动但后端身份须保持一致。
     * @param timestamps 可选时间戳池，需容纳每个执行 Pass 前后各一次时间戳。
     * @return 本次 GPU 提交的完成标记。
     */
    rhi::CompletionToken execute(rhi::Device &device, rhi::QueryHandle timestamps = {});

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class RenderGraph;
};
// 声明逻辑资源及访问关系；compile 只分析 CPU 数据，不创建 GPU 对象。
/**
 * @brief 以 PImpl 声明资源和 Pass 关系，并仅在 CPU 上分析编译的渲染图构建器。
 */
class RenderGraph {
  public:
    /**
     * @brief 创建空的逻辑渲染图。
     * @return 无（构造函数）。
     */
    RenderGraph();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~RenderGraph();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    RenderGraph(RenderGraph &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    RenderGraph &operator=(RenderGraph &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    RenderGraph(const RenderGraph &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    RenderGraph &operator=(const RenderGraph &other) = delete;
    /**
     * @brief 声明由编译图分配与回收的临时纹理。
     * @param description 纹理尺寸、格式、mip 数及调试名称。
     * @return 当前图中的逻辑纹理句柄。
     */
    GraphResource createTexture(const rhi::TextureDesc &description);
    /**
     * @brief 声明由编译图分配与回收的临时缓冲。
     * @param description 缓冲字节数、内存类型和调试名称。
     * @return 当前图中的逻辑缓冲句柄。
     */
    GraphResource createBuffer(const rhi::BufferDesc &description);
    /**
     * @brief 引入外部纹理，不接管其所有权。
     * @param texture 执行设备中有效的外部纹理。
     * @param description 与外部纹理一致的描述。
     * @param initial 执行图之前纹理已有的访问状态。
     * @return 用于声明图内访问的逻辑资源句柄。
     */
    GraphResource importTexture(rhi::TextureHandle texture, const rhi::TextureDesc &description,
                                rhi::Access initial = rhi::Access::SampledRead);
    /**
     * @brief 引入外部缓冲，不接管其所有权。
     * @param buffer 执行设备中有效的外部缓冲。
     * @param description 与外部缓冲一致的描述。
     * @param initial 执行图之前缓冲已有的访问状态。
     * @return 用于声明图内访问的逻辑资源句柄。
     */
    GraphResource importBuffer(rhi::BufferHandle buffer, const rhi::BufferDesc &description,
                               rhi::Access initial = rhi::Access::UniformRead);
    // 按资源生产顺序添加 Pass；不影响导出结果且无 sideEffect 的分支会被剔除。
    // 外部可变资源应 import 并声明访问，回调中的命令不会被反向分析。
    /**
     * @brief 按资源生产顺序添加 Pass；外部资源也须显式声明访问。
     * @param name 诊断及计时使用的 Pass 名称。
     * @param accesses 全部资源的访问方式、阶段和子范围。
     * @param callback 执行时录制命令的回调，其捕获对象须保持有效。
     * @param sideEffect 为 true 时即使无导出依赖也保留此 Pass。
     * @return Pass 编号，供 dependsOn 指定依赖。
     */
    uint32_t addPass(std::string name, std::vector<GraphAccess> accesses, GraphCallback callback,
                     bool sideEffect = false);
    /**
     * @brief 增加显式执行依赖，要求 pass 在 dependency 之后执行。
     * @param pass 需要增加前置依赖的 Pass 编号。
     * @param dependency 必须先执行的 Pass 编号。
     * @return 无。
     */
    void dependsOn(uint32_t pass, uint32_t dependency); // pass 必须在 dependency 之后执行。
    /**
     * @brief 标记图输出，保留其生产依赖并使资源存活到执行结束。
     * @param resource 当前图中的有效逻辑资源。
     * @return 无。
     */
    void exportResource(GraphResource resource); // 保留输出依赖，且资源存活到图执行结束。
    /**
     * @brief 在 CPU 上校验初始化、范围、访问冲突与依赖环，生成裁剪、屏障及资源复用方案。
     * @return 可在设备线程反复执行的编译图；分析失败抛出异常。
     */
    CompiledRenderGraph compile() const; // 校验初始化、范围、重叠访问及保留 Pass 的依赖环。

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_RENDER_GRAPH_HPP_INCLUDED
