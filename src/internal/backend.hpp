/**
 * @file
 * @brief 定义高层渲染后端边界、工作线程和有界任务队列，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_BACKEND_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_BACKEND_HPP_INCLUDED
#include "frame.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace renderer::detail {
// 高层后端边界：接收整理后的帧数据；原生图形 API 类型不穿过此接口。
/**
 * @brief 接收准备后帧数据的高层后端边界，不向前端暴露原生图形类型。
 */
class Backend {
  public:
    /**
     * @brief 在拥有上下文的线程销毁高层后端。
     * @return 无（析构函数）。
     */
    virtual ~Backend() = default;
    /**
     * @brief 读取窗口像素尺寸或独立纹理目标尺寸。
     * @return 宽、高像素数。
     */
    virtual std::pair<int, int> framebufferSize() const = 0;
    /** @brief 按设备限制更新独立纹理目标尺寸，窗口模式拒绝此操作。
     * @param width 正像素宽。@param height 正像素高。@return 无。
     */
    virtual void resizeOffscreen(uint32_t width, uint32_t height) = 0;
    /**
     * @brief 上传帧数据并执行高层渲染流程。
     * @param frame 已经裁剪、排序、合批的帧数据。
     * @param width 目标像素宽度。
     * @param height 目标像素高度。
     * @return 绘制统计与可用的 GPU 计时。
     */
    virtual RenderStats draw(PreparedFrame frame, int width, int height) = 0;
    /**
     * @brief 读取实际后端的设备信息。
     * @return 后端与设备描述字符串。
     */
    virtual DeviceInfo deviceInfo() const = 0;
    /**
     * @brief 同步回读最终输出。
     * @return 左上原点、显示编码的 RGBA8 图像。
     */
    virtual Image readback() = 0;
    /**
     * @brief 等待所有 GPU 工作完成。
     * @return 无。
     */
    virtual void waitIdle() = 0;
    /**
     * @brief 读取累计图形调试错误。
     * @return 累计错误计数。
     */
    virtual uint32_t debugErrorCount() const = 0;
    /**
     * @brief 交换窗口缓冲。
     * @return 无。
     */
    virtual void present() = 0;
    /**
     * @brief 解绑当前线程的图形上下文。
     * @return 无。
     */
    virtual void releaseContext() = 0;
    /**
     * @brief 使当前线程持有图形上下文。
     * @return 无。
     */
    virtual void acquireContext() = 0;
    /**
     * @brief 获取上下文生命周期引用，保证最终窗口释放在应用线程进行。
     * @return 不暴露原生类型的共享生命周期引用。
     */
    virtual std::shared_ptr<void> contextLifetime() const = 0;
    /**
     * @brief 设置窗口的独立渲染线程占用标记。
     * @param threaded 是否已将上下文移交给专用渲染线程。
     * @return 无。
     */
    virtual void setThreaded(bool threaded) = 0;
};
} // namespace renderer::detail

namespace renderer {
/**
 * @brief 管理具体后端、上下文生命周期和可选有界渲染工作队列的内部实现。
 */
struct Renderer::Impl {
    std::unique_ptr<detail::Backend> backend;
    RendererConfig config;
    // 即使后端在工作线程销毁，窗口的最后引用仍保留到应用线程析构 Impl 时释放。
    std::shared_ptr<void> lifetime;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable ready, space;
    std::deque<std::function<void()>> jobs;
    bool stopping = false;
    /**
     * @brief 启动可选工作线程并完成上下文接管握手。
     * @return 无。
     */
    void start() {
        lifetime = backend->contextLifetime();
        if (!config.renderThread)
            return;
        // OpenGL 上下文同一时刻只能属于一个线程，先解绑再交给工作线程接管。
        backend->releaseContext();
        backend->setThreaded(true);
        auto startup = std::make_shared<std::promise<void>>();
        auto initialized = startup->get_future();
        try {
            worker = std::thread([this, startup] {
                try {
                    backend->acquireContext();
                    startup->set_value();
                } catch (...) {
                    startup->set_exception(std::current_exception());
                    return;
                }
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(mutex);
                        ready.wait(lock, [&] { return stopping || !jobs.empty(); });
                        if (jobs.empty() && stopping)
                            break;
                        job = std::move(jobs.front());
                        jobs.pop_front();
                        space.notify_all();
                    }
                    job();
                }
                // 队列已排空，必须在仍拥有上下文的线程完成 GPU 资源销毁。
                backend.reset();
            });
            initialized.get(); // 启动握手：在构造成功前确认上下文已接管，异常回传主线程。
        } catch (...) {
            if (worker.joinable())
                worker.join();
            backend->acquireContext();
            backend->setThreaded(false);
            throw;
        }
    }
    /**
     * @brief 停止接收任务，排空队列并等待工作线程退出。
     * @return 无（析构函数）。
     */
    ~Impl() {
        if (worker.joinable()) {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            ready.notify_all();
            worker.join(); // stopping 只停止接收新任务，已有任务仍按顺序执行完毕。
        }
    }
    /**
     * @brief 按容量限制排队任务，并将结果或异常传递给 future。
     * @tparam F 任务可调用对象类型。
     * @param function 可无参调用的任务；无工作线程时同步执行。
     * @return 承载任务返回值或异常的 future。
     */
    template <class F> auto enqueue(F &&function) {
        using T = std::invoke_result_t<F>;
        // packaged_task 将任务异常放入 future，避免直接逃逸并终止工作线程。
        auto task = std::make_shared<std::packaged_task<T()>>(std::forward<F>(function));
        auto future = task->get_future();
        if (!config.renderThread) {
            (*task)();
            return future;
        }
        {
            std::unique_lock lock(mutex);
            // 队列满时施加背压，限制排队的快照数量及其持有的资源内存。
            space.wait(lock, [&] { return stopping || jobs.size() < config.maxQueuedFrames; });
            if (stopping)
                throw std::logic_error("Renderer is shutting down");
            jobs.emplace_back([task] { (*task)(); });
        }
        ready.notify_one();
        return future;
    }
};
} // namespace renderer

#endif // RENDERER_SRC_INTERNAL_BACKEND_HPP_INCLUDED
