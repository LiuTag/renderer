/** @file @brief Box2D 的不透明二维物理接口：刚体、事件、查询、关节与 Scene XZ 同步。 */
#ifndef RENDERER_MODULES_PHYSICS2D_PHYSICS2D_HPP_INCLUDED
#define RENDERER_MODULES_PHYSICS2D_PHYSICS2D_HPP_INCLUDED
#include <memory>
#include <optional>
#include <renderer/scene.hpp>
#include <vector>
namespace renderer {
struct Body2DTag;
struct Joint2DTag;
using Body2DHandle = Handle<Body2DTag>;
using Joint2DHandle = Handle<Joint2DTag>;
/** @brief 静态、速度驱动的运动学和受力驱动的动态刚体。 */
enum class Body2DType { Static, Kinematic, Dynamic };
/** @brief 支持的局部碰撞形状；多边形用输入点的凸包。 */
enum class Shape2DType { Circle, Box, Capsule, Polygon };
/** @brief 一块形状的几何、质量和过滤参数；二维 x/y 对应世界 X/Z。 */
struct Shape2DDesc {
    Shape2DType type = Shape2DType::Circle;
    Vec2 offset{}, halfExtents{.5f, .5f}, pointA{0, -.5f}, pointB{0, .5f};
    float radius = .5f, angle = 0; ///< 圆/胶囊半径；局部旋转为弧度，多边形圆角固定为零。
    std::vector<Vec2> vertices;    ///< 多边形使用 3～8 个局部点；重复/共线导致退化时拒绝。
    float density = 1, friction = .3f, restitution = 0;
    uint64_t categoryBits = 1, maskBits = ~uint64_t(0);
    bool sensor = false, contactEvents = true, hitEvents = false;
};
/** @brief 刚体创建参数；可附多个实体/传感器形状，userData 是应用持久身份。 */
struct Body2DDesc {
    Body2DType type = Body2DType::Dynamic;
    Vec2 position{}, linearVelocity{};
    float angle = 0, angularVelocity = 0, linearDamping = 0, angularDamping = 0, gravityScale = 1;
    bool lockRotation = false, bullet = false, allowSleep = true, enabled = true;
    uint64_t userData = 0;
    std::vector<Shape2DDesc> shapes{Shape2DDesc{}};
};
/** @brief 已模拟的当前状态；角度为物理平面逆时针弧度，映射世界 Y 旋转时取负。 */
struct Body2DState {
    Vec2 position{}, linearVelocity{};
    float angle = 0, angularVelocity = 0, mass = 0;
    bool awake = false, enabled = true;
};
/** @brief 固定步物理配置；重力默认零，适合俯视游戏。 */
struct Physics2DConfig {
    Vec2 gravity{};
    double fixedTimeStep = 1.0 / 60;
    uint32_t subSteps = 4, maxStepsPerAdvance = 8;
    uint32_t workerCount = 1; ///< 1～16；大于 1 使用 Box2D 原生任务线程，不是渲染线程。
};
/** @brief 本次 advance 的步数、插值比例及过载时明确丢弃的现实秒数。 */
struct Physics2DAdvance {
    uint32_t steps = 0;
    double interpolationAlpha = 0, droppedSeconds = 0;
};
/** @brief 可用于 HUD/诊断的物理计数和本次 advance CPU 时间。 */
struct Physics2DStats {
    uint32_t bodies = 0, shapes = 0, joints = 0;
    double simulationMilliseconds = 0;
    Physics2DAdvance lastAdvance;
};
/** @brief 物理步结束后复制到应用线程的接触和传感器事件。 */
enum class Physics2DEventKind { ContactBegin, ContactEnd, SensorBegin, SensorEnd, Hit };
/** @brief 事件值快照；传感器事件 a 是传感器，删除后的旧句柄仍保留身份但 valid 为假。 */
struct Physics2DEvent {
    Physics2DEventKind kind;
    Body2DHandle a, b;
    uint32_t shapeA = 0, shapeB = 0;
    uint64_t userDataA = 0, userDataB = 0;
    Vec2 point{}, normal{};  ///< 仅 Hit 提供命中点/从 a 指向 b 的法线。
    float approachSpeed = 0; ///< 仅 Hit 提供接近速度。
};
/** @brief 查询过滤，采用双方 category/mask 规则，可排除自身和传感器。 */
struct Physics2DQueryFilter {
    uint64_t categoryBits = ~uint64_t(0), maskBits = ~uint64_t(0);
    bool includeSensors = false;
    Body2DHandle ignore;
};
/** @brief 射线最近命中值，不暴露 Box2D 的形状身份。 */
struct Physics2DHit {
    Body2DHandle body;
    uint32_t shapeIndex = 0;
    uint64_t userData = 0;
    Vec2 point{}, normal{};
    float fraction = 0; ///< 沿输入位移的 [0,1] 比例。
};
/** @brief 连接两个刚体局部锚点的距离/弹簧关节。 */
struct DistanceJoint2DDesc {
    Body2DHandle a, b;
    Vec2 anchorA{}, anchorB{};
    float length = 1, minLength = .01f, maxLength = 1000, hertz = 2, dampingRatio = .7f;
    bool spring = false, limit = false, collideConnected = false;
};
/** @brief 以 PImpl 隐藏 Box2D 的物理世界；对外单写，内部可用原生工作线程。 */
class Physics2D {
  public:
    /** @brief 创建世界。@param config 重力、固定步与线程预算。@return 无。 */
    explicit Physics2D(const Physics2DConfig &config = {});
    /** @brief 销毁全部刚体、关节和内部任务线程。@return 无。 */
    ~Physics2D();
    /** @brief 转移世界。@param other 源对象。@return 无。 */
    Physics2D(Physics2D &&other) noexcept;
    /** @brief 转移赋值。@param other 源对象。@return 当前对象。 */
    Physics2D &operator=(Physics2D &&other) noexcept;
    /** @brief 创建刚体和所有附属形状，失败回滚。@param description 几何和动力学描述。
     * @return 带归属/代际的安全句柄，不可序列化为存档身份。
     */
    Body2DHandle createBody(const Body2DDesc &description);
    /** @brief 删除刚体，附属关节也失效。@param body 有效句柄。@return 无。 */
    void destroyBody(Body2DHandle body);
    /** @brief 检查刚体句柄。@param body 待检查值。@return 归属、代际和存活均匹配时为真。 */
    bool valid(Body2DHandle body) const;
    /** @brief 读取当前状态。@param body 有效句柄。@return 独立值快照。 */
    Body2DState state(Body2DHandle body) const;
    /** @brief 显式瞬移并重置插值前态，不用于正常移动。@param body 有效句柄。
     * @param position 新二维位置。@param angle 物理角度弧度。@return 无。
     */
    void setTransform(Body2DHandle body, Vec2 position, float angle = 0);
    /** @brief 设置速度。@param body 有效刚体。@param linear 线速度。@param angular
     * 角速度，弧度/秒。@return 无。 */
    void setVelocity(Body2DHandle body, Vec2 linear, float angular = 0);
    /** @brief 启停刚体；停用时不移动也不碰撞。@param body 有效句柄。@param enabled
     * 是否启用。@return 无。 */
    void setEnabled(Body2DHandle body, bool enabled);
    /** @brief 施加下一次物理步的力。@param body 有效句柄。@param force 平面力。
     * @param point 世界施力点，空则在质心。@param wake 是否唤醒。@return 无。
     */
    void applyForce(Body2DHandle body, Vec2 force, std::optional<Vec2> point = {},
                    bool wake = true);
    /** @brief 施加瞬时冲量。@param body 有效句柄。@param impulse 平面冲量。
     * @param point 世界施力点，空则在质心。@param wake 是否唤醒。@return 无。
     */
    void applyImpulse(Body2DHandle body, Vec2 impulse, std::optional<Vec2> point = {},
                      bool wake = true);
    /** @brief 累积现实时间并推进有限个固定步。@param seconds [0,4] 秒。
     * @return 步数/插值/丢弃时间；最大积压为 fixedTimeStep*maxStepsPerAdvance。
     */
    Physics2DAdvance advance(double seconds);
    /** @brief 精确推进一个固定步，不消费 advance 的余数，适合外部固定步循环。@return 无。 */
    void step();
    /** @brief 取走并清空已复制的事件。@return 跨多个固定步累计的值快照。 */
    std::vector<Physics2DEvent> takeEvents();
    /** @brief 查询最近命中。@param origin 起点。@param translation 非零射线位移。
     * @param filter 层/传感器/自身筛选。@return 命中或空值；初始重叠遵循 Box2D 射线规则。
     */
    std::optional<Physics2DHit> rayCast(Vec2 origin, Vec2 translation,
                                        const Physics2DQueryFilter &filter = {}) const;
    /** @brief 精确圆形重叠查询。@param center 圆心。@param radius 正半径。
     * @param filter 查询过滤。@return 去重的刚体句柄。
     */
    std::vector<Body2DHandle> overlapCircle(Vec2 center, float radius,
                                            const Physics2DQueryFilter &filter = {}) const;
    /** @brief 创建距离或弹簧关节。@param description 两个不同有效刚体的约束。@return 关节句柄。 */
    Joint2DHandle createDistanceJoint(const DistanceJoint2DDesc &description);
    /** @brief 删除关节。@param joint 有效句柄。@return 无。 */
    void destroyJoint(Joint2DHandle joint);
    /** @brief 检查关节句柄。@param joint 待检查值。@return 归属、代际和存活均匹配时为真。 */
    bool valid(Joint2DHandle joint) const;
    /** @brief 绑定节点，保留初始视觉偏移/Y 高度/缩放，不能重复绑定同一节点。
     * @param body 有效刚体。@param scene 节点场景。@param node 有效的非根节点。
     * @param rotation 是否把物理角度同步为负的世界 Y 旋转。@return 无。
     */
    void bindNode(Body2DHandle body, Scene &scene, NodeHandle node, bool rotation = false);
    /** @brief 解除绑定，不删除节点。@param body 有效刚体。@return 无。 */
    void unbindNode(Body2DHandle body);
    /** @brief 同步本场景的绑定，按父子顺序更新，已删除节点自动解绑。
     * @param scene 要同步的场景。@param interpolate 是否按 advance 余数插值（会滞后一固定步）。
     * @return 无；父矩阵奇异时抛出异常。只将物理结果写入 Scene，不从节点反向驱动物理。
     */
    void synchronize(Scene &scene, bool interpolate = true);
    /** @brief 读取计数和计时。@return 状态副本。 */
    Physics2DStats statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif // RENDERER_MODULES_PHYSICS2D_PHYSICS2D_HPP_INCLUDED
