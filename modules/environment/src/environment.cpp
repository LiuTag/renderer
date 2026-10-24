/** @file @brief 持续昼夜周期、连续天气强度及公共 Scene/RenderView 的表现更新。 */
#include <algorithm>
#include <cmath>
#include <renderer/environment/environment.hpp>
#include <stdexcept>
namespace renderer {
struct EnvironmentController::Impl {
    EnvironmentConfig config;
    double hour = 12;
    Weather weather = Weather::Clear;
    float precipitation = 0, start = 0, target = 0;
    double elapsed = 0;
};
EnvironmentController::EnvironmentController(const EnvironmentConfig &c)
    : impl_(std::make_unique<Impl>()) {
    if (!std::isfinite(c.dayLength) || c.dayLength <= 0 || !std::isfinite(c.hour) ||
        !finite(c.dayColor) || !finite(c.nightColor) || !finite(c.wind))
        throw std::invalid_argument("Invalid environment configuration");
    for (float v : {c.dayAmbient, c.nightAmbient, c.sunIntensity, c.weatherTransition, c.dayColor.x,
                    c.dayColor.y, c.dayColor.z, c.nightColor.x, c.nightColor.y, c.nightColor.z})
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid environment color/intensity");
    if (c.dayAmbient > 1 || c.nightAmbient > 1)
        throw std::invalid_argument("Ambient light must be in [0,1]");
    impl_->config = c;
    setHour(c.hour);
}
EnvironmentController::~EnvironmentController() = default;
EnvironmentController::EnvironmentController(EnvironmentController &&) noexcept = default;
EnvironmentController &
EnvironmentController::operator=(EnvironmentController &&) noexcept = default;
void EnvironmentController::setHour(double hour) {
    if (!std::isfinite(hour))
        throw std::invalid_argument("Invalid hour");
    impl_->hour = std::fmod(hour, 24);
    if (impl_->hour < 0)
        impl_->hour += 24;
}
void EnvironmentController::setWeather(Weather weather) {
    if (weather != Weather::Clear && weather != Weather::Rain && weather != Weather::Snow)
        throw std::invalid_argument("Invalid weather");
    if (impl_->weather == weather)
        return;
    impl_->weather = weather;
    impl_->start = impl_->precipitation;
    impl_->target = weather == Weather::Clear ? 0.f : 1.f;
    impl_->elapsed = 0;
    if (impl_->config.weatherTransition == 0)
        impl_->precipitation = impl_->target;
}
void EnvironmentController::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Invalid environment delta");
    setHour(impl_->hour +
            std::fmod(seconds, impl_->config.dayLength) / impl_->config.dayLength * 24);
    impl_->elapsed = std::min(impl_->elapsed + seconds, double(impl_->config.weatherTransition));
    const float t = impl_->config.weatherTransition > 0
                        ? float(impl_->elapsed / impl_->config.weatherTransition)
                        : 1;
    impl_->precipitation = impl_->start + (impl_->target - impl_->start) * t;
}
EnvironmentState EnvironmentController::state() const {
    const auto &p = *impl_;
    const float angle = radians(float(p.hour - 6) * 15), sun = std::sin(angle);
    const float t = std::clamp((sun + .05f) / .4f, 0.f, 1.f);
    EnvironmentState s;
    s.hour = p.hour;
    s.lightDirection = normalized({-.3f, -sun, -.4f});
    s.lightIntensity = p.config.sunIntensity * std::max(0.f, sun);
    s.lightColor = {1, .92f, .8f};
    s.ambient = p.config.nightAmbient + (p.config.dayAmbient - p.config.nightAmbient) * t;
    s.colorMultiply = p.config.nightColor + (p.config.dayColor - p.config.nightColor) * t;
    s.wind = p.config.wind;
    s.precipitation = p.precipitation;
    s.weather = p.weather;
    return s;
}
void EnvironmentController::apply(Scene &scene, NodeHandle sun, RenderView &view) const {
    auto s = state();
    const Mat4 world =
        *Mat4::lookAt({0, 0, 0}, s.lightDirection,
                      std::abs(s.lightDirection.y) > .95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0})
             .inverse();
    Mat4 local = world;
    if (const auto parent = scene.parent(sun)) {
        const auto inverse = scene.worldTransform(parent).inverse();
        if (!inverse)
            throw std::invalid_argument("Sun parent transform is singular");
        local = *inverse * world;
    }
    auto light = scene.component<Light>(sun).value_or(Light{});
    light.type = LightType::Directional;
    light.color = s.lightColor;
    light.intensity = s.lightIntensity;
    scene.setComponent(sun, light);
    scene.setLocalTransform(sun, local);
    view.ambientLight = s.ambient;
    view.colorMultiply = s.colorMultiply;
}
} // namespace renderer
