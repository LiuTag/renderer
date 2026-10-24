/**
 * @file
 * @brief 声明独立 animation 模块的方向序列帧和事件，链接 renderer::animation 使用。
 */
#ifndef RENDERER_MODULES_ANIMATION_SPRITE_ANIMATION_HPP_INCLUDED
#define RENDERER_MODULES_ANIMATION_SPRITE_ANIMATION_HPP_INCLUDED
#include "renderer/scene.hpp"
#include <memory>
#include <string>
#include <vector>

namespace renderer {
/** @brief 序列帧的图片、持续时间和进入该帧时产生的可选事件。 */
struct SpriteAnimationFrame {
    SpriteHandle sprite;
    double duration = 0.1; ///< 秒，必须为正。
    std::string event;     ///< 空字符串不产生事件，例如使用 "footstep" 或 "hit"。
};
/**
 * @brief 一段动画的 1、4 或 8 个方向；各方向帧数、持续时间及事件必须一致。
 * 方向 0 朝向相机在 XZ 平面的背向量，随后朝相机右侧递增；四方向为前、右、后、左。
 */
struct SpriteAnimationClipDesc {
    std::vector<std::vector<SpriteAnimationFrame>> directions;
};
/** @brief 通过不可变共享 PImpl 保存序列帧；句柄由 AssetStore 拥有，不应在使用中销毁。 */
class SpriteAnimationClip {
  public:
    /**
     * @brief 校验并复制动画描述；每方向必须包含至少一帧。
     * @param description 完整的方向与帧数据，句柄实际有效性在场景提取时验证。
     * @return 无（构造函数）；元数据无效时抛出异常。
     */
    explicit SpriteAnimationClip(const SpriteAnimationClipDesc &description);
    /** @brief 释放动画数据引用。@return 无。 */
    ~SpriteAnimationClip();
    /** @brief 共享复制动画。@param other 源动画。@return 无。 */
    SpriteAnimationClip(const SpriteAnimationClip &other);
    /** @brief 共享复制动画。@param other 源动画。@return 当前动画。 */
    SpriteAnimationClip &operator=(const SpriteAnimationClip &other);
    /** @brief 转移动画引用。@param other 源动画。@return 无。 */
    SpriteAnimationClip(SpriteAnimationClip &&other) noexcept;
    /** @brief 转移动画引用。@param other 源动画。@return 当前动画。 */
    SpriteAnimationClip &operator=(SpriteAnimationClip &&other) noexcept;
    /** @brief 读取单次播放长度。@return 秒；已转移对象为 0。 */
    double duration() const;
    /** @brief 读取方向数。@return 1、4、8；已转移对象为 0。 */
    size_t directionCount() const;
    /** @brief 读取每个方向的帧数。@return 帧数；已转移对象为 0。 */
    size_t frameCount() const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class SpriteAnimator;
};
/** @brief 进入动画帧时生成的事件；跨帧更新也会按发生顺序保留事件。 */
struct SpriteAnimationEvent {
    std::string name;
    size_t frame = 0;
    uint64_t cycle = 0; ///< 从 0 开始的循环序号。
};
/** @brief 以 PImpl 管理一个对象的动画播放状态，按单写线程使用。 */
class SpriteAnimator {
  public:
    /** @brief 创建尚未播放动画的播放器。@return 无。 */
    SpriteAnimator();
    /** @brief 释放播放器。@return 无。 */
    ~SpriteAnimator();
    /** @brief 转移播放状态。@param other 源播放器。@return 无。 */
    SpriteAnimator(SpriteAnimator &&other) noexcept;
    /** @brief 转移播放状态。@param other 源播放器。@return 当前播放器。 */
    SpriteAnimator &operator=(SpriteAnimator &&other) noexcept;
    /** @brief 禁止复制可变状态。@param other 源播放器。@return 无；已删除。 */
    SpriteAnimator(const SpriteAnimator &other) = delete;
    /** @brief 禁止复制可变状态。@param other 源播放器。@return 无；已删除。 */
    SpriteAnimator &operator=(const SpriteAnimator &other) = delete;
    /**
     * @brief 从首帧开始播放，清除旧事件并产生首帧事件；保留速度，取消暂停。
     * @param clip 要播放的动画，内部共享其数据。
     * @param loop 是否循环；false 时停在末帧。
     * @return 无。
     */
    void play(const SpriteAnimationClip &clip, bool loop = true);
    /**
     * @brief 推进动画并收集事件；未播放时只校验时间后返回。
     * @param seconds 非负有限秒数，内部乘以播放速度。
     * @return 无；单次更新加待取事件超过 4096 时抛出异常且不推进状态。
     */
    void advance(double seconds);
    /**
     * @brief 根据世界朝向与相机选择最近的方向帧，不重置播放时间。
     * @param facing 非零的世界 XZ 朝向，Y 分量忽略。
     * @param cameraWorld 有限可逆的相机到世界矩阵。
     * @return 无；俯视极点以世界 +Z 作为方向 0。
     */
    void selectDirection(Vec3 facing, const Mat4 &cameraWorld);
    /**
     * @brief 将当前图片写回节点的 SpriteRenderer，保留其余表现设置。
     * @param scene 目标场景。
     * @param node 已有 SpriteRenderer 的有效节点。
     * @return 无；未播放动画或缺少组件时抛出异常。
     */
    void apply(Scene &scene, NodeHandle node) const;
    /** @brief 设置播放速度。@param speed 非负有限倍率，0 表示不推进。@return 无。 */
    void setSpeed(double speed);
    /** @brief 设置暂停。@param paused true 暂停时间推进。@return 无。 */
    void setPaused(bool paused);
    /** @brief 读取当前图片。@return 图片句柄；尚未播放时为空。 */
    SpriteHandle sprite() const;
    /** @brief 读取帧序号。@return 从 0 开始的当前帧。 */
    size_t frameIndex() const;
    /** @brief 读取方向序号。@return 从 0 开始的当前方向。 */
    size_t directionIndex() const;
    /** @brief 查询非循环动画结束状态。@return 已到末尾时为 true。 */
    bool finished() const;
    /** @brief 取出并清空事件。@return 按时间排列的事件列表。 */
    std::vector<SpriteAnimationEvent> takeEvents();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif // RENDERER_MODULES_ANIMATION_SPRITE_ANIMATION_HPP_INCLUDED
