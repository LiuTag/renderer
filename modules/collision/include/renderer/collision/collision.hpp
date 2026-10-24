/** @file @brief XZ 平面圆形/AABB 障碍、空间网格查询与连续圆形移动接口。 */
#ifndef RENDERER_MODULES_COLLISION_COLLISION_HPP_INCLUDED
#define RENDERER_MODULES_COLLISION_COLLISION_HPP_INCLUDED
#include <memory>
#include <renderer/handle.hpp>
#include <renderer/math.hpp>
#include <vector>
namespace renderer {
struct ColliderTag;
using ColliderHandle = Handle<ColliderTag>;
/** @brief 二维碰撞形状；Vec2 的 x/y 分别对应世界 X/Z。 */
enum class ColliderShape { Circle, Box };
/** @brief 障碍描述；圆形用 radius，轴对齐矩形用 halfExtents。 */
struct ColliderDesc {
    ColliderShape shape = ColliderShape::Circle;
    Vec2 center{}, halfExtents{.5f, .5f};
    float radius = .5f;
    uint32_t layerMask = ~0u;
    uint64_t userData = 0;
};
/** @brief 移动后的中心、剩余位移和接触障碍；不产生伤害或业务回调。 */
struct CircleMove {
    Vec2 position{}, remaining{};
    std::vector<ColliderHandle> contacts;
};
/** @brief PImpl 碰撞世界，空间网格宽阶段、圆形连续检测及滑动；按单写线程使用。 */
class CollisionWorld {
  public:
    /** @brief 创建空世界。@param cellSize 正的网格边长。@return 无。 */
    explicit CollisionWorld(float cellSize = 2);
    /** @brief 释放碰撞数据。@return 无。 */
    ~CollisionWorld();
    /** @brief 转移世界。@param other 源世界。@return 无。 */
    CollisionWorld(CollisionWorld &&other) noexcept;
    /** @brief 转移赋值。@param other 源世界。@return 当前对象。 */
    CollisionWorld &operator=(CollisionWorld &&other) noexcept;
    /** @brief 添加障碍。@param collider 合法形状和有限参数。@return 带归属/代际的句柄。 */
    ColliderHandle add(const ColliderDesc &collider);
    /** @brief 替换障碍。@param handle 当前有效句柄。@param collider 新描述。@return 无。 */
    void update(ColliderHandle handle, const ColliderDesc &collider);
    /** @brief 删除障碍。@param handle 当前有效句柄。@return 无；旧句柄立即失效。 */
    void remove(ColliderHandle handle);
    /** @brief 读取障碍。@param handle 当前有效句柄。@return 障碍副本。 */
    ColliderDesc description(ColliderHandle handle) const;
    /** @brief 检查句柄。@param handle 待检查句柄。@return 归属、代际和存活状态均匹配时为真。 */
    bool valid(ColliderHandle handle) const;
    /** @brief 圆形重叠查询，包含边界接触。
     * @param center XZ 中心。@param radius 正半径。@param mask 查询层掩码。
     * @param ignore 可忽略的自身句柄。@return 去重的相交障碍。
     */
    std::vector<ColliderHandle> overlapCircle(Vec2 center, float radius, uint32_t mask = ~0u,
                                              ColliderHandle ignore = {});
    /** @brief 连续移动圆形并沿表面滑动；AABB 使用保守扩张，转角可能略提前接触。
     * @param center 初始 XZ 中心。@param radius 正半径。@param delta 目标位移。
     * @param mask 查询层掩码。@param ignore 忽略自身。@param iterations 1～16 次接触迭代。
     * @return 最终中心、未消费位移和接触列表；不修改世界中的障碍。
     */
    CircleMove moveCircle(Vec2 center, float radius, Vec2 delta, uint32_t mask = ~0u,
                          ColliderHandle ignore = {}, uint32_t iterations = 6);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif
