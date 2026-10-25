/** @file @brief 独立相机控制模块：轨道、偏移跟随、自由移动、平滑和 Scene 写回，不依赖窗口。 */
#ifndef RENDERER_MODULES_CAMERA_CAMERA_CONTROLLER_HPP_INCLUDED
#define RENDERER_MODULES_CAMERA_CAMERA_CONTROLLER_HPP_INCLUDED
#include <memory>
#include <renderer/scene.hpp>
namespace renderer {
/** @brief Orbit 围绕观察点，Follow 用世界偏移跟随目标，Free 按相机方向自由移动。 */
enum class CameraControlMode { Orbit, Follow, Free };
/** @brief 控制器初始状态和限制；角度为弧度，位置/距离为世界单位。 */
struct CameraControllerConfig {
    CameraControlMode mode = CameraControlMode::Orbit;               ///< 初始控制模式。
    Camera::Projection projection = Camera::Projection::Perspective; ///< 控制器拥有的投影类型。
    Vec3 target{};                      ///< Orbit/Follow 的世界目标，由应用提供。
    Vec3 position{0, 2, 8};             ///< Free 模式的初始世界位置。
    Vec3 followOffset{0, 6, 10};        ///< Follow 初始眼睛相对观察点的世界偏移，不能为零。
    Vec3 targetOffset{};                ///< Follow 在目标世界位置上附加的观察点偏移。
    float yaw = 0, pitch = radians(25); ///< Orbit/Free 初始角度；yaw=0 面向 -Z，pitch 正值俯视。
    float distance = 10, minDistance = .1f, maxDistance = 1000; ///< 观察距离及正值上下限。
    float minPitch = radians(-85), maxPitch = radians(85);      ///< 必须严格位于 (-pi/2, pi/2)。
    float orthographicHeight = 10, minOrthographicHeight = .1f,
          maxOrthographicHeight = 1000; ///< 正交视口世界高度及正值上下限。
    float verticalFov = radians(60), minVerticalFov = radians(10),
          maxVerticalFov = radians(120); ///< 透视垂直视场角及弧度上下限，最大值小于 179 度。
    float moveSpeed = 5,
          boostMultiplier = 4;   ///< Free 每秒速度、加速倍率；速度可为零，倍率至少为 1。
    float zoomSpeed = .1f;       ///< 每单位 zoom 输入的指数缩放系数，必须为正。
    float smoothing = 0;         ///< 每秒指数响应，0 立即应用；例如 8 表示平滑跟随。
    bool groundMovement = false; ///< Free 前进限制在 XZ，竖直移动使用世界 Y；否则使用相机轴。
};
/** @brief 一次更新的设备无关输入，应用负责把键鼠/手柄事件映射到这些量。 */
struct CameraControlInput {
    Vec2 rotation{}; ///< yaw/pitch 弧度增量，已经包含灵敏度/按键 dt，不会再次乘时间。
    Vec2 pan{};      ///< Orbit/Follow 沿相机右/上方向的世界位移增量；Free 忽略。
    Vec3 movement{}; ///< Free 的右/上/前方向，每分量 [-1,1]；总长度大于 1 时归一化，再乘速度与 dt。
    float zoom = 0;  ///< 正值拉远，负值拉近；正交改高度，透视轨道/跟随改距离，透视 Free 改 FOV。
    bool boost = false; ///< Free 是否使用加速倍率。
};
/** @brief 当前平滑后的姿态和镜头值；world 为相机到世界的仿射矩阵，可供 Billboard/监听器使用。 */
struct CameraControllerState {
    CameraControlMode mode = CameraControlMode::Orbit;               ///< 当前控制模式。
    Camera::Projection projection = Camera::Projection::Perspective; ///< 当前投影类型。
    Vec3 position{}, target{}; ///< 世界视点/观察点；Free 的观察点由视线和 distance 推导。
    float yaw = 0, pitch = 0;  ///< 当前偏航/俯仰弧度。
    float distance = 10;       ///< 视点到观察点的距离，Free 中只用于推导观察点。
    float orthographicHeight = 10, verticalFov = radians(60); ///< 正交世界高度和透视垂直视场弧度。
    Mat4 world = Mat4::identity();                            ///< 无缩放的相机到世界矩阵。
};
/** @brief PImpl 相机操作器；只依赖 core，不订阅输入、不持有 Scene/Window，按单写线程使用。 */
class CameraController {
  public:
    /** @brief 创建并立即计算初始姿态。
     * @param config 模式、初始姿态、镜头与有限限制。@return 无；无效配置抛出异常。
     */
    explicit CameraController(const CameraControllerConfig &config = {});
    /** @brief 释放内部控制状态。@return 无。 */
    ~CameraController();
    /** @brief 转移状态。@param other 源对象；之后仅可析构或转移赋值。@return 无。 */
    CameraController(CameraController &&other) noexcept;
    /** @brief 转移赋值。@param other 源对象。@return 当前对象。 */
    CameraController &operator=(CameraController &&other) noexcept;
    /** @brief 禁止复制可变控制状态。@param other 源对象。@return 无；已删除。 */
    CameraController(const CameraController &other) = delete;
    /** @brief 禁止复制可变控制状态。@param other 源对象。@return 无；已删除。 */
    CameraController &operator=(const CameraController &other) = delete;
    /** @brief 用配置重置目标和当前状态，清除平移偏移及平滑历史。
     * @param config 新配置。@return 无；无效配置不改变原状态。
     */
    void reset(const CameraControllerConfig &config = {});
    /** @brief 设置 Orbit/Follow 的世界目标，Free 保留此值但不用于当前运动。
     * @param target 有限世界坐标。@param teleport true 立即重算姿态，适合传送，false 在 update
     * 时生效。
     * @return 无；失败不改变状态。
     */
    void setTarget(Vec3 target, bool teleport = false);
    /** @brief 切换控制模式并保留当前视点/朝向，清除待完成平滑。
     * @param mode 新模式。@return 无；Follow 的偏移改为当前视点相对观察点的偏移。
     */
    void setMode(CameraControlMode mode);
    /** @brief 设置控制器拥有的投影类型，保留位置和镜头数值。
     * @param projection 透视或正交。@return 无；随后 apply 同步 Camera 组件。
     */
    void setProjection(Camera::Projection projection);
    /** @brief 处理输入并推进平滑，设备增量与持续移动分开计时。
     * @param seconds [0,4] 内有限秒数；0 不推进移动/平滑，立即模式仍处理增量。
     * @param input 有限旋转/平移/缩放及范围合法的移动方向。@return 无；失败不改变状态。
     */
    void update(double seconds, const CameraControlInput &input = {});
    /** @brief 读取当前姿态，不推进时间。@return 已平滑的世界矩阵、观察点、角度和镜头值。 */
    CameraControllerState state() const;
    /** @brief 把当前世界姿态和镜头写入场景，保留近远面、宽高比和剔除掩码。
     * @param scene 相机所属场景。@param camera 含 Camera 组件的有效非根节点。
     * @return 无；处理父变换，缺少组件、奇异父矩阵或投影不兼容时抛出异常且不写入。
     */
    void apply(Scene &scene, NodeHandle camera) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    const Impl &checked() const;
};
} // namespace renderer
#endif // RENDERER_MODULES_CAMERA_CAMERA_CONTROLLER_HPP_INCLUDED
