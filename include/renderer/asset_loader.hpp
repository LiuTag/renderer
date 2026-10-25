/** @file @brief 声明有界后台图片/模型读取、优先级、取消及主线程发布接口。 */
#ifndef RENDERER_INCLUDE_RENDERER_ASSET_LOADER_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_ASSET_LOADER_HPP_INCLUDED
#include "renderer/model.hpp"
#include <memory>
#include <string>
#include <vector>
namespace renderer {
using AssetRequestId = uint64_t;
/** @brief 请求生命周期；Ready 尚未进入目标仓库，Published 后句柄才能用于场景。 */
enum class AssetLoadState { Queued, Loading, Ready, Published, Cancelled, Failed };
/** @brief 后台任务/结果容量；maxRequests 包含未 forget 的终态记录。 */
struct AssetLoaderConfig {
    uint32_t workers = 2, maxRequests = 128;
    uint64_t maxReadyBytes = 128ull * 1024 * 1024;
};
/** @brief 主线程发布预算；单个超大资源允许独占一次发布，避免永久饥饿。 */
struct AssetPublishBudget {
    uint32_t maxRequests = 4;
    uint64_t maxBytes = 16ull * 1024 * 1024;
};
/** @brief 请求状态副本；错误为 UTF-8，取消不强制中断已开始的解码。 */
struct AssetLoadInfo {
    AssetRequestId id = 0;
    AssetLoadState state = AssetLoadState::Queued;
    uint64_t bytes = 0;
    std::string error;
};
/** @brief 发布/失败/取消事件；texture 或 model 只有 Published 时有效，资源归目标仓库拥有。 */
struct AssetLoadResult {
    AssetLoadInfo info;
    TextureHandle texture;
    Model model;
    uint64_t package = 0;
};
/** @brief 请求和已解码结果统计，不含工作线程正在解码的暂存峰值。 */
struct AssetLoaderStats {
    uint32_t queued = 0, loading = 0, ready = 0, records = 0;
    uint64_t readyBytes = 0;
};
/** @brief PImpl 加载服务；控制接口按创建线程调用，析构取消剩余请求并等待解码线程退出。 */
class AssetLoader {
  public:
    /** @brief 创建固定后台线程。@param config 有界容量，workers 为 [1,16]。@return 无。 */
    explicit AssetLoader(const AssetLoaderConfig &config = {});
    /** @brief 停止并等待后台任务，不访问目标仓库。@return 无。 */
    ~AssetLoader();
    /** @brief 禁止复制工作线程服务。@param other 源服务。@return 无，已删除。 */
    AssetLoader(const AssetLoader &other) = delete;
    /** @brief 禁止复制赋值。@param other 源服务。@return 无，已删除。 */
    AssetLoader &operator=(const AssetLoader &other) = delete;
    /** @brief 排队图片读取/解码。@param path Unicode 文件路径。@param srgb 颜色纹理设 true。
     * @param priority 越大越先开始，相同按请求顺序。@param sampling 图片采样方式。@return
     * 唯一请求身份。 */
    AssetRequestId loadTexture(const std::filesystem::path &path, bool srgb = true,
                               int priority = 0, TextureSampling sampling = {});
    /** @brief 排队完整静态模型及依赖读取。@param path glTF/GLB/OBJ 路径。@param priority
     * 越大越优先。@return 请求身份。 */
    AssetRequestId loadModel(const std::filesystem::path &path, int priority = 0);
    /** @brief 取消未发布请求，已发布资源不被销毁。@param id 本服务请求身份。@return
     * 是否发生状态改变。 */
    bool cancel(AssetRequestId id);
    /** @brief 按预算将 Ready 资源零拷贝接收进仓库，并取走一次性终态事件。
     * @param destination 由调用线程独占的仓库。@param budget 发布数/CPU 载荷预算，均须大于 0。
     * @return 本次发布、失败、取消事件；GPU 上传由 Renderer 的独立预算控制。 */
    std::vector<AssetLoadResult> publish(AssetStore &destination, AssetPublishBudget budget = {});
    /** @brief 读取请求状态。@param id 本服务有效身份。@return 值副本。 */
    AssetLoadInfo status(AssetRequestId id) const;
    /** @brief 删除已消费的终态记录以复用容量。@param id Published/Cancelled/Failed 身份。@return
     * 无。 */
    void forget(AssetRequestId id);
    /** @brief 查询后台队列状态。@return 当前统计副本。 */
    AssetLoaderStats statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif
