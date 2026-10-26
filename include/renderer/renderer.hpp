/**
 * @file
 * @brief 声明高层渲染器、帧设置、透明与抗锯齿模式、统计和图像回读接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_RENDERER_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_RENDERER_HPP_INCLUDED
#include "renderer/overlay.hpp"
#include "renderer/render_snapshot.hpp"
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace renderer {
class Window;

/**
 * @brief 帧槽容量、调试、独立渲染线程及队列背压配置。
 */
struct RendererConfig {
    uint32_t framesInFlight = 3; // GPU 帧槽数量，槽位复用前等待其完成标记。
    uint32_t initialInstanceCapacity = 4096;
    bool debug = true;
    bool renderThread = false;    // 将上下文与 GPU 操作交给专用线程。
    uint32_t maxQueuedFrames = 2; // 待执行任务队列上限，满时 submit 会等待空位。
    uint64_t assetUploadBudgetBytes =
        16ull * 1024 * 1024; ///< 每帧新资源上传预算；0 不限，单个超大资源允许独占一帧。
    bool forceTextureDecompression = false; ///< 调试：强制 BC 解码后上传，便于对照画质/内存。
    bool textureCompressionFallback = true; ///< 不支持 BC 时解码 RGBA8；false 明确报错。
    bool incrementalInstanceUploads = true; ///< 各帧槽保留实例副本，只上传发生变化的连续范围。
    bool batchResourceUploads = true;       ///< 合并同帧资源传输；false 用于同步上传性能对照。
};
/** @brief 独立纹理渲染目标的像素尺寸，与窗口/DPI 无关。 */
struct OffscreenConfig {
    uint32_t width = 1280, height = 720;
};
/** @brief 直通、ACES 或 Reinhard 的 HDR 色调映射方式。 */
enum class ToneMapping { None, ACES, Reinhard };
// 排序 Alpha 保留传统层叠；加权 OIT 不依赖绘制顺序，但颜色合成是近似。
/** @brief 排序 Alpha 或近似的加权混合 OIT 透明绘制方式。 */
enum class TransparencyMode { SortedAlpha, WeightedBlendedOIT };
/** @brief 最终画面的抗锯齿方式；FXAA 在 ToneMap 后执行，不改变几何采样数。 */
enum class AntiAliasing { None, FXAA };
/**
 * @brief 单帧相机选择、清除色、光照、裁剪、阴影和后处理配置。
 */
struct RenderView {
    NodeHandle cameraNode;                      // 必须是本次快照中存在且可见的相机。
    Vec4 clearColor{0.018f, 0.025f, 0.045f, 1}; // 线性 RGB。
    Vec3 lightDirection{-0.4f, -1.0f, -0.3f};   // 无显式灯光时，默认光线的传播方向。
    float ambientLight = 0.25f;
    bool frustumCulling = true;
    bool shadows = true;
    uint32_t shadowResolution = 1024;
    float exposure = 1;
    ToneMapping toneMapping = ToneMapping::None;
    TransparencyMode transparency = TransparencyMode::SortedAlpha;
    AntiAliasing antiAliasing = AntiAliasing::None; ///< 按视图选择，可逐帧切换，默认关闭。
    Vec3 colorMultiply{1, 1, 1}, colorAdd{};        ///< ToneMap 后的线性调色乘数/偏移。
    float saturation = 1;                           ///< 饱和度，[0,4]。
    bool contactShadows = false;        ///< 基于场景不透明/Mask 深度的屏幕空间接触阴影。
    float contactShadowStrength = .6f;  ///< 阴影强度，[0,1]。
    float contactShadowDistance = 1;    ///< 世界空间最大采样距离，必须为正。
    float contactShadowThickness = .1f; ///< 世界空间命中厚度容差，必须为正。
    uint32_t contactShadowSteps = 12;   ///< 射线步数，[1,32]。
    OverlaySnapshot overlay;    ///< ToneMap/FXAA 后合成的不可变屏幕覆盖层，回读/PNG 同样包含它。
    float timeSeconds = 0;      ///< 有限的效果时间，应用提供，单位秒；两个后端一致。
    bool spatialCulling = true; ///< 用不可变 BVH 粗裁剪，关闭时逐项扫描，便于对照。
};
/**
 * @brief 一个渲染 Pass 的 GPU 计时记录，单位为毫秒。
 */
struct PassTiming {
    std::string name;
    double gpuMilliseconds = 0;
};
/**
 * @brief 帧实例、合批、资源、CPU 耗时和可用 GPU 时间戳统计。
 */
struct RenderStats {
    uint32_t sceneInstances = 0;
    uint32_t visibleInstances = 0;
    uint32_t culledInstances = 0;
    uint32_t drawCalls = 0; // 包含场景、阴影、OIT 合成、ToneMap 及可选 FXAA 绘制。
    uint64_t triangles = 0; // 仅场景绘制三角形，不包含阴影及全屏三角形。
    uint64_t uploadedBytes = 0;
    uint64_t gpuMeshes = 0;
    uint64_t gpuTextures = 0;
    uint64_t retiredResources = 0;
    double prepareMilliseconds = 0;
    double submitMilliseconds = 0;
    uint32_t sceneDrawCalls = 0, shadowDrawCalls = 0, transparentDrawCalls = 0;
    uint32_t transparencyResolveDrawCalls = 0;
    double gpuMilliseconds = 0;
    bool gpuTimingValid = false; // GPU 时间来自已完成的旧帧，不一定属于本次提交。
    std::vector<PassTiming> passes;
    uint32_t antiAliasingDrawCalls = 0; ///< 本帧抗锯齿全屏绘制次数，关闭时为 0。
    uint32_t overlayDrawCalls = 0;      ///< 屏幕覆盖层绘制次数，单独计数且计入 drawCalls。
    uint64_t overlayTriangles = 0;      ///< 覆盖层三角形数，不计入场景 triangles。
    uint64_t assetUploadedBytes = 0, instanceUploadedBytes = 0, instanceReusedBytes = 0;
    uint32_t instanceUploadRanges = 0,
             pendingResourceDraws = 0; ///< 未上传完整的批次本帧跳过，后续帧继续准备。
    uint64_t uploadCalls = 0, uploadSubmissions = 0, uploadWaits = 0, stagingAllocations = 0,
             stagingReuses = 0;
};
/** @brief 渲染器拥有的资源有效载荷估算，非驱动实际显存；不含驱动/分配对齐和待回收原生对象。 */
struct RendererMemoryStats {
    uint64_t meshBytes = 0, textureBytes = 0, materialBytes = 0, frameBufferBytes = 0,
             renderTargetBytes = 0;
    uint64_t compressedTextureBytes = 0, textureArrayLayers = 0; ///< 实际 BC 载荷和数组层总数。
    uint64_t pooledUploadBytes = 0; ///< 受管理暂存池字节，可位于系统内存或设备可见内存。
    uint64_t instanceMirrorBytes = 0, cachedMeshes = 0, cachedTextures = 0, cachedMaterials = 0;
    bool nativeMemoryDetailsAvailable = false; ///< 当前仅 Vulkan/VMA 提供下列分配器统计。
    uint64_t nativeMemoryBlocks = 0, nativeMemoryAllocations = 0;
    uint64_t nativeMemoryBlockBytes = 0, nativeMemoryAllocationBytes = 0;
    /** @brief 汇总受管理 GPU 资源有效载荷。@return 估算字节。 */
    uint64_t estimatedGpuBytes() const {
        return meshBytes + textureBytes + materialBytes + frameBufferBytes + renderTargetBytes +
               pooledUploadBytes;
    }
};
/**
 * @brief 实际图形后端、供应商、设备及驱动版本描述。
 */
struct DeviceInfo {
    std::string backend;
    std::string vendor;
    std::string device;
    std::string version;
};
/**
 * @brief 显示编码、左上原点的 RGBA8 输出图像及 PNG/PPM 保存接口。
 */
struct Image {
    uint32_t width = 0, height = 0;
    // 显示编码的 RGBA8，行从左上角开始；与手工上传纹理的行序不同。
    std::vector<uint8_t> pixels;
    /**
     * @brief 将图像保存为 P6 PPM；忽略 Alpha，写入失败抛出异常。
     * @param path 输出文件路径。
     * @return 无。
     */
    void savePpm(const std::string &path) const;
    /** @brief 保存 RGBA PNG，保留 Alpha 与左上行序，自动创建父目录。
     * @param path 本地路径，使用 filesystem::path 可传入 Unicode 路径。
     * @return 无；无效输入、带过滤字节的像素超过约 1 GiB、编码或写文件失败时抛出异常。
     */
    void savePng(const std::filesystem::path &path) const;
};

// 高层渲染入口；创建、使用与析构建议集中在应用主线程。
// PImpl 隐藏队列及后端；同一个窗口只能由一个 Renderer 或低层 Device 持有上下文。
/**
 * @brief 以 PImpl 隐藏后端与队列的高层渲染服务，可选择窗口或独立离屏纹理目标。
 */
class Renderer {
  public:
    /**
     * @brief 为窗口创建高层 OpenGL 渲染器并占有其上下文。
     * @param window 已创建且尚未绑定 Device 或 Renderer 的窗口。
     * @param config 帧槽、调试与可选渲染线程配置。
     * @return 可移动的渲染器对象。
     */
    static Renderer createOpenGL(Window &window, const RendererConfig &config = {});
    /**
     * @brief 为 Vulkan 窗口创建高层渲染器，复用统一场景和渲染管线。
     * @param window graphicsApi=Vulkan 且未被其他设备占用的窗口。
     * @param config 帧槽、调试和专用渲染线程配置。
     * @return 可移动的渲染器；创建失败抛出异常。
     */
    static Renderer createVulkan(Window &window, const RendererConfig &config = {});
    /** @brief 创建独立 OpenGL 纹理渲染器，不需要应用 Window。
     * @param target 每边 [1,16384] 的像素尺寸，还须满足设备纹理上限。
     * @param config 管线与可选渲染线程配置。
     * @return 渲染器；目前仅 Windows WGL pbuffer。初始化有短暂引导窗口，驱动可能保留内部隐藏 HWND。
     */
    static Renderer createOpenGLOffscreen(const OffscreenConfig &target = {},
                                          const RendererConfig &config = {});
    /** @brief 创建 Vulkan 离屏渲染器，全程不创建窗口、表面或交换链。
     * @param target 每边 [1,16384] 的像素尺寸，还须满足设备纹理上限。
     * @param config 管线与可选渲染线程配置。
     * @return 渲染器，只需要 Vulkan 设备和驱动，不需要显示服务。
     */
    static Renderer createVulkanOffscreen(const OffscreenConfig &target = {},
                                          const RendererConfig &config = {});
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~Renderer();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    Renderer(Renderer &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    Renderer &operator=(Renderer &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Renderer(const Renderer &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Renderer &operator=(const Renderer &other) = delete;

    // 等价于 submit(...).get()：等待 CPU 录制/提交结束，不代表 GPU 已执行完毕。
    // 先绘制到自有纹理；窗口模式再拷贝到窗口，交换缓冲需显式调用 present()。
    /**
     * @brief 渲染一帧并等待 CPU 提交完成；GPU 可能仍执行，窗口交换需调用 present。
     * @param snapshot 不可变场景快照。
     * @param view 快照中的相机及裁剪、阴影、透明与后处理设置。
     * @return 本帧 CPU/绘制统计及已完成旧帧的可用 GPU 计时。
     */
    RenderStats render(const RenderSnapshot &snapshot, const RenderView &view);
    // 快照按值持有，允许提交后立即修改场景；任务错误通过 future.get() 传回。
    // 未开启 renderThread 时在调用线程执行；开启后队列满仍会阻塞提交者。
    /**
     * @brief 提交一帧；队列满时等待，任务异常由 future.get 传回。
     * @param snapshot 按值持有的不可变快照，提交后可继续修改源场景。
     * @param view 本帧视图设置的副本。
     * @return CPU 提交完成后的统计 future；未开启渲染线程时在调用线程执行。
     */
    std::future<RenderStats> submit(RenderSnapshot snapshot, RenderView view);
    /**
     * @brief 在拥有上下文的线程交换窗口缓冲。
     * @return 无；离屏渲染器没有交换链，调用会抛出 logic_error。
     */
    void present();
    /**
     * @brief 查询实际图形后端与设备信息。
     * @return 后端、供应商、设备和驱动版本字符串。
     */
    DeviceInfo deviceInfo() const;
    /** @brief 在渲染队列中读取缓存与目标内存统计。@return GPU 有效载荷和 CPU 实例镜像估算，不等待
     * GPU 空闲。 */
    RendererMemoryStats memoryStatistics() const;
    /** @brief 创建透明度固定为 1 的 sRGB 相机输出，初始为黑色；仅限本 Renderer 使用。
     * @param target 像素尺寸。@return GPU 纹理共享引用，尺寸不可变；需缩放时创建新引用。 */
    RenderTexture createRenderTexture(const OffscreenConfig &target);
    /** @brief 用指定相机渲染并在 GPU 内复制到目标，不回读、不呈现窗口。
     * @param snapshot 场景快照，不允许采样本次写入的 target。@param view 相机/后处理。
     * @param target 本 Renderer 的有效输出。@return 本视图统计；渲染队列保证后续视图能采样结果。 */
    RenderStats renderToTexture(const RenderSnapshot &snapshot, const RenderView &view,
                                const RenderTexture &target);
    /**
     * @brief 阻塞回读最近一帧的最终图像。
     * @return 左上原点、显示编码的 RGBA8 图像；首次渲染前调用会抛出异常。
     */
    Image readback(); // 阻塞回读最近的输出，用于截图与测试。
    /** @brief 在同一队列任务中渲染并回读，确保返回本快照的图像。
     * @param snapshot 不可变快照。
     * @param view 当前视图设置。
     * @return 显示编码、左上原点 RGBA8 图像；同步等待 GPU，不需要 present。
     */
    Image renderToImage(const RenderSnapshot &snapshot, const RenderView &view);
    /** @brief 调整独立纹理目标尺寸，按队列顺序生效，下一帧重建附件。
     * @param width [1,16384] 内且不超过设备纹理上限的像素宽。
     * @param height [1,16384] 内且不超过设备纹理上限的像素高。
     * @return 无；窗口渲染器或无效尺寸会抛出异常，拒绝后保留原尺寸。
     */
    void resizeOffscreen(uint32_t width, uint32_t height);
    /**
     * @brief 等待此前排队任务以及 GPU 工作完成。
     * @return 无。
     */
    void waitIdle(); // 等待此前排队任务及 GPU 工作完成。
    /**
     * @brief 读取后端记录的调试错误数。
     * @return 累计错误计数。
     */
    uint32_t debugErrorCount() const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    /**
     * @brief 由工厂接管渲染器实现。
     * @param impl 包含后端与队列的独占实现状态。
     * @return 无（构造函数）。
     */
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_RENDERER_HPP_INCLUDED
