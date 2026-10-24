#include "renderer/animation/sprite_animation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace renderer {
struct SpriteAnimationClip::Impl {
    SpriteAnimationClipDesc description;
    std::vector<double> starts;
    double duration = 0;
};
SpriteAnimationClip::SpriteAnimationClip(const SpriteAnimationClipDesc &description) {
    const auto count = description.directions.size();
    if ((count != 1 && count != 4 && count != 8) || description.directions[0].empty())
        throw std::invalid_argument("Sprite animation needs 1, 4 or 8 nonempty directions");
    auto data = std::make_shared<Impl>();
    data->description = description;
    for (const auto &frame : description.directions[0]) {
        if (!frame.sprite || !std::isfinite(frame.duration) || frame.duration <= 0)
            throw std::invalid_argument("Invalid animation sprite or frame duration");
        data->starts.push_back(data->duration);
        const double end = data->duration + frame.duration;
        if (!std::isfinite(end) || end <= data->duration)
            throw std::invalid_argument("Animation duration overflow or lost precision");
        data->duration = end;
    }
    for (const auto &direction : description.directions) {
        if (direction.size() != data->starts.size())
            throw std::invalid_argument("Animation direction frame counts differ");
        for (size_t i = 0; i < direction.size(); ++i)
            if (!direction[i].sprite || direction[i].sprite.owner != direction[0].sprite.owner ||
                direction[i].sprite.owner != description.directions[0][0].sprite.owner ||
                direction[i].duration != description.directions[0][i].duration ||
                direction[i].event != description.directions[0][i].event)
                throw std::invalid_argument("Direction timelines or asset owners differ");
    }
    impl_ = std::move(data);
}
SpriteAnimationClip::~SpriteAnimationClip() = default;
SpriteAnimationClip::SpriteAnimationClip(const SpriteAnimationClip &) = default;
SpriteAnimationClip &SpriteAnimationClip::operator=(const SpriteAnimationClip &) = default;
SpriteAnimationClip::SpriteAnimationClip(SpriteAnimationClip &&) noexcept = default;
SpriteAnimationClip &SpriteAnimationClip::operator=(SpriteAnimationClip &&) noexcept = default;
double SpriteAnimationClip::duration() const {
    return impl_ ? impl_->duration : 0;
}
size_t SpriteAnimationClip::directionCount() const {
    return impl_ ? impl_->description.directions.size() : 0;
}
size_t SpriteAnimationClip::frameCount() const {
    return impl_ ? impl_->starts.size() : 0;
}
struct SpriteAnimator::Impl {
    std::shared_ptr<const SpriteAnimationClip::Impl> clip;
    double phase = 0, speed = 1;
    size_t direction = 0, frame = 0;
    uint64_t cycle = 0;
    bool loop = true, paused = false, finished = false;
    std::vector<SpriteAnimationEvent> events;
};
SpriteAnimator::SpriteAnimator() : impl_(std::make_unique<Impl>()) {}
SpriteAnimator::~SpriteAnimator() = default;
SpriteAnimator::SpriteAnimator(SpriteAnimator &&) noexcept = default;
SpriteAnimator &SpriteAnimator::operator=(SpriteAnimator &&) noexcept = default;
void SpriteAnimator::play(const SpriteAnimationClip &clip, bool loop) {
    if (!clip.impl_)
        throw std::invalid_argument("Cannot play a moved-from animation clip");
    std::vector<SpriteAnimationEvent> events;
    const auto &event = clip.impl_->description.directions[0][0].event;
    if (!event.empty())
        events.push_back({event, 0, 0});
    impl_->clip = clip.impl_;
    impl_->phase = 0;
    impl_->frame = impl_->direction = 0;
    impl_->cycle = 0;
    impl_->loop = loop;
    impl_->paused = impl_->finished = false;
    impl_->events = std::move(events);
}
void SpriteAnimator::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(seconds * impl_->speed))
        throw std::invalid_argument("Invalid animation delta time");
    if (!impl_->clip || impl_->paused || impl_->finished || seconds * impl_->speed == 0)
        return;
    const auto &clip = *impl_->clip;
    const double end = impl_->phase + seconds * impl_->speed;
    if (!std::isfinite(end))
        throw std::overflow_error("Animation time overflow");
    const double target = impl_->loop ? end : std::min(end, clip.duration);
    const double loopCount = impl_->loop ? std::floor(target / clip.duration) : 0;
    // 限制到双精度可精确表示的循环序号，避免整型转换或事件时间比较失真。
    if (loopCount > 9007199254740991.0 ||
        loopCount > double(std::numeric_limits<uint64_t>::max() - impl_->cycle))
        throw std::overflow_error("Animation cycle overflow");
    struct Pending {
        double time;
        SpriteAnimationEvent event;
    };
    std::vector<Pending> pending;
    const auto &frames = clip.description.directions[0];
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].event.empty())
            continue;
        const double start = clip.starts[i];
        const double first =
            impl_->loop ? std::max(0.0, std::floor((impl_->phase - start) / clip.duration) + 1) : 0;
        const double last = impl_->loop ? std::floor((target - start) / clip.duration) : 0;
        if (!impl_->loop && !(start > impl_->phase && start <= target))
            continue;
        if (last < first)
            continue;
        const double count = last - first + 1;
        if (count > 4096 || impl_->events.size() + pending.size() + size_t(count) > 4096)
            throw std::length_error("Take animation events more frequently or reduce delta time");
        for (uint64_t k = uint64_t(first); k <= uint64_t(last); ++k)
            pending.push_back(
                {start + double(k) * clip.duration, {frames[i].event, i, impl_->cycle + k}});
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [](const auto &a, const auto &b) { return a.time < b.time; });
    auto events = impl_->events;
    events.reserve(events.size() + pending.size());
    for (auto &event : pending)
        events.push_back(std::move(event.event));
    const double phase = impl_->loop ? std::fmod(target, clip.duration) : target;
    const auto position = std::upper_bound(clip.starts.begin(), clip.starts.end(), phase);
    impl_->frame = std::min(size_t(position - clip.starts.begin() - 1), frames.size() - 1);
    impl_->phase = phase;
    impl_->cycle += uint64_t(loopCount);
    impl_->finished = !impl_->loop && target >= clip.duration;
    impl_->events = std::move(events);
}
void SpriteAnimator::selectDirection(Vec3 facing, const Mat4 &cameraWorld) {
    facing.y = 0;
    if (!finite(facing) || dot(facing, facing) < 1e-12f || !cameraWorld.isAffine() ||
        !cameraWorld.inverse())
        throw std::invalid_argument(
            "Direction selection needs a ground direction and valid camera");
    facing = normalized(facing);
    Vec3 front{cameraWorld(0, 2), 0, cameraWorld(2, 2)};
    front = dot(front, front) > 1e-10f ? normalized(front) : Vec3{0, 0, 1};
    const auto right = cross({0, 1, 0}, front);
    const double angle = std::atan2(double(dot(facing, right)), double(dot(facing, front)));
    const auto count = impl_->clip ? impl_->clip->description.directions.size() : 1;
    const auto index = int64_t(std::llround(angle * double(count) / (2 * std::acos(-1.0))));
    impl_->direction = size_t((index + int64_t(count)) % int64_t(count));
}
void SpriteAnimator::apply(Scene &scene, NodeHandle node) const {
    auto component = scene.component<SpriteRenderer>(node);
    if (!impl_->clip || !component)
        throw std::logic_error("Animator requires a playing clip and SpriteRenderer");
    const auto current = sprite();
    if (component->sprite != current) {
        component->sprite = current;
        scene.setComponent(node, *component);
    }
}
void SpriteAnimator::setSpeed(double speed) {
    if (!std::isfinite(speed) || speed < 0)
        throw std::invalid_argument("Invalid animation speed");
    impl_->speed = speed;
}
void SpriteAnimator::setPaused(bool paused) {
    impl_->paused = paused;
}
SpriteHandle SpriteAnimator::sprite() const {
    return impl_->clip ? impl_->clip->description.directions[impl_->direction][impl_->frame].sprite
                       : SpriteHandle{};
}
size_t SpriteAnimator::frameIndex() const {
    return impl_->frame;
}
size_t SpriteAnimator::directionIndex() const {
    return impl_->direction;
}
bool SpriteAnimator::finished() const {
    return impl_->finished;
}
std::vector<SpriteAnimationEvent> SpriteAnimator::takeEvents() {
    return std::exchange(impl_->events, {});
}
} // namespace renderer
