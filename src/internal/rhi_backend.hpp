/**
 * @file
 * @brief 定义 RHI 后端虚接口及设备内部桥接，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_RHI_BACKEND_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_RHI_BACKEND_HPP_INCLUDED
#include "renderer/rhi.hpp"
#include "rhi_commands.hpp"
namespace renderer::detail {
struct WindowState;
}
namespace renderer::rhi::detail {
/**
 * @brief 由具体图形后端实现的资源、管线、命令回放及同步虚接口。
 */
class DeviceBackend {
  public:
    UploadStatistics uploadStats;
    virtual void beginUploadBatch() = 0;
    virtual CompletionToken endUploadBatch() = 0;
    virtual void uploadRegion(TextureHandle, TextureRegion, std::span<const uint8_t>, uint32_t,
                              uint32_t) = 0;
    virtual void copyTexture(TextureHandle a, TextureHandle b, const TextureCopy &r) {
        (void)submit({detail::CopyTexture{a, b, r}});
    }
    /**
     * @brief 在设备线程销毁具体后端。
     * @return 无（析构函数）。
     */
    virtual ~DeviceBackend() = default;
    /**
     * @brief 查询后端能力与绑定限制。
     * @return 设备信息、资源上限、对齐要求及功能支持。
     */
    virtual DeviceCaps capabilities() const = 0;
    /**
     * @brief 查询指定纹理格式支持的用途。
     * @param format 需要查询的纹理格式。
     * @return 采样、颜色附件、深度附件及存储图像支持标记。
     */
    virtual FormatFeatures formatFeatures(Format format) const = 0;
    /**
     * @brief 创建 GPU 缓冲并可选初始化前部数据。
     * @param description 正字节数、内存类型和调试名称。
     * @param initial 不超过缓冲容量的初始字节数据，调用期间复制。
     * @return 当前设备的新缓冲句柄。
     */
    virtual BufferHandle createBuffer(const BufferDesc &description,
                                      std::span<const std::byte> initial) = 0;
    /**
     * @brief 按尺寸、格式与 mip 数分配纹理存储。
     * @param description 有效的二维纹理描述。
     * @return 当前设备的新纹理句柄；内容需后续上传或写入。
     */
    virtual TextureHandle createTexture(const TextureDesc &description) = 0;
    /**
     * @brief 创建共享底层存储的 mip 视图；销毁原句柄不影响存活视图。
     * @param texture 同一设备的源纹理或视图。
     * @param firstMip 相对于源纹理/视图的首个 mip。
     * @param mipCount 视图包含的正 mip 层数。
     * @return 独立管理生命周期的新纹理视图句柄。
     */
    virtual TextureHandle createTextureView(TextureHandle texture, uint32_t firstMip,
                                            uint32_t mipCount, uint32_t firstLayer,
                                            uint32_t layerCount, bool arrayView) = 0;
    /**
     * @brief 创建不可变纹理采样器。
     * @param description 过滤、寻址、mip 与深度比较设置。
     * @return 当前设备的新采样器句柄。
     */
    virtual SamplerHandle createSampler(const SamplerDesc &description) = 0;
    /**
     * @brief 创建包含着色器、顶点格式、附件格式和固定状态的图形管线。
     * @param description 有效的图形管线配置，着色器接口须匹配。
     * @return 新图形管线句柄；创建或链接失败抛出异常。
     */
    virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &description) = 0;
    /**
     * @brief 从计算着色器产物创建计算管线。
     * @param shader Compute 阶段的有效 SPIR-V 及反射信息。
     * @param name 可选调试名称。
     * @return 新计算管线句柄。
     */
    virtual PipelineHandle createComputePipeline(const ShaderArtifact &shader,
                                                 const std::string &name) = 0;
    /**
     * @brief 创建一个逻辑描述符组。
     * @param description 逻辑 set 编号及缓冲、纹理、采样器和图像条目。
     * @return 新的绑定组句柄。
     */
    virtual BindingGroupHandle createBindingGroup(const BindingGroupDesc &description) = 0;
    /**
     * @brief 创建 GPU 时间戳查询池。
     * @param count 池内可记录时间戳的正条目数。
     * @return 新的时间戳查询池句柄。
     */
    virtual QueryHandle createTimestampQueries(uint32_t count) = 0;
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param buffer 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    virtual void destroy(BufferHandle buffer) = 0;
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param texture 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    virtual void destroy(TextureHandle texture) = 0;
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param sampler 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    virtual void destroy(SamplerHandle sampler) = 0;
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param pipeline 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    virtual void destroy(PipelineHandle pipeline) = 0;
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param group 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    virtual void destroy(BindingGroupHandle group) = 0;
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param queries 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    virtual void destroy(QueryHandle queries) = 0;
    /**
     * @brief 取得 Upload 缓冲的持久映射地址；写入前需等待该范围的 GPU 使用完成。
     * @param buffer 以 Memory::Upload 创建的缓冲。
     * @return 缓冲首字节地址，在缓冲存活且设备有效期间保持有效。
     */
    virtual void *map(BufferHandle buffer) = 0;
    /**
     * @brief 将 CPU 字节数据更新到缓冲范围。
     * @param buffer 目标缓冲。
     * @param offset 目标起始字节偏移。
     * @param data 待复制的数据，范围不能超出缓冲容量。
     * @return 无。
     */
    virtual void upload(BufferHandle buffer, size_t offset, std::span<const std::byte> data) = 0;
    /**
     * @brief 上传指定 mip 的完整 RGBA8 图像。
     * @param texture Rgba8 或 Rgba8Srgb 目标纹理。
     * @param rgba8 左下原点、紧密排列的 RGBA8 像素，大小须匹配 mip 尺寸。
     * @param mipLevel 相对于纹理或视图的 mip 下标。
     * @return 无。
     */
    virtual void upload(TextureHandle texture, std::span<const uint8_t> rgba8, uint32_t mipLevel,
                        uint32_t layer) = 0;
    /**
     * @brief 由基准层内容生成其余 mip 层。
     * @param texture 已分配多个 mip 且已初始化基准层的纹理。
     * @return 无。
     */
    virtual void generateMipmaps(TextureHandle texture) = 0;
    /**
     * @brief 同步回读缓冲中的字节范围。
     * @param buffer 待回读缓冲。
     * @param bytes 字节数，0 表示从 offset 读至末尾。
     * @param offset 源起始字节偏移。
     * @return 拥有回读内容的字节数组。
     */
    virtual std::vector<std::byte> readBuffer(BufferHandle buffer, size_t bytes, size_t offset) = 0;
    /**
     * @brief 同步回读纹理基准层，转换为 RGBA8。
     * @param texture 要回读的有效纹理或视图。
     * @return 左上原点的 RGBA8 图像。
     */
    virtual RgbaImage readTexture(TextureHandle texture, uint32_t layer) = 0;
    /**
     * @brief 在设备线程验证并回放命令，GPU 可异步执行。
     * @param commands 按顺序回放的已验证命令记录，引用的资源必须仍有效。
     * @return 设备专属的完成标记，返回时不保证 GPU 已完成。
     */
    virtual CompletionToken submit(const std::vector<rhi::detail::Command> &commands) = 0;
    /**
     * @brief 非阻塞检查一次提交是否完成。
     * @param token 由当前设备 submit 返回的完成标记。
     * @return 对应 GPU 工作已完成时为 true。
     */
    virtual bool complete(CompletionToken token) = 0;
    /**
     * @brief 等待指定提交完成，并处理可安全回收的资源。
     * @param token 由当前设备 submit 返回的完成标记。
     * @return 无。
     */
    virtual void wait(CompletionToken token) = 0;
    /**
     * @brief 等待此前所有 GPU 工作完成并回收延迟销毁资源。
     * @return 无。
     */
    virtual void waitIdle() = 0;
    /**
     * @brief 查询已记录的 GPU 时间戳。
     * @param queries 时间戳池。
     * @param index 池内条目下标。
     * @param wait true 时阻塞等待；false 时未就绪立即返回空值。
     * @return 以纳秒为单位的 GPU 时间戳，未就绪时为空；差值才是耗时。
     */
    virtual std::optional<uint64_t> timestampResult(QueryHandle queries, uint32_t index,
                                                    bool wait) = 0;
    /**
     * @brief 查询因 GPU 尚在使用而暂缓销毁的对象数量。
     * @return 待回收的原生资源对象数。
     */
    virtual size_t pendingDeletions() const = 0;
    /**
     * @brief 读取后端累计的调试错误数量。
     * @return 累计错误计数。
     */
    virtual uint32_t debugErrorCount() const = 0;
    /**
     * @brief 从当前线程解绑图形上下文，用于线程间移交。
     * @return 无。
     */
    virtual void releaseContext() = 0;
    /**
     * @brief 在当前线程接管图形上下文。
     * @return 无。
     */
    virtual void acquireContext() = 0;
    /**
     * @brief 在拥有上下文的线程交换窗口缓冲。
     * @return 无。
     */
    virtual void swap() = 0;
    /**
     * @brief 取得共享原生窗口状态以控制生命周期。
     * @return 底层窗口状态的共享引用。
     */
    virtual std::shared_ptr<renderer::detail::WindowState> windowState() const = 0;
};
} // namespace renderer::rhi::detail
namespace renderer::rhi {
/**
 * @brief 共享持有具体 RHI 后端的设备私有实现。
 */
struct Device::Impl {
    std::shared_ptr<detail::DeviceBackend> backend;
};
} // namespace renderer::rhi
namespace renderer::detail {
/**
 * @brief 供渲染图等内部模块访问或共享设备后端的友元桥。
 */
struct RhiAccess {
    /**
     * @brief 取得设备包装对象对应的后端。
     * @param d 有效设备。
     * @return 由设备持有的后端引用。
     */
    static rhi::detail::DeviceBackend &backend(rhi::Device &d) {
        return *d.impl_->backend;
    }
    /**
     * @brief 共享设备后端，使编译图不依赖包装对象地址。
     * @param d 有效设备。
     * @return 后端的共享所有权。
     */
    static std::shared_ptr<rhi::detail::DeviceBackend> share(rhi::Device &d) {
        return d.impl_->backend;
    }
};
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_RHI_BACKEND_HPP_INCLUDED
