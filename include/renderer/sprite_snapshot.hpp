/**
 * @file
 * @brief 声明精灵在指定视图中展开后的只读渲染数据，供扩展模块复用公共接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_SPRITE_SNAPSHOT_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_SPRITE_SNAPSHOT_HPP_INCLUDED
#include "renderer/scene.hpp"
#include "renderer/sprite.hpp"
#include <memory>
#include <span>
#include <utility>

namespace renderer {
/** @brief 展开后的精灵渲染记录；纹理为共享只读 CPU 数据，不包含内部资源类型。 */
struct SpriteViewItem {
    NodeHandle node;
    Mat4 world; ///< 当前视图计算的图片平面变换，已包含锚点、裁剪、翻转及 Billboard。
    AABB bounds;
    Vec4 uvRect; ///< UV 偏移 XY、缩放 ZW，翻转时缩放为负。
    Vec4 tint;
    AlphaMode alphaMode = AlphaMode::Blend;
    float alphaCutoff = 0.5f;
    SpriteTransparency transparency = SpriteTransparency::Sorted;
    uint32_t layerMask = 0xFFFFFFFFu;
    std::shared_ptr<const TextureDesc> texture; ///< 复制该引用可独立保留纹理像素。
};
/**
 * @brief 以共享不可变 PImpl 保存指定视图的精灵渲染数据，不包含动画或交互逻辑。
 * 由 RenderSnapshot::spriteView 创建；独立于源场景/仓库存活，可跨线程只读。
 */
class SpriteViewSnapshot {
  public:
    /** @brief 创建空视图。@return 无。 */
    SpriteViewSnapshot();
    /** @brief 释放只读视图引用。@return 无。 */
    ~SpriteViewSnapshot();
    /** @brief 共享复制视图。@param other 源视图。@return 无。 */
    SpriteViewSnapshot(const SpriteViewSnapshot &other);
    /** @brief 共享复制视图。@param other 源视图。@return 当前视图。 */
    SpriteViewSnapshot &operator=(const SpriteViewSnapshot &other);
    /** @brief 转移视图引用。@param other 源视图。@return 无。 */
    SpriteViewSnapshot(SpriteViewSnapshot &&other) noexcept;
    /** @brief 转移视图引用。@param other 源视图。@return 当前视图。 */
    SpriteViewSnapshot &operator=(SpriteViewSnapshot &&other) noexcept;
    /**
     * @brief 读取按层、落地点及分组排序的精灵记录；只包含层匹配且通过视图裁剪的几何。
     * @return 只读记录范围，空视图为空；范围只在本视图数据仍存活期间有效。
     */
    std::span<const SpriteViewItem> sprites() const;
    /** @brief 读取视投影矩阵。@return NDC 深度为 [0,1] 的矩阵；空视图为单位矩阵。 */
    Mat4 viewProjection() const;
    /** @brief 读取视投影逆矩阵。@return 用于反投影的矩阵；空视图为单位矩阵。 */
    Mat4 inverseViewProjection() const;
    /** @brief 读取视口像素尺寸。@return 宽高；空视图为 (0,0)。 */
    std::pair<uint32_t, uint32_t> viewportSize() const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class RenderSnapshot;
};
} // namespace renderer
#endif // RENDERER_INCLUDE_RENDERER_SPRITE_SNAPSHOT_HPP_INCLUDED
