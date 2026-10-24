/**
 * @file
 * @brief 声明场景提取器与可跨线程读取的不可变渲染快照。
 */
#ifndef RENDERER_INCLUDE_RENDERER_RENDER_SNAPSHOT_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_RENDER_SNAPSHOT_HPP_INCLUDED
#include "renderer/scene.hpp"
#include "renderer/sprite_snapshot.hpp"
#include <cstddef>
#include <memory>
#include <optional>

namespace renderer {
struct RenderView;
namespace detail {
struct SnapshotAccess;
}

// 可复制的不可变 PImpl；共享已解析资源，不依赖原 Scene/AssetStore 的存活。
// 可交给渲染线程读取，后续场景修改只会影响下一次提取的快照。
/**
 * @brief 共享不可变 PImpl 的渲染快照，保有解析后的资源并允许渲染线程读取。
 */
class RenderSnapshot {
  public:
    /**
     * @brief 创建空快照。
     * @return 无（构造函数）。
     */
    RenderSnapshot();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~RenderSnapshot();
    /**
     * @brief 共享复制构造，共享不可变内部数据。
     * @param other 待复制的同类型对象。
     * @return 无（构造函数）。
     */
    RenderSnapshot(const RenderSnapshot &other);
    /**
     * @brief 共享复制赋值，共享不可变内部数据。
     * @param other 待复制的同类型对象。
     * @return 当前对象的引用。
     */
    RenderSnapshot &operator=(const RenderSnapshot &other);
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    RenderSnapshot(RenderSnapshot &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    RenderSnapshot &operator=(RenderSnapshot &&other) noexcept;
    /**
     * @brief 读取快照中的网格与精灵实例总数，不包含继承隐藏的节点。
     * @return 实例数量；空快照为零，精灵的最终退化检查在视图准备时进行。
     */
    size_t instanceCount() const;
    /** @brief 读取快照中的精灵数量。@return 精灵数量；空快照为零。 */
    size_t spriteCount() const;
    /**
     * @brief 读取快照中的可见相机数。
     * @return 相机数量；空快照为零。
     */
    size_t cameraCount() const;
    /**
     * @brief 查询提取该快照时的场景版本。
     * @return 保存的场景版本号；空快照为零。
     */
    uint64_t revision() const;
    /**
     * @brief 按源节点查询实例的逻辑世界矩阵，精灵返回未进行 Billboard 的矩阵。
     * @param node 生成该实例的场景节点句柄。
     * @return 找到时返回世界矩阵，否则返回空值。
     */
    std::optional<Mat4> instanceTransform(NodeHandle node) const;
    /**
     * @brief 为当前快照计算指定视图的只读精灵几何与矩阵，不修改场景或依赖扩展模块。
     * @param view 选择相机、裁剪及透明模式的视图配置。
     * @param width 正的帧缓冲像素宽度。
     * @param height 正的帧缓冲像素高度。
     * @return 保有资源的不可变视图；空快照、无效相机/尺寸/透明模式时抛出异常。
     */
    SpriteViewSnapshot spriteView(const RenderView &view, uint32_t width, uint32_t height) const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class RenderExtractor;
    friend struct detail::SnapshotAccess;
};

// 将可变场景转为不可变快照；内部缓存世界包围盒及法线矩阵，按单写线程使用。
/**
 * @brief 使用 PImpl 缓存派生数据、将可变场景转为不可变快照的单写线程服务。
 */
class RenderExtractor {
  public:
    /**
     * @brief 创建场景提取器及其缓存。
     * @return 无（构造函数）。
     */
    RenderExtractor();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~RenderExtractor();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    RenderExtractor(RenderExtractor &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    RenderExtractor &operator=(RenderExtractor &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    RenderExtractor(const RenderExtractor &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    RenderExtractor &operator=(const RenderExtractor &other) = delete;
    // 校验网格与材质槽；遇到已销毁/跨仓库资源句柄会抛出异常。
    /**
     * @brief 更新场景派生数据并提取不可变快照；无效资源或材质槽抛出异常。
     * @param scene 按单写线程使用的源场景，提取会刷新内部缓存。
     * @param assets 用于解析网格、材质和纹理的资源仓库。
     * @return 共享不可变资源数据、可独立于源对象存活的快照。
     */
    RenderSnapshot extract(Scene &scene, const AssetStore &assets);

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_RENDER_SNAPSHOT_HPP_INCLUDED
