/** @file @brief 声明无逐精灵场景节点的批量入口，供地图分块和粒子共享核心渲染流程。 */
#ifndef RENDERER_INCLUDE_RENDERER_SPRITE_BATCH_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_SPRITE_BATCH_HPP_INCLUDED
#include "renderer/sprite.hpp"
#include <cstdint>
#include <memory>
#include <span>
namespace renderer {
namespace detail {
struct SpriteBatchAccess;
}
/** @brief 一个批量精灵的世界变换、表现参数和应用身份；不创建场景节点。 */
struct BatchSprite {
    SpriteRenderer renderer;
    Mat4 world = Mat4::identity();
    uint64_t id = 0; ///< 应用身份，由拾取结果返回；0 表示未指定。
};
/** @brief 不可变共享 PImpl 的批量精灵；替换采用写时发布，已有快照保持旧内容。 */
class SpriteBatch {
  public:
    /** @brief 创建空批次。@return 无。 */
    SpriteBatch();
    /** @brief 释放共享数据。@return 无。 */
    ~SpriteBatch();
    /** @brief 共享复制。@param other 源批次。@return 无。 */
    SpriteBatch(const SpriteBatch &other);
    /** @brief 共享赋值。@param other 源批次。@return 当前对象。 */
    SpriteBatch &operator=(const SpriteBatch &other);
    /** @brief 转移数据。@param other 源批次。@return 无。 */
    SpriteBatch(SpriteBatch &&other) noexcept;
    /** @brief 转移赋值。@param other 源批次。@return 当前对象。 */
    SpriteBatch &operator=(SpriteBatch &&other) noexcept;
    /** @brief 原子式替换本包装对象的数据；按单写线程调用。
     * @param assets 解析资源的仓库；成功后批次保留资源引用。
     * @param sprites 世界空间精灵数组，复制数据，不保存传入 span。
     * @return 无；校验失败保留旧内容。
     */
    void replace(const AssetStore &assets, std::span<const BatchSprite> sprites);
    /** @brief 查询实例数。@return 空批次为 0。 */
    size_t size() const;
    /** @brief 查询包含任意 Billboard 朝向的保守世界包围盒。@return 空批次为零包围盒。 */
    AABB bounds() const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend struct detail::SpriteBatchAccess;
};
} // namespace renderer
#endif // RENDERER_INCLUDE_RENDERER_SPRITE_BATCH_HPP_INCLUDED
