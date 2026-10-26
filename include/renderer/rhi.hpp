/**
 * @file
 * @brief 声明与后端无关的 GPU 资源、管线、命令录制、同步及设备接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_RHI_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_RHI_HPP_INCLUDED
#include "renderer/handle.hpp"
#include "renderer/math.hpp"
#include "renderer/shader.hpp"
#include "renderer/texture_region.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace renderer {
class Window;
namespace detail {
struct RhiAccess;
}
} // namespace renderer
namespace renderer::rhi {
struct BufferTag;
struct TextureTag;
struct SamplerTag;
struct PipelineTag;
struct GroupTag;
struct QueryTag;
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;
using PipelineHandle = Handle<PipelineTag>;
using BindingGroupHandle = Handle<GroupTag>;
using QueryHandle = Handle<QueryTag>;
/** @brief RHI 纹理的颜色、浮点或深度存储格式。 */
enum class Format {
    Rgba8,
    Rgba8Srgb,
    Rgba16Float,
    Rgba32Float,
    R32Float,
    Depth32Float,
    Bc1,
    Bc1Srgb,
    Bc2,
    Bc2Srgb,
    Bc3,
    Bc3Srgb,
    Bc4,
    Bc5,
    Bc7,
    Bc7Srgb,
    R8
};
// Upload 用于 CPU 写入的持久映射缓冲；Readback 用于诊断回读，Device 由 GPU 使用。
/** @brief GPU 专用、CPU 上传或诊断回读的缓冲内存使用方式。 */
enum class Memory { Device, Upload, Readback };
/** @brief RHI 采样器的最近点或线性过滤方式。 */
enum class Filter { Nearest, Linear };
/** @brief RHI 采样器的重复、钳制、镜像或边框寻址方式。 */
enum class AddressMode { Repeat, Clamp, Mirror, Border };
/** @brief 深度测试或比较采样所用的比较运算。 */
enum class Compare { Never, Less, LessEqual, Equal, Greater, Always };
/** @brief 关闭、背面或正面的三角形剔除方式。 */
enum class CullMode { None, Back, Front };
/** @brief 未指定逐附件混合时使用的不透明、Alpha 或加法预设。 */
enum class BlendMode { Opaque, Alpha, Additive };
/** @brief 附件颜色与 Alpha 加法混合所使用的源/目标因子。 */
enum class BlendFactor {
    Zero,
    One,
    SourceColor,
    OneMinusSourceColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationColor,
    OneMinusDestinationColor,
    DestinationAlpha,
    OneMinusDestinationAlpha
};
// 每个颜色附件独立控制加法混合；颜色和 Alpha 可使用不同因子。
/**
 * @brief 单个颜色附件的独立加法混合配置，颜色与 Alpha 分别指定因子。
 */
struct ColorBlendState {
    bool enabled = false;
    BlendFactor sourceColor = BlendFactor::One, destinationColor = BlendFactor::Zero;
    BlendFactor sourceAlpha = BlendFactor::One, destinationAlpha = BlendFactor::Zero;
};
/** @brief 渲染作用域开始时保留、清除或丢弃附件内容的操作。 */
enum class LoadOp { Load, Clear, DontCare };
/** @brief 渲染作用域结束时保留或丢弃附件内容的操作。 */
enum class StoreOp { Store, DontCare };
// 描述访问发生的流水线阶段，与 Access 一起保留跨后端同步语义。
/** @brief 可按位组合的流水线阶段，用于声明资源访问及同步。 */
enum class Stage : uint32_t {
    None = 0,
    Vertex = 1,
    Fragment = 2,
    Compute = 4,
    Transfer = 8,
    Indirect = 16,
    Attachment = 32,
    All = 0xFFFFFFFFu
};
/**
 * @brief 合并流水线阶段标记。
 * @param a 第一组阶段。
 * @param b 第二组阶段。
 * @return 两组阶段的按位或。
 */
constexpr Stage operator|(Stage a, Stage b) {
    return static_cast<Stage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
/** @brief 资源在不同流水线用途下的读写或呈现访问状态。 */
enum class Access {
    None,
    VertexRead,
    IndexRead,
    UniformRead,
    SampledRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    IndirectRead,
    TransferRead,
    TransferWrite,
    ColorWrite,
    DepthWrite,
    DepthRead,
    Present
};
/**
 * @brief GPU 缓冲字节容量、内存类型及调试名称。
 */
struct BufferDesc {
    size_t size = 0;
    Memory memory = Memory::Device;
    std::string name;
};
/**
 * @brief GPU 二维纹理的尺寸、像素格式、mip 数及调试名称。
 */
struct TextureDesc {
    uint32_t width = 1, height = 1, mipLevels = 1;
    Format format = Format::Rgba8;
    std::string name;
    uint32_t layers = 1;       ///< 数组层数量，必须不超过设备上限。
    bool arrayTexture = false; ///< 单层也强制数组类型；layers>1 自动为数组。
};
/**
 * @brief 不可变过滤、寻址、mip、边框颜色与深度比较采样配置。
 */
struct SamplerDesc {
    Filter minFilter = Filter::Linear, magFilter = Filter::Linear;
    AddressMode addressU = AddressMode::Repeat, addressV = AddressMode::Repeat;
    bool mipmapped = true, compare = false;
    Compare compareFunction = Compare::LessEqual;
    Vec4 borderColor{1, 1, 1, 1};
    Filter mipFilter = Filter::Linear;
};
/**
 * @brief 顶点属性位置、流绑定、浮点分量数及流内字节偏移。
 */
struct VertexAttribute {
    uint32_t location = 0, binding = 0, components = 3, offset = 0;
};
/**
 * @brief 顶点流绑定编号、字节步长及逐实例推进配置。
 */
struct VertexBinding {
    uint32_t binding = 0, stride = 0;
    bool instanced = false;
};
/**
 * @brief 图形着色器、顶点布局、附件格式及固定光栅/深度/混合状态。
 */
struct GraphicsPipelineDesc {
    ShaderArtifact vertex, fragment;
    std::vector<VertexAttribute> attributes;
    std::vector<VertexBinding> vertexBindings;
    std::vector<Format> colorFormats{Format::Rgba8};
    std::optional<Format> depthFormat;
    // 光栅、深度及混合状态在创建后固定；改变状态需选择另一条 Pipeline。
    bool depthTest = true, depthWrite = true, clockwise = false;
    Compare depthCompare = Compare::Less;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Opaque;
    float depthBiasConstant = 0, depthBiasSlope = 0;
    std::string name;
    // 为空时沿用 blend；非空时必须与 colorFormats 等长，并逐附件覆盖混合状态。
    std::vector<ColorBlendState> colorBlends;
};
/**
 * @brief 一项着色器逻辑资源绑定及缓冲或纹理子范围。
 */
struct Binding {
    uint32_t binding = 0;
    BindingKind kind = BindingKind::UniformBuffer;
    BufferHandle buffer;
    size_t offset = 0, size = 0; // 缓冲范围；size 为 0 表示从 offset 到缓冲末尾。
    TextureHandle texture;
    SamplerHandle sampler;
    uint32_t mipLevel = 0;
    bool writable = false;
};
/**
 * @brief 一个逻辑 set 的全部资源绑定条目。
 */
struct BindingGroupDesc {
    uint32_t group = 0; // 对应着色器 layout(set = N)，不暴露 OpenGL 的物理绑定点。
    std::vector<Binding> entries;
};
/**
 * @brief 颜色附件纹理、mip、加载/保存操作及清除色。
 */
struct ColorAttachment {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    Vec4 clearColor{};
    uint32_t mipLevel = 0;
};
/**
 * @brief 深度附件纹理、mip、加载/保存操作及清除值。
 */
struct DepthAttachment {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    float clearDepth = 1;
    uint32_t mipLevel = 0;
};
/**
 * @brief 单个动态渲染作用域的颜色与可选深度附件集合。
 */
struct RenderingDesc {
    std::vector<ColorAttachment> colors;
    std::optional<DepthAttachment> depth;
};
// 显式资源访问转换；保留资源身份及 mip/字节范围，由具体后端映射同步操作。
// 纹理设置 texture/image，缓冲设置 buffer；数量为 0 表示从起点到资源末尾。
/**
 * @brief 携带资源身份、范围、阶段及前后访问方式的显式同步声明。
 */
struct ResourceBarrier {
    Access before = Access::None, after = Access::None;
    bool texture = false;
    BufferHandle buffer;
    TextureHandle image;
    Stage sourceStages = Stage::All, destinationStages = Stage::All;
    uint32_t firstMip = 0, mipCount = 0;
    size_t offset = 0, size = 0;
};
/**
 * @brief 二维纹理拷贝的源/目标 mip、左下起点和像素尺寸。
 */
struct TextureCopy {
    uint32_t sourceMip = 0, destinationMip = 0, sourceX = 0, sourceY = 0, destinationX = 0,
             destinationY = 0, width = 0, height = 0;
    uint32_t sourceLayer = 0, destinationLayer = 0, layerCount = 1; ///< 相对于视图的数组层范围。
};
// 设备范围内单调递增的提交标记，不能拿到另一台设备上查询或等待。
/**
 * @brief 设备专属、单调递增的提交完成标记，不能跨设备查询。
 */
struct CompletionToken {
    uint64_t owner = 0, value = 0;
    /**
     * @brief 检查完成标记是否非空，不检查 GPU 完成状态。
     * @return owner 非零时为 true。
     */
    explicit operator bool() const {
        return owner != 0;
    }
};
/** @brief 设备累计上传统计；提交/等待仅指资源上传，不含普通绘制与主动回读。 */
struct UploadStatistics {
    uint64_t calls = 0, bytes = 0, submissions = 0, waits = 0;
    uint64_t stagingAllocations = 0, stagingReuses = 0, pooledStagingBytes = 0;
};
/** @brief 提交对象缓存和分配器统计；未实现此诊断的后端返回 available=false 与零值。
 * 原生内存统计包含资源对齐及空闲块，不等同于资源像素载荷或驱动实际驻留显存。
 */
struct DeviceStatistics {
    bool available = false;
    uint64_t submissionContextsCreated = 0, submissionContextsReused = 0;
    uint64_t cachedSubmissionContexts = 0, pendingSubmissions = 0;
    uint64_t descriptorPoolsCreated = 0, descriptorPoolsReused = 0;
    uint64_t cachedDescriptorPools = 0; ///< 仅已完成、可复用上下文持有的描述符池数量。
    uint64_t memoryBlocks = 0, memoryAllocations = 0;
    uint64_t memoryBlockBytes = 0;      ///< 分配器向 Vulkan 申请的块字节，包含空闲范围。
    uint64_t memoryAllocationBytes = 0; ///< 当前存活资源的分配字节，包含资源对齐。
};
/**
 * @brief 实际后端标识、资源限制、对齐要求及可用功能。
 */
struct DeviceCaps {
    std::string backend, vendor, device, version;
    uint32_t maxTextureSize = 0, maxColorAttachments = 0, maxVertexAttributes = 0;
    uint32_t maxVertexStride = 0;
    uint32_t maxSampledTextures = 0, maxUniformBindings = 0, maxStorageBindings = 0,
             maxImageBindings = 0;
    uint32_t uniformAlignment = 1, storageAlignment = 1, maxComputeInvocations = 0;
    uint32_t maxTextureArrayLayers = 256; ///< 最大二维纹理数组层数。
    bool compute = true, indirectDraw = true, timestamps = true, persistentMapping = true;
};
/**
 * @brief 一个纹理格式可用于采样、颜色/深度附件或存储图像的支持标记。
 */
struct FormatFeatures {
    bool sampled = false, colorAttachment = false, depthAttachment = false, storageImage = false;
};
/**
 * @brief 左上原点的 RGBA8 纹理回读结果。
 */
struct RgbaImage {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
class CommandEncoder;
// finish 后不可变、可复制；命令持有句柄，不延长未提交资源的有效期。
// 必须保证录制用到的资源在提交时仍然有效。
/**
 * @brief 共享不可变 PImpl 的可复制命令列表；所引用资源须至少保持有效至提交。
 */
class CommandList {
  public:
    /**
     * @brief 创建空命令列表。
     * @return 无（构造函数）。
     */
    CommandList();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~CommandList();
    /**
     * @brief 共享复制构造，共享不可变内部数据。
     * @param other 待复制的同类型对象。
     * @return 无（构造函数）。
     */
    CommandList(const CommandList &other);
    /**
     * @brief 共享复制赋值，共享不可变内部数据。
     * @param other 待复制的同类型对象。
     * @return 当前对象的引用。
     */
    CommandList &operator=(const CommandList &other);
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    CommandList(CommandList &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    CommandList &operator=(CommandList &&other) noexcept;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class CommandEncoder;
    friend class Device;
};
// 只在 CPU 录制，可在工作线程使用各自独立的编码器；同一编码器不并发修改。
// begin/endRendering 必须配对，绘制在渲染作用域内，计算与拷贝在作用域外。
/**
 * @brief 独占 PImpl 的 CPU 命令录制器，可在工作线程使用独立实例，不并发修改同一实例。
 */
class CommandEncoder {
  public:
    /**
     * @brief 创建空命令编码器。
     * @return 无（构造函数）。
     */
    CommandEncoder();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~CommandEncoder();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    CommandEncoder(CommandEncoder &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    CommandEncoder &operator=(CommandEncoder &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    CommandEncoder(const CommandEncoder &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    CommandEncoder &operator=(const CommandEncoder &other) = delete;
    /**
     * @brief 开始渲染作用域，选择颜色和深度附件。
     * @param description 附件、mip、加载/保存操作及清除值。
     * @return 无。
     */
    void beginRendering(const RenderingDesc &description);
    /**
     * @brief 结束当前渲染作用域，与 beginRendering 配对。
     * @return 无。
     */
    void endRendering();
    /**
     * @brief 选择后续绘制或计算使用的不可变管线。
     * @param pipeline 提交设备的有效图形或计算管线。
     * @return 无。
     */
    void bindPipeline(PipelineHandle pipeline);
    /**
     * @brief 绑定一组逻辑着色器资源。
     * @param group 与当前管线布局兼容的绑定组。
     * @return 无。
     */
    void bindGroup(BindingGroupHandle group);
    /**
     * @brief 绑定顶点输入缓冲及起始范围。
     * @param binding 管线声明的顶点流绑定编号。
     * @param buffer 有效顶点缓冲。
     * @param offset 首个顶点数据的字节偏移。
     * @return 无。
     */
    void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset = 0);
    /**
     * @brief 绑定 32 位无符号索引缓冲。
     * @param buffer 包含 uint32 索引的有效缓冲。
     * @param offset 索引数据的起始字节偏移，按 4 字节对齐。
     * @return 无。
     */
    void bindIndexBuffer(BufferHandle buffer, size_t offset = 0);
    /**
     * @brief 设置从原点开始的动态视口。
     * @param width 视口像素宽度。
     * @param height 视口像素高度。
     * @return 无。
     */
    void viewport(uint32_t width, uint32_t height);
    /**
     * @brief 设置动态裁剪矩形。
     * @param x 左下角 X 像素坐标。
     * @param y 左下角 Y 像素坐标。
     * @param width 矩形像素宽度。
     * @param height 矩形像素高度。
     * @return 无。
     */
    void scissor(int x, int y, uint32_t width, uint32_t height);
    /**
     * @brief 在渲染作用域内录制非索引三角形绘制。
     * @param vertexCount 每个实例的顶点数量。
     * @param instanceCount 实例数量。
     * @param firstVertex 首个顶点下标。
     * @param firstInstance 首个实例编号。
     * @return 无。
     */
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
              uint32_t firstInstance = 0);
    /**
     * @brief 在渲染作用域内录制索引三角形绘制。
     * @param indexCount 每个实例使用的索引数量。
     * @param instanceCount 实例数量。
     * @param firstIndex 相对于已绑定索引范围的首个索引下标。
     * @param baseVertex 加到每个索引值上的顶点偏移。
     * @param firstInstance 首个实例编号。
     * @return 无。
     */
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                     int32_t baseVertex = 0, uint32_t firstInstance = 0);
    /**
     * @brief 从缓冲读取标准五字段索引间接绘制参数。
     * @param buffer 间接参数缓冲。
     * @param offset 首条参数的字节偏移，须按 4 字节对齐。
     * @param drawCount 绘制参数条目数量。
     * @param stride 条目间字节跨度，须至少 20 且为 4 的倍数；20 表示紧密排列。
     * @return 无。
     */
    void drawIndexedIndirect(BufferHandle buffer, size_t offset, uint32_t drawCount = 1,
                             uint32_t stride = 20);
    /**
     * @brief 在渲染作用域外录制计算工作组调度。
     * @param x X 方向工作组数量。
     * @param y Y 方向工作组数量。
     * @param z Z 方向工作组数量。
     * @return 无。
     */
    void dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);
    /**
     * @brief 从缓冲中的三个 uint32 读取计算工作组数量。
     * @param buffer 间接调度参数缓冲。
     * @param offset 参数字节偏移，须按 4 字节对齐。
     * @return 无。
     */
    void dispatchIndirect(BufferHandle buffer, size_t offset = 0);
    /**
     * @brief 在渲染作用域外录制缓冲区域拷贝。
     * @param source 源缓冲。
     * @param destination 目标缓冲。
     * @param bytes 拷贝的字节数。
     * @param sourceOffset 源起始字节偏移。
     * @param destinationOffset 目标起始字节偏移。
     * @return 无。
     */
    void copyBuffer(BufferHandle source, BufferHandle destination, size_t bytes,
                    size_t sourceOffset = 0, size_t destinationOffset = 0);
    /**
     * @brief 在渲染作用域外录制兼容格式纹理之间的区域拷贝。
     * @param source 源纹理。
     * @param destination 目标纹理。
     * @param region 源/目标 mip、起点和拷贝像素尺寸。
     * @return 无。
     */
    void copyTexture(TextureHandle source, TextureHandle destination, const TextureCopy &region);
    /**
     * @brief 录制显式资源访问转换及相应同步。
     * @param barriers 资源身份、前后访问方式、阶段及子范围列表，录制时复制。
     * @return 无。
     */
    void barrier(std::span<const ResourceBarrier> barriers);
    /**
     * @brief 在命令流中记录 GPU 时间戳。
     * @param queries 已创建的时间戳查询池。
     * @param index 池内查询下标。
     * @return 无。
     */
    void timestamp(QueryHandle queries, uint32_t index);
    /**
     * @brief 将纹理拷贝到窗口默认帧缓冲；窗口交换仍需单独调用。
     * @param texture 要显示的颜色纹理。
     * @return 无。
     */
    void present(TextureHandle texture);
    /**
     * @brief 结束录制并转移命令；渲染作用域必须已结束，编码器不能复用。
     * @return 可复制的不可变命令列表。
     */
    CommandList finish(); // 结束本次录制并转移命令；编码器不能再次录制。

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// 资源创建、更新、提交、查询及销毁均在拥有上下文的设备线程执行。
// 公共 PImpl 不包含任何原生 API 类型；一个窗口只绑定一个 Device/Renderer。
/**
 * @brief 以 PImpl 隐藏图形 API 的设备服务，资源操作与提交均在持有上下文的线程执行。
 */
class Device {
  public:
    /** @brief 开始合并资源传输，CPU 数据在 upload 返回前复制；不可嵌套。@return 无。 */
    void beginUploadBatch();
    /** @brief 提交当前上传批次，GPU 可异步执行，后续同设备命令自动有序。
     * @return 完成标记，空批次可为空；提交绘制、waitIdle 或回读会先发布传输，仍需配对结束批次。 */
    CompletionToken endUploadBatch();
    /** @brief 查询设备累计上传与暂存池统计。@return 独立统计副本，须在设备线程调用。 */
    UploadStatistics uploadStatistics() const;
    /** @brief 查询提交复用与原生内存分配统计，不隐式等待 GPU。
     * @return 统计副本；available=false 表示后端不提供此诊断。须在设备线程调用。
     */
    DeviceStatistics statistics() const;
    /** @brief 更新未压缩纹理的局部像素，可与当前传输批次合并。
     * @param texture RGBA8/R8 纹理或视图。@param region 当前 mip 的像素矩形。
     * @param pixels 紧密区域载荷，调用期间复制。@param mipLevel 相对 mip。@param layer 相对数组层。
     * @return 无；写入与后续同设备命令有序。 */
    void uploadRegion(TextureHandle texture, TextureRegion region, std::span<const uint8_t> pixels,
                      uint32_t mipLevel = 0, uint32_t layer = 0);
    /** @brief 将纹理区域作为资源传输拷贝，可与当前批次合并。
     * @param source 源纹理。@param destination 目标纹理。@param region 格式一致且不重叠的区域。
     * @return 无；不同于 CommandEncoder，此操作直接在设备线程记录传输。 */
    void copyTexture(TextureHandle source, TextureHandle destination, const TextureCopy &region);
    /**
     * @brief 创建占有窗口上下文的 OpenGL 设备。
     * @param window 有效且未绑定其他 Device/Renderer 的窗口。
     * @param debug 是否启用后端调试回调。
     * @return 可移动的设备对象。
     */
    static Device createOpenGL(Window &window, bool debug = true);
    /**
     * @brief 创建 Vulkan 1.3 设备、窗口表面及交换链，公共接口不暴露原生类型。
     * @param window graphicsApi=Vulkan 且尚未绑定设备的窗口。
     * @param debug 有校验层时启用校验及调试信息收集。
     * @return 可移动的设备；不满足设备能力或初始化失败时抛出异常。
     */
    static Device createVulkan(Window &window, bool debug = true);
    /** @brief 创建无应用窗口的 OpenGL 设备。
     * @param debug 是否启用调试回调。
     * @return 设备；目前仅 Windows WGL pbuffer，有短暂引导窗口，驱动可能保留内部隐藏 HWND。
     */
    static Device createOpenGLOffscreen(bool debug = true);
    /** @brief 创建无窗口/表面/交换链的 Vulkan 设备。
     * @param debug 是否启用可用校验层。
     * @return 设备，可用普通纹理和 RenderGraph 渲染，不依赖显示服务。
     */
    static Device createVulkanOffscreen(bool debug = true);
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~Device();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    Device(Device &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    Device &operator=(Device &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Device(const Device &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Device &operator=(const Device &other) = delete;
    /**
     * @brief 查询后端能力与绑定限制。
     * @return 设备信息、资源上限、对齐要求及功能支持。
     */
    DeviceCaps capabilities() const;
    /**
     * @brief 查询指定纹理格式支持的用途。
     * @param format 需要查询的纹理格式。
     * @return 采样、颜色附件、深度附件及存储图像支持标记。
     */
    FormatFeatures formatFeatures(Format format) const;
    /**
     * @brief 创建 GPU 缓冲并可选初始化前部数据。
     * @param description 正字节数、内存类型和调试名称。
     * @param initial 不超过缓冲容量的初始字节数据，调用期间复制。
     * @return 当前设备的新缓冲句柄。
     */
    BufferHandle createBuffer(const BufferDesc &description,
                              std::span<const std::byte> initial = {});
    /**
     * @brief 按尺寸、格式、层数与 mip 数分配纹理存储。
     * @param description 有效的二维或二维数组描述；压缩格式需设备支持。
     * @return 当前设备的新纹理句柄；内容需后续上传或写入。
     */
    TextureHandle createTexture(const TextureDesc &description);
    // 视图共享底层存储；销毁原纹理句柄不影响仍然存活的视图。
    /**
     * @brief 创建共享底层存储的 mip/数组层视图；销毁原句柄不影响存活视图。
     * @param texture 同一设备的源纹理或视图。
     * @param firstMip 相对于源纹理/视图的首个 mip。
     * @param mipCount 视图包含的正 mip 层数。
     * @param firstLayer 相对于源纹理/视图的首个数组层。
     * @param layerCount 正层数，0 表示剩余全部层。
     * @param arrayView true 强制数组维度，即使仅有一层；否则单层视图为二维。
     * @return 独立管理生命周期的新纹理视图句柄。
     */
    TextureHandle createTextureView(TextureHandle texture, uint32_t firstMip, uint32_t mipCount,
                                    uint32_t firstLayer = 0, uint32_t layerCount = 0,
                                    bool arrayView = false);
    /**
     * @brief 创建不可变纹理采样器。
     * @param description 过滤、寻址、mip 与深度比较设置。
     * @return 当前设备的新采样器句柄。
     */
    SamplerHandle createSampler(const SamplerDesc &description = {});
    /**
     * @brief 创建包含着色器、顶点格式、附件格式和固定状态的图形管线。
     * @param description 有效的图形管线配置，着色器接口须匹配。
     * @return 新图形管线句柄；创建或链接失败抛出异常。
     */
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &description);
    /**
     * @brief 从计算着色器产物创建计算管线。
     * @param shader Compute 阶段的有效 SPIR-V 及反射信息。
     * @param name 可选调试名称。
     * @return 新计算管线句柄。
     */
    PipelineHandle createComputePipeline(const ShaderArtifact &shader,
                                         const std::string &name = {});
    /**
     * @brief 创建一个逻辑描述符组。
     * @param description 逻辑 set 编号及缓冲、纹理、采样器和图像条目。
     * @return 新的绑定组句柄。
     */
    BindingGroupHandle createBindingGroup(const BindingGroupDesc &description);
    /**
     * @brief 创建 GPU 时间戳查询池。
     * @param count 池内可记录时间戳的正条目数。
     * @return 新的时间戳查询池句柄。
     */
    QueryHandle createTimestampQueries(uint32_t count);
    // 句柄立即失效；已提交命令需要的原生对象延迟到 GPU 完成后回收。
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param buffer 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    void destroy(BufferHandle buffer);
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param texture 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    void destroy(TextureHandle texture);
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param sampler 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    void destroy(SamplerHandle sampler);
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param pipeline 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    void destroy(PipelineHandle pipeline);
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param group 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    void destroy(BindingGroupHandle group);
    /**
     * @brief 立即使句柄失效，底层资源延迟到 GPU 使用完成后回收。
     * @param queries 当前设备中仍有效的待销毁资源。
     * @return 无。
     */
    void destroy(QueryHandle queries);
    // 仅 Upload 缓冲可持久映射；CPU 写入前需等待引用该范围的 GPU 工作完成。
    /**
     * @brief 取得 Upload 缓冲的持久映射地址；写入前需等待该范围的 GPU 使用完成。
     * @param buffer 以 Memory::Upload 创建的缓冲。
     * @return 缓冲首字节地址，在缓冲存活且设备有效期间保持有效。
     */
    void *map(BufferHandle buffer);
    /**
     * @brief 将 CPU 字节数据更新到缓冲范围。
     * @param buffer 目标缓冲。
     * @param offset 目标起始字节偏移。
     * @param data 待复制的数据，范围不能超出缓冲容量。
     * @return 无。
     */
    void upload(BufferHandle buffer, size_t offset, std::span<const std::byte> data);
    /**
     * @brief 上传指定数组层/mip 的完整图像或压缩块载荷。
     * @param texture RGBA8 或 BC 格式的目标纹理/视图。
     * @param rgba8 紧密 RGBA8 或 BC 块行，长度须匹配 mip；RHI 不转换行序。
     * @param mipLevel 相对于纹理或视图的 mip 下标。
     * @param layer 相对于纹理或视图的数组层下标。
     * @return 无。
     */
    void upload(TextureHandle texture, std::span<const uint8_t> rgba8, uint32_t mipLevel = 0,
                uint32_t layer = 0);
    /**
     * @brief 由基准层内容生成其余 mip 层。
     * @param texture 已初始化所有数组基准层的未压缩纹理；BC 需离线提供 mip。
     * @return 无。
     */
    void generateMipmaps(TextureHandle texture);
    /**
     * @brief 同步回读缓冲中的字节范围。
     * @param buffer 待回读缓冲。
     * @param bytes 字节数，0 表示从 offset 读至末尾。
     * @param offset 源起始字节偏移。
     * @return 拥有回读内容的字节数组。
     */
    std::vector<std::byte> readBuffer(BufferHandle buffer, size_t bytes = 0, size_t offset = 0);
    /**
     * @brief 同步回读纹理基准层，转换为 RGBA8。
     * @param texture 要回读的有效纹理或视图。
     * @param layer 相对于纹理或视图的数组层下标。
     * @return 左上原点的 RGBA8 图像。
     */
    RgbaImage readTexture(TextureHandle texture, uint32_t layer = 0);
    // 在设备线程验证并回放命令，返回完成标记；返回时 GPU 可能仍在执行。
    /**
     * @brief 在设备线程验证并回放命令，GPU 可异步执行。
     * @param commands 已 finish 的不可变命令列表，所引用资源须仍有效。
     * @return 设备专属的完成标记，返回时不保证 GPU 已完成。
     */
    CompletionToken submit(const CommandList &commands);
    /**
     * @brief 非阻塞检查一次提交是否完成。
     * @param token 由当前设备 submit 返回的完成标记。
     * @return 对应 GPU 工作已完成时为 true。
     */
    bool complete(CompletionToken token);
    /**
     * @brief 等待指定提交完成，并处理可安全回收的资源。
     * @param token 由当前设备 submit 返回的完成标记。
     * @return 无。
     */
    void wait(CompletionToken token);
    /**
     * @brief 等待此前所有 GPU 工作完成并回收延迟销毁资源。
     * @return 无。
     */
    void waitIdle();
    // 时间戳单位为纳秒；wait=false 时结果未就绪返回空值，避免强制同步。
    /**
     * @brief 查询已记录的 GPU 时间戳。
     * @param queries 时间戳池。
     * @param index 池内条目下标。
     * @param wait true 时阻塞等待；false 时未就绪立即返回空值。
     * @return 以纳秒为单位的 GPU 时间戳，未就绪时为空；差值才是耗时。
     */
    std::optional<uint64_t> timestampResult(QueryHandle queries, uint32_t index, bool wait = false);
    /**
     * @brief 查询因 GPU 尚在使用而暂缓销毁的对象数量。
     * @return 待回收的原生资源对象数。
     */
    size_t pendingDeletions() const;
    /**
     * @brief 读取后端累计的调试错误数量。
     * @return 累计错误计数。
     */
    uint32_t debugErrorCount() const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    /**
     * @brief 由工厂接管设备实现。
     * @param impl 包含后端的独占实现状态。
     * @return 无（构造函数）。
     */
    explicit Device(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend struct renderer::detail::RhiAccess;
};
} // namespace renderer::rhi

#endif // RENDERER_INCLUDE_RENDERER_RHI_HPP_INCLUDED
