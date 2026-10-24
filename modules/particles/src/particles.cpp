/** @file @brief 固定时间步粒子、确定性随机发射及颜色/尺寸插值，保持模块独立。 */
#include <algorithm>
#include <cmath>
#include <random>
#include <renderer/particles/particles.hpp>
#include <stdexcept>
namespace renderer {
namespace {
void rateValid(float r) {
    if (!std::isfinite(r) || r < 0 || r > 100000)
        throw std::invalid_argument("Invalid particle rate");
}
void validate(const ParticleEmitterDesc &d) {
    rateValid(d.rate);
    if (!d.sprite || !d.capacity || d.capacity > 1000000 || !finite(d.origin) ||
        !finite(d.spread) || !finite(d.velocityMin) || !finite(d.velocityMax) ||
        !finite(d.acceleration) || d.spread.x < 0 || d.spread.y < 0 || d.spread.z < 0 ||
        d.velocityMin.x > d.velocityMax.x || d.velocityMin.y > d.velocityMax.y ||
        d.velocityMin.z > d.velocityMax.z || !std::isfinite(d.minLifetime) ||
        !std::isfinite(d.maxLifetime) || d.minLifetime <= 0 || d.maxLifetime < d.minLifetime ||
        d.maxLifetime > 600 || !std::isfinite(d.startSize) || !std::isfinite(d.endSize) ||
        d.startSize <= 0 || d.endSize <= 0 ||
        (d.billboard != BillboardMode::Fixed && d.billboard != BillboardMode::Upright &&
         d.billboard != BillboardMode::ViewPlane) ||
        (d.transparency != SpriteTransparency::Sorted &&
         d.transparency != SpriteTransparency::FollowView))
        throw std::invalid_argument("Invalid particle emitter");
    for (auto c : {d.startColor, d.endColor}) {
        for (float v : {c.x, c.y, c.z, c.w})
            if (!std::isfinite(v) || v < 0)
                throw std::invalid_argument("Invalid particle color");
        if (c.w > 1)
            throw std::invalid_argument("Invalid particle alpha");
    }
}
} // namespace
ParticleEmitterDesc particlePreset(ParticlePreset p, SpriteHandle sprite) {
    ParticleEmitterDesc d;
    d.sprite = sprite;
    switch (p) {
    case ParticlePreset::Fire:
        d.startColor = {3, 1, .1f, .9f};
        d.endColor = {.7f, .05f, 0, 0};
        break;
    case ParticlePreset::Smoke:
        d.rate = 18;
        d.minLifetime = 2;
        d.maxLifetime = 4;
        d.startSize = .2f;
        d.endSize = 1.4f;
        d.velocityMin = {-.1f, .3f, -.1f};
        d.velocityMax = {.1f, .8f, .1f};
        d.startColor = {.3f, .3f, .3f, .5f};
        d.endColor = {.5f, .5f, .5f, 0};
        break;
    case ParticlePreset::Rain:
        d.rate = 300;
        d.minLifetime = .6f;
        d.maxLifetime = 1;
        d.spread = {8, 0, 8};
        d.velocityMin = {-.8f, -14, 0};
        d.velocityMax = {-.4f, -10, .1f};
        d.startSize = .1f;
        d.endSize = .1f;
        d.startColor = {.6f, .75f, 1, .7f};
        break;
    case ParticlePreset::Snow:
        d.rate = 100;
        d.minLifetime = 3;
        d.maxLifetime = 5;
        d.spread = {8, 0, 8};
        d.velocityMin = {-.3f, -2, -.2f};
        d.velocityMax = {.3f, -1, .2f};
        d.startSize = .1f;
        d.endSize = .08f;
        break;
    default:
        throw std::invalid_argument("Invalid particle preset");
    }
    return d;
}
struct ParticleSystem::Impl {
    ParticleEmitterDesc desc;
    std::mt19937 random;
    std::vector<ParticleState> live;
    bool emitting = true;
    double remainder = 0, emission = 0;
    uint64_t next = 1;
    explicit Impl(const ParticleEmitterDesc &d) : desc(d), random(d.seed) {}
    float value(float lo, float hi) {
        return std::lerp(lo, hi, float(random() >> 8) * (1.f / 16777216.f));
    }
};
ParticleSystem::ParticleSystem(const ParticleEmitterDesc &d) {
    validate(d);
    impl_ = std::make_unique<Impl>(d);
    impl_->live.reserve(d.capacity);
}
ParticleSystem::~ParticleSystem() = default;
ParticleSystem::ParticleSystem(ParticleSystem &&) noexcept = default;
ParticleSystem &ParticleSystem::operator=(ParticleSystem &&) noexcept = default;
uint32_t ParticleSystem::burst(uint32_t n) {
    auto &p = *impl_;
    n = std::min(n, p.desc.capacity - uint32_t(p.live.size()));
    for (uint32_t i = 0; i < n; ++i) {
        const auto &d = p.desc;
        ParticleState s;
        s.position =
            d.origin + Vec3{p.value(-d.spread.x, d.spread.x), p.value(-d.spread.y, d.spread.y),
                            p.value(-d.spread.z, d.spread.z)};
        s.velocity = {p.value(d.velocityMin.x, d.velocityMax.x),
                      p.value(d.velocityMin.y, d.velocityMax.y),
                      p.value(d.velocityMin.z, d.velocityMax.z)};
        s.lifetime = p.value(d.minLifetime, d.maxLifetime);
        s.rotation = p.value(-3.14159f, 3.14159f);
        s.id = p.next++;
        if (!finite(s.position) || !finite(s.velocity))
            throw std::overflow_error("Particle emission overflow");
        p.live.push_back(s);
    }
    return n;
}
void ParticleSystem::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 4)
        throw std::invalid_argument("Particle delta must be in [0,4]");
    auto &p = *impl_;
    p.remainder += seconds;
    constexpr double step = 1.0 / 60;
    while (p.remainder + 1e-12 >= step) {
        p.remainder -= step;
        for (auto &s : p.live) {
            s.age += float(step);
            s.velocity = s.velocity + p.desc.acceleration * float(step);
            s.position = s.position + s.velocity * float(step);
        }
        // 极端参数导致溢出的单个粒子直接回收，不能把 NaN/Inf 提交到渲染批次。
        std::erase_if(p.live, [](const auto &s) {
            return s.age >= s.lifetime || !finite(s.position) || !finite(s.velocity);
        });
        if (p.emitting) {
            p.emission += p.desc.rate * step;
            const auto n = uint32_t(std::floor(p.emission + 1e-9));
            p.emission -= n;
            burst(n);
        }
    }
}
void ParticleSystem::setEmitting(bool e) {
    impl_->emitting = e;
}
void ParticleSystem::setOrigin(Vec3 p) {
    if (!finite(p))
        throw std::invalid_argument("Invalid emitter origin");
    impl_->desc.origin = p;
}
void ParticleSystem::setRate(float r) {
    rateValid(r);
    impl_->desc.rate = r;
}
void ParticleSystem::clear() {
    impl_->live.clear();
    impl_->remainder = impl_->emission = 0;
}
std::span<const ParticleState> ParticleSystem::particles() const {
    return impl_->live;
}
SpriteBatch ParticleSystem::batch(const AssetStore &assets) const {
    std::vector<BatchSprite> data;
    data.reserve(impl_->live.size());
    const auto &d = impl_->desc;
    for (const auto &p : impl_->live) {
        const float t = std::clamp(p.age / p.lifetime, 0.f, 1.f);
        const auto lerp = [t](float a, float b) { return a + (b - a) * t; };
        SpriteRenderer r;
        r.sprite = d.sprite;
        r.billboard = d.billboard;
        r.transparency = d.transparency;
        r.rotation = p.rotation;
        const float size = lerp(d.startSize, d.endSize);
        r.sizeMultiplier = {size, size};
        r.tint = {lerp(d.startColor.x, d.endColor.x), lerp(d.startColor.y, d.endColor.y),
                  lerp(d.startColor.z, d.endColor.z), lerp(d.startColor.w, d.endColor.w)};
        data.push_back({r, Mat4::translation(p.position), p.id});
    }
    SpriteBatch batch;
    batch.replace(assets, data);
    return batch;
}
} // namespace renderer
