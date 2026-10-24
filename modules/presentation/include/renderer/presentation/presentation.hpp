/** @file @brief 场景精灵的自动遮挡淡出和短时颜色覆盖控制，不处理伤害规则。 */
#ifndef RENDERER_MODULES_PRESENTATION_PRESENTATION_HPP_INCLUDED
#define RENDERER_MODULES_PRESENTATION_PRESENTATION_HPP_INCLUDED
#include <renderer/renderer.hpp>
namespace renderer {
/** @brief 自动淡出配置；只处理 Blend 场景精灵，批次和物理遮挡不在此控制器内。 */
struct SpritePresentationConfig {
    float occludedAlpha = .25f; ///< 遮挡时保留的原始 Alpha 比例，[0,1]。
    float fadeSeconds = .2f;    ///< 淡入淡出的正持续秒数。
    float alphaThreshold = .1f; ///< 判定遮挡的图片 Alpha 阈值，[0,1]。
    uint32_t occluderMask = ~0u;
    std::vector<NodeHandle> excludedNodes;
};
/** @brief PImpl 表现控制器；激活期间拥有节点 Alpha 和 colorOverlay，结束后恢复原值。 */
class SpritePresentation {
  public:
    /** @brief 创建控制器。@param config 淡出权重、过渡秒数和筛选条件。@return 无。 */
    explicit SpritePresentation(const SpritePresentationConfig &config = {});
    /** @brief 释放状态；恢复场景值应先调用 reset。@return 无。 */
    ~SpritePresentation();
    /** @brief 转移状态。@param other 源对象。@return 无。 */
    SpritePresentation(SpritePresentation &&other) noexcept;
    /** @brief 转移赋值。@param other 源对象。@return 当前对象。 */
    SpritePresentation &operator=(SpritePresentation &&other) noexcept;
    /** @brief 请求一次短时颜色覆盖，再线性衰减到原值。
     * @param node 目标精灵节点。@param color RGB 和 [0,1] 覆盖权重。
     * @param seconds 正持续时间。@return 无；节点在 update 时检查。
     */
    void flash(NodeHandle node, Vec4 color = {1, 1, 1, 1}, float seconds = .15f);
    /** @brief 查找相机与目标之间的精灵，推进淡出和闪白；之后重新提取快照绘制。
     * @param scene 可变场景。@param snapshot 当前几何快照。@param view 当前视图。
     * @param width 视口像素宽。@param height 视口像素高。
     * @param target 被保护目标的世界位置。@param seconds 非负有限秒数。
     * @return 无；不检测网格遮挡，透明像素通过 Alpha 过滤。
     */
    void update(Scene &scene, const RenderSnapshot &snapshot, const RenderView &view,
                uint32_t width, uint32_t height, Vec3 target, double seconds);
    /** @brief 恢复仍有效节点的原始 Alpha/颜色覆盖并清空状态。@param scene 原场景。@return 无。 */
    void reset(Scene &scene);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif
