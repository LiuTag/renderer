/**
 * @file
 * @brief 声明独立 interaction 模块的射线与图片拾取，链接 renderer::interaction 使用。
 */
#ifndef RENDERER_MODULES_INTERACTION_PICKING_HPP_INCLUDED
#define RENDERER_MODULES_INTERACTION_PICKING_HPP_INCLUDED
#include "renderer/renderer.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace renderer {
/** @brief 世界空间射线，方向为单位向量，原点位于相机近裁剪面。 */
struct Ray {
    Vec3 origin, direction;
};
/** @brief 图片拾取结果；节点来自快照，调用者操作场景前应检查节点是否仍有效。 */
struct SpriteHit {
    NodeHandle node;
    Vec3 worldPosition;
    Vec2 textureUV;
    float distance = 0;      ///< 从近裁剪面射线原点到图片的距离。
    uint64_t instanceId = 0; ///< 批量精灵身份；此时 node 为空，应由应用解释身份。
};
/** @brief 由交互层拥有的筛选条件；渲染组件不保存是否可拾取等业务属性。 */
struct SceneQueryFilter {
    uint32_t layerMask = 0xFFFFFFFFu;      ///< 与精灵可见层额外求交，不影响实际绘制。
    std::vector<NodeHandle> excludedNodes; ///< 排除阴影、标记、附件等节点，构造查询时复制筛选。
};
/**
 * @brief 以 PImpl 缓存指定视图的精灵几何，查询与渲染使用同一 Billboard 和排序规则。
 * 保有不可变快照，允许只读并发查询；只拾取精灵，不检测普通网格遮挡或替代物理碰撞。
 */
class SceneQuery {
  public:
    /**
     * @brief 从快照创建视图查询缓存。
     * @param snapshot 包含相机及精灵的有效快照。
     * @param view 相机、裁剪和透明模式，必须与显示该快照的视图一致。
     * @param width 帧缓冲像素宽度，须为正。
     * @param height 帧缓冲像素高度，须为正。
     * @param filter 交互层额外筛选，默认接受所有渲染可见的精灵。
     * @return 无（构造函数）；无效相机、尺寸或透明模式抛出异常。
     */
    SceneQuery(RenderSnapshot snapshot, const RenderView &view, uint32_t width, uint32_t height,
               const SceneQueryFilter &filter = {});
    /** @brief 释放查询缓存及快照引用。@return 无。 */
    ~SceneQuery();
    /** @brief 转移查询缓存。@param other 源查询。@return 无。 */
    SceneQuery(SceneQuery &&other) noexcept;
    /** @brief 转移查询缓存。@param other 源查询。@return 当前查询。 */
    SceneQuery &operator=(SceneQuery &&other) noexcept;
    /** @brief 禁止复制缓存。@param other 源查询。@return 无；已删除。 */
    SceneQuery(const SceneQuery &other) = delete;
    /** @brief 禁止复制缓存。@param other 源查询。@return 无；已删除。 */
    SceneQuery &operator=(const SceneQuery &other) = delete;
    /**
     * @brief 将屏幕位置转换为世界射线，支持透视和正交投影。
     * @param pixel 帧缓冲像素坐标，左上角为原点；允许视口外的有限位置。
     * @return 从近裁剪面出发的单位方向射线。
     */
    Ray screenRay(Vec2 pixel) const;
    /**
     * @brief 求屏幕射线与水平地面的交点。
     * @param pixel 左上原点的帧缓冲像素坐标。
     * @param height 地面世界 Y 坐标，默认为 0，须有限。
     * @return 交点；射线平行或交点不在近远裁剪面之间时为空。
     */
    std::optional<Vec3> groundPoint(Vec2 pixel, float height = 0) const;
    /**
     * @brief 拾取视口内最前的可拾取精灵，跳过透明像素。
     * @param pixel 左上原点的帧缓冲像素坐标；视口外返回空值。
     * @param alphaThreshold Blend 的最小可拾取 Alpha，范围 [0,1]，Alpha 为 0 总是跳过。
     * @return 命中的图片；无命中为空。按纹理第 0 级采样，OIT 内按最近交点选择。
     */
    std::optional<SpriteHit> pickSprite(Vec2 pixel, float alphaThreshold = 0.1f) const;
    /**
     * @brief 将世界位置投影到屏幕，便于放置名字或交互提示。
     * @param world 有限世界位置。
     * @return 左上原点的像素 XY 和 [0,1] 深度 Z；视锥外返回空值。
     */
    std::optional<Vec3> worldToScreen(Vec3 world) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif // RENDERER_MODULES_INTERACTION_PICKING_HPP_INCLUDED
