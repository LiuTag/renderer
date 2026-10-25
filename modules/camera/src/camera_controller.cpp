/** @file @brief 相机姿态计算、指数平滑、设备无关输入和保持父子关系的场景写回。 */
#include <algorithm>
#include <cmath>
#include <renderer/camera/camera_controller.hpp>
#include <stdexcept>
namespace renderer {
namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::invalid_argument(message);
}
bool modeValid(CameraControlMode mode) {
    return mode == CameraControlMode::Orbit || mode == CameraControlMode::Follow ||
           mode == CameraControlMode::Free;
}
bool projectionValid(Camera::Projection projection) {
    return projection == Camera::Projection::Perspective ||
           projection == Camera::Projection::Orthographic;
}
float wrap(double angle) {
    return float(std::remainder(angle, 2 * pi));
}
float length(Vec3 v) {
    return float(std::hypot(double(v.x), double(v.y), double(v.z)));
}
void interval(float low, float high, const char *message) {
    require(std::isfinite(low) && std::isfinite(high) && low > 0 && high >= low, message);
}
void validate(const CameraControllerConfig &c) {
    require(modeValid(c.mode) && projectionValid(c.projection),
            "Invalid camera controller mode/projection");
    require(finite(c.target) && finite(c.position) && finite(c.followOffset) &&
                finite(c.targetOffset),
            "Invalid camera position/offset");
    require(std::isfinite(c.yaw) && std::isfinite(c.pitch), "Invalid camera angles");
    require(std::isfinite(c.minPitch) && std::isfinite(c.maxPitch) && c.minPitch <= c.maxPitch &&
                c.minPitch > float(-pi / 2 + .0001) && c.maxPitch < float(pi / 2 - .0001),
            "Camera pitch limits cross a pole");
    interval(c.minDistance, c.maxDistance, "Invalid camera distance limits");
    interval(c.minOrthographicHeight, c.maxOrthographicHeight,
             "Invalid orthographic height limits");
    interval(c.minVerticalFov, c.maxVerticalFov, "Invalid camera FOV limits");
    require(c.maxVerticalFov < radians(179), "Camera FOV is too large");
    require(std::isfinite(c.distance) && c.distance > 0 && std::isfinite(c.orthographicHeight) &&
                c.orthographicHeight > 0 && std::isfinite(c.verticalFov) && c.verticalFov > 0,
            "Invalid initial camera lens/distance");
    require(std::isfinite(c.moveSpeed) && c.moveSpeed >= 0 && std::isfinite(c.boostMultiplier) &&
                c.boostMultiplier >= 1 && std::isfinite(c.zoomSpeed) && c.zoomSpeed > 0 &&
                std::isfinite(c.smoothing) && c.smoothing >= 0,
            "Invalid camera speed/smoothing");
    if (c.mode == CameraControlMode::Follow)
        require(std::isfinite(length(c.followOffset)) && length(c.followOffset) > 1e-6f,
                "Follow offset must be nonzero");
}
struct Axes {
    Vec3 right, up, back;
};
Axes axes(float yaw, float pitch) {
    const float sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
    return {{cy, 0, -sy}, {-sy * sp, cp, -cy * sp}, {sy * cp, sp, cy * cp}};
}
Mat4 matrix(Vec3 position, const Axes &a) {
    auto result = Mat4::identity();
    for (int i = 0; i < 3; ++i) {
        result(i, 0) = i == 0 ? a.right.x : i == 1 ? a.right.y : a.right.z;
        result(i, 1) = i == 0 ? a.up.x : i == 1 ? a.up.y : a.up.z;
        result(i, 2) = i == 0 ? a.back.x : i == 1 ? a.back.y : a.back.z;
    }
    result(0, 3) = position.x;
    result(1, 3) = position.y;
    result(2, 3) = position.z;
    return result;
}
float zoom(float current, float amount, float speed, float low, float high) {
    // 在 log 空间限幅，极大滚轮输入也不会先 exp 溢出。
    const auto value = std::clamp(std::log(double(current)) + double(amount) * speed,
                                  std::log(double(low)), std::log(double(high)));
    return std::clamp(float(std::exp(value)), low, high);
}
float mix(float a, float b, double t) {
    return float(double(a) + (double(b) - a) * t);
}
Vec3 mix(Vec3 a, Vec3 b, double t) {
    return {mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)};
}
} // namespace
struct CameraController::Impl {
    CameraControllerConfig config;
    CameraControllerState current;
    Vec3 panOffset{};
    explicit Impl(CameraControllerConfig c) : config(c) {
        validate(config);
        config.yaw = wrap(config.yaw);
        config.pitch = std::clamp(config.pitch, config.minPitch, config.maxPitch);
        if (config.mode == CameraControlMode::Follow) {
            config.distance = length(config.followOffset);
            config.yaw = std::atan2(config.followOffset.x, config.followOffset.z);
            config.pitch =
                std::clamp(std::atan2(config.followOffset.y,
                                      std::hypot(config.followOffset.x, config.followOffset.z)),
                           config.minPitch, config.maxPitch);
        }
        config.distance = std::clamp(config.distance, config.minDistance, config.maxDistance);
        config.orthographicHeight = std::clamp(
            config.orthographicHeight, config.minOrthographicHeight, config.maxOrthographicHeight);
        config.verticalFov =
            std::clamp(config.verticalFov, config.minVerticalFov, config.maxVerticalFov);
        blend(1);
    }
    Vec3 target() const {
        return config.target + panOffset +
               (config.mode == CameraControlMode::Follow ? config.targetOffset : Vec3{});
    }
    void blend(double amount) {
        auto &s = current;
        s.mode = config.mode;
        s.projection = config.projection;
        s.yaw = wrap(double(s.yaw) + std::remainder(double(config.yaw) - s.yaw, 2 * pi) * amount);
        s.pitch = mix(s.pitch, config.pitch, amount);
        s.distance = mix(s.distance, config.distance, amount);
        s.orthographicHeight = mix(s.orthographicHeight, config.orthographicHeight, amount);
        s.verticalFov = mix(s.verticalFov, config.verticalFov, amount);
        const auto a = axes(s.yaw, s.pitch);
        if (config.mode == CameraControlMode::Free) {
            s.position = mix(s.position, config.position, amount);
            s.target = s.position - a.back * s.distance;
        } else {
            s.target = mix(s.target, target(), amount);
            s.position = s.target + a.back * s.distance;
        }
        s.world = matrix(s.position, a);
        require(finite(s.position) && finite(s.target) && s.world.isFinite(),
                "Camera result overflow");
    }
    void advance(double seconds, const CameraControlInput &input) {
        config.yaw = wrap(double(config.yaw) + input.rotation.x);
        config.pitch = float(std::clamp(double(config.pitch) + input.rotation.y,
                                        double(config.minPitch), double(config.maxPitch)));
        const auto a = axes(config.yaw, config.pitch);
        if (config.mode == CameraControlMode::Free) {
            const auto n = length(input.movement);
            const auto direction = n > 1 ? input.movement * (1 / n) : input.movement;
            const auto up = config.groundMovement ? Vec3{0, 1, 0} : a.up;
            const auto forward = config.groundMovement
                                     ? Vec3{-std::sin(config.yaw), 0, -std::cos(config.yaw)}
                                     : a.back * (-1);
            const double speed =
                double(config.moveSpeed) * (input.boost ? config.boostMultiplier : 1) * seconds;
            require(std::isfinite(speed) && speed <= 1e30, "Camera movement overflow");
            config.position =
                config.position +
                (a.right * direction.x + up * direction.y + forward * direction.z) * float(speed);
        } else
            panOffset = panOffset + a.right * input.pan.x + a.up * input.pan.y;
        if (config.projection == Camera::Projection::Orthographic)
            config.orthographicHeight =
                zoom(config.orthographicHeight, input.zoom, config.zoomSpeed,
                     config.minOrthographicHeight, config.maxOrthographicHeight);
        else if (config.mode == CameraControlMode::Free)
            config.verticalFov = zoom(config.verticalFov, input.zoom, config.zoomSpeed,
                                      config.minVerticalFov, config.maxVerticalFov);
        else
            config.distance = zoom(config.distance, input.zoom, config.zoomSpeed,
                                   config.minDistance, config.maxDistance);
        require(finite(config.position) && finite(panOffset) && finite(target()),
                "Camera movement/pan overflow");
        const double amount =
            config.smoothing == 0 ? 1 : -std::expm1(-double(config.smoothing) * seconds);
        blend(amount);
    }
};
CameraController::CameraController(const CameraControllerConfig &config)
    : impl_(std::make_unique<Impl>(config)) {}
CameraController::~CameraController() = default;
CameraController::CameraController(CameraController &&) noexcept = default;
CameraController &CameraController::operator=(CameraController &&) noexcept = default;
const CameraController::Impl &CameraController::checked() const {
    if (!impl_)
        throw std::logic_error("CameraController has been moved from");
    return *impl_;
}
void CameraController::reset(const CameraControllerConfig &config) {
    impl_ = std::make_unique<Impl>(config);
}
void CameraController::setTarget(Vec3 target, bool teleport) {
    require(finite(target), "Camera target must be finite");
    auto next = checked();
    next.config.target = target;
    require(finite(next.target()), "Camera target overflow");
    if (teleport && next.config.mode != CameraControlMode::Free)
        next.blend(1);
    *impl_ = next;
}
void CameraController::setMode(CameraControlMode mode) {
    require(modeValid(mode), "Invalid camera control mode");
    auto next = checked();
    if (next.config.mode == mode)
        return;
    const auto s = next.current;
    next.config.mode = mode;
    next.panOffset = {};
    next.config.position = s.position;
    next.config.yaw = s.yaw;
    next.config.pitch = s.pitch;
    next.config.distance = s.distance;
    next.config.orthographicHeight = s.orthographicHeight;
    next.config.verticalFov = s.verticalFov;
    next.config.target =
        s.target - (mode == CameraControlMode::Follow ? next.config.targetOffset : Vec3{});
    next.config.followOffset = s.position - s.target;
    next.current.mode = mode;
    *impl_ = next;
}
void CameraController::setProjection(Camera::Projection projection) {
    require(projectionValid(projection), "Invalid camera projection");
    checked();
    impl_->config.projection = projection;
    impl_->current.projection = projection;
}
void CameraController::update(double seconds, const CameraControlInput &input) {
    require(std::isfinite(seconds) && seconds >= 0 && seconds <= 4,
            "Camera time step must be in [0,4]");
    require(std::isfinite(input.rotation.x) && std::isfinite(input.rotation.y) &&
                std::isfinite(input.pan.x) && std::isfinite(input.pan.y) &&
                std::isfinite(input.zoom) && finite(input.movement),
            "Invalid camera input");
    require(std::abs(input.movement.x) <= 1 && std::abs(input.movement.y) <= 1 &&
                std::abs(input.movement.z) <= 1,
            "Camera movement input must be in [-1,1]");
    auto next = checked();
    next.advance(seconds, input);
    *impl_ = next;
}
CameraControllerState CameraController::state() const {
    return checked().current;
}
void CameraController::apply(Scene &scene, NodeHandle node) const {
    const auto &s = checked().current;
    require(scene.valid(node) && node != scene.root(),
            "Camera controller requires a valid non-root camera node");
    auto camera = scene.component<Camera>(node);
    require(bool(camera), "Camera controller node has no Camera component");
    auto local = s.world;
    if (const auto parent = scene.parent(node)) {
        const auto inverse = scene.worldTransform(parent).inverse();
        require(bool(inverse), "Camera parent transform is singular");
        local = *inverse * s.world;
    }
    require(local.isAffine(), "Camera local transform is invalid");
    camera->projection = s.projection;
    camera->orthographicHeight = s.orthographicHeight;
    camera->verticalFov = s.verticalFov;
    // 在写 Scene 之前检查旧近远面是否兼容新投影，避免只写了一半姿态。
    const auto aspect = camera->aspectRatio > 0 ? camera->aspectRatio : 1.f;
    if (camera->projection == Camera::Projection::Perspective)
        Mat4::perspective(camera->verticalFov, aspect, camera->nearPlane, camera->farPlane);
    else {
        const auto half = camera->orthographicHeight * .5f;
        Mat4::orthographic(-half * aspect, half * aspect, -half, half, camera->nearPlane,
                           camera->farPlane);
    }
    scene.setLocalTransform(node, local);
    scene.setComponent(node, *camera);
}
} // namespace renderer
