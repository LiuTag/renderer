/**
 * @file
 * @brief 声明高层渲染器、帧设置、透明与抗锯齿模式、统计和图像回读接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_RENDERER_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_RENDERER_HPP_INCLUDED
#include "renderer/render_snapshot.hpp"
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
 * @brief 显示编码、左上原点的 RGBA8 输出图像及 PPM 保存接口。
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
};

// 高层渲染入口；创建、使用与析构建议集中在应用主线程。
// PImpl 隐藏队列及后端；同一个窗口只能由一个 Renderer 或低层 Device 持有上下文。
/**
 * @brief 以 PImpl 隐藏后端与队列的高层帧渲染服务，同一窗口仅绑定一个 Renderer/Device。
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
    // 先绘制到自有离屏目标，再拷贝到窗口；窗口交换缓冲需显式调用 present()。
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
     * @return 无。
     */
    void present();
    /**
     * @brief 查询实际图形后端与设备信息。
     * @return 后端、供应商、设备和驱动版本字符串。
     */
    DeviceInfo deviceInfo() const;
    /**
     * @brief 阻塞回读最近一帧的最终图像。
     * @return 左上原点、显示编码的 RGBA8 图像。
     */
    Image readback(); // 阻塞回读最近的输出，用于截图与测试。
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
