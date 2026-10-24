/** @file @brief 空间网格宽阶段、圆形重叠解穿透、连续扫掠及切向滑动实现。 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <renderer/collision/collision.hpp>
#include <set>
#include <stdexcept>
namespace renderer {
namespace {
bool finite2(Vec2 p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}
Vec2 vadd(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}
Vec2 sub(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}
Vec2 mul(Vec2 a, float v) {
    return {a.x * v, a.y * v};
}
float dot2(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}
void circle(Vec2 p, float r) {
    if (!finite2(p) || !std::isfinite(r) || r <= 0)
        throw std::invalid_argument("Invalid collision circle");
}
void validate(const ColliderDesc &d) {
    if (!finite2(d.center) || (d.shape != ColliderShape::Circle && d.shape != ColliderShape::Box))
        throw std::invalid_argument("Invalid collider");
    if (d.shape == ColliderShape::Circle)
        circle(d.center, d.radius);
    else if (!finite2(d.halfExtents) || d.halfExtents.x <= 0 || d.halfExtents.y <= 0)
        throw std::invalid_argument("Invalid box extents");
}
Vec2 penetration(Vec2 p, float r, const ColliderDesc &d) {
    if (d.shape == ColliderShape::Circle) {
        auto v = sub(p, d.center);
        const float dist = std::sqrt(dot2(v, v)), depth = r + d.radius - dist;
        if (depth <= 0)
            return {};
        return dist > 1e-7f ? mul(v, depth / dist) : Vec2{depth, 0};
    }
    const auto v = sub(p, d.center);
    Vec2 q{std::clamp(v.x, -d.halfExtents.x, d.halfExtents.x),
           std::clamp(v.y, -d.halfExtents.y, d.halfExtents.y)};
    const auto difference = sub(v, q);
    const float dist = std::sqrt(dot2(difference, difference));
    if (dist > 1e-7f)
        return dist < r ? mul(difference, (r - dist) / dist) : Vec2{};
    const float x = d.halfExtents.x - std::abs(v.x), y = d.halfExtents.y - std::abs(v.y);
    return x < y ? Vec2{(v.x >= 0 ? 1.f : -1.f) * (x + r), 0}
                 : Vec2{0, (v.y >= 0 ? 1.f : -1.f) * (y + r)};
}
bool overlap(Vec2 p, float r, const ColliderDesc &d) {
    if (d.shape == ColliderShape::Circle) {
        auto v = sub(p, d.center);
        return dot2(v, v) <= (r + d.radius) * (r + d.radius);
    }
    Vec2 v{std::max(0.f, std::abs(p.x - d.center.x) - d.halfExtents.x),
           std::max(0.f, std::abs(p.y - d.center.y) - d.halfExtents.y)};
    return dot2(v, v) <= r * r;
}
struct Sweep {
    float time = 2;
    Vec2 normal;
};
Sweep sweep(Vec2 p, float r, Vec2 delta, const ColliderDesc &d) {
    if (d.shape == ColliderShape::Circle) {
        auto v = sub(p, d.center);
        const float a = dot2(delta, delta), b = dot2(v, delta),
                    c = dot2(v, v) - (r + d.radius) * (r + d.radius), disc = b * b - a * c;
        if (a <= 1e-12f || b >= 0 || disc < 0)
            return {};
        const float t = (-b - std::sqrt(disc)) / a;
        if (t < -.0001f || t > 1)
            return {};
        const auto n = vadd(v, mul(delta, std::max(0.f, t)));
        return {std::max(0.f, t), mul(n, 1 / std::sqrt(dot2(n, n)))};
    }
    const auto v = sub(p, d.center);
    const Vec2 extent{d.halfExtents.x + r, d.halfExtents.y + r};
    float enter = -1e30f, exit = 1e30f;
    Vec2 normal;
    for (int axis = 0; axis < 2; ++axis) {
        const float position = axis ? v.y : v.x, velocity = axis ? delta.y : delta.x,
                    e = axis ? extent.y : extent.x;
        if (std::abs(velocity) < 1e-9f) {
            if (position < -e || position > e)
                return {};
            continue;
        }
        float lo = (-e - position) / velocity, hi = (e - position) / velocity;
        Vec2 n = axis ? Vec2{0, -1} : Vec2{-1, 0};
        if (lo > hi) {
            std::swap(lo, hi);
            n = mul(n, -1);
        }
        if (lo > enter) {
            enter = lo;
            normal = n;
        }
        exit = std::min(exit, hi);
    }
    if (enter > exit || exit < 0 || enter > 1)
        return {};
    if (enter < 0) {
        // 圆心可能位于扩张矩形的角区，但尚未接触真实圆角；不能因入射时间为负而漏掉穿越。
        const Vec2 closest{std::clamp(v.x, -d.halfExtents.x, d.halfExtents.x),
                           std::clamp(v.y, -d.halfExtents.y, d.halfExtents.y)};
        const auto outward = sub(v, closest);
        const float length = std::sqrt(dot2(outward, outward));
        if (length <= 1e-7f)
            return {};
        normal = mul(outward, 1 / length);
    }
    if (dot2(delta, normal) >= 0)
        return {};
    return {std::max(0.f, enter), normal};
}
} // namespace
struct CollisionWorld::Impl {
    struct Slot {
        ColliderDesc value;
        uint64_t generation = 1;
        bool live = true;
    };
    uint64_t owner;
    float cellSize;
    bool dirty = true;
    std::vector<Slot> slots;
    std::vector<uint32_t> freeSlots;
    std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>> grid;
    explicit Impl(float c) : cellSize(c) {
        static std::atomic<uint64_t> next{1};
        owner = next++;
    }
    std::vector<std::pair<int32_t, int32_t>> cells(Vec2 lo, Vec2 hi) const {
        const double x0 = std::floor(lo.x / cellSize), x1 = std::floor(hi.x / cellSize),
                     y0 = std::floor(lo.y / cellSize), y1 = std::floor(hi.y / cellSize);
        if (!finite2(lo) || !finite2(hi) || x0 < -1e8 || x1 > 1e8 || y0 < -1e8 || y1 > 1e8 ||
            (x1 - x0 + 1) * (y1 - y0 + 1) > 65536)
            throw std::length_error("Collision query spans too many grid cells");
        std::vector<std::pair<int32_t, int32_t>> result;
        for (int64_t x = int64_t(x0); x <= int64_t(x1); ++x)
            for (int64_t y = int64_t(y0); y <= int64_t(y1); ++y)
                result.emplace_back(int32_t(x), int32_t(y));
        return result;
    }
    void rebuild() {
        if (!dirty)
            return;
        std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>> next;
        for (uint32_t i = 0; i < slots.size(); ++i)
            if (slots[i].live) {
                const auto &d = slots[i].value;
                const auto e =
                    d.shape == ColliderShape::Circle ? Vec2{d.radius, d.radius} : d.halfExtents;
                for (auto cell : cells(sub(d.center, e), vadd(d.center, e)))
                    next[cell].push_back(i);
            }
        grid = std::move(next);
        dirty = false;
    }
    std::vector<ColliderHandle> candidates(Vec2 lo, Vec2 hi, uint32_t mask, ColliderHandle ignore) {
        rebuild();
        std::set<uint32_t> ids;
        for (auto cell : cells(lo, hi))
            if (auto i = grid.find(cell); i != grid.end())
                ids.insert(i->second.begin(), i->second.end());
        std::vector<ColliderHandle> result;
        for (auto i : ids) {
            ColliderHandle h{i, slots[i].generation, owner};
            if (h != ignore && (slots[i].value.layerMask & mask))
                result.push_back(h);
        }
        return result;
    }
};
CollisionWorld::CollisionWorld(float c) {
    if (!std::isfinite(c) || c <= 0)
        throw std::invalid_argument("Invalid collision cell size");
    impl_ = std::make_unique<Impl>(c);
}
CollisionWorld::~CollisionWorld() = default;
CollisionWorld::CollisionWorld(CollisionWorld &&) noexcept = default;
CollisionWorld &CollisionWorld::operator=(CollisionWorld &&) noexcept = default;
bool CollisionWorld::valid(ColliderHandle h) const {
    return h.owner == impl_->owner && h.index < impl_->slots.size() && impl_->slots[h.index].live &&
           h.generation == impl_->slots[h.index].generation;
}
ColliderHandle CollisionWorld::add(const ColliderDesc &d) {
    validate(d);
    const auto e = d.shape == ColliderShape::Circle ? Vec2{d.radius, d.radius} : d.halfExtents;
    impl_->cells(sub(d.center, e), vadd(d.center, e));
    uint32_t index;
    if (impl_->freeSlots.empty()) {
        if (impl_->slots.size() >= std::numeric_limits<uint32_t>::max())
            throw std::length_error("Too many colliders");
        index = uint32_t(impl_->slots.size());
        impl_->slots.push_back({d});
    } else {
        index = impl_->freeSlots.back();
        impl_->freeSlots.pop_back();
        impl_->slots[index].value = d;
        impl_->slots[index].live = true;
    }
    impl_->dirty = true;
    return {index, impl_->slots[index].generation, impl_->owner};
}
void CollisionWorld::update(ColliderHandle h, const ColliderDesc &d) {
    if (!valid(h))
        throw std::invalid_argument("Stale collider");
    validate(d);
    const auto e = d.shape == ColliderShape::Circle ? Vec2{d.radius, d.radius} : d.halfExtents;
    impl_->cells(sub(d.center, e), vadd(d.center, e));
    impl_->slots[h.index].value = d;
    impl_->dirty = true;
}
void CollisionWorld::remove(ColliderHandle h) {
    if (!valid(h))
        throw std::invalid_argument("Stale collider");
    auto &s = impl_->slots[h.index];
    // 槽位复用必须递增代次；代次耗尽时永久退役，避免旧句柄再次生效。
    if (s.generation != std::numeric_limits<uint64_t>::max()) {
        impl_->freeSlots.push_back(h.index);
        ++s.generation;
    }
    s.live = false;
    impl_->dirty = true;
}
ColliderDesc CollisionWorld::description(ColliderHandle h) const {
    if (!valid(h))
        throw std::invalid_argument("Stale collider");
    return impl_->slots[h.index].value;
}
std::vector<ColliderHandle> CollisionWorld::overlapCircle(Vec2 p, float r, uint32_t mask,
                                                          ColliderHandle ignore) {
    circle(p, r);
    auto result = impl_->candidates(sub(p, {r, r}), vadd(p, {r, r}), mask, ignore);
    std::erase_if(result, [&](auto h) { return !overlap(p, r, impl_->slots[h.index].value); });
    return result;
}
CircleMove CollisionWorld::moveCircle(Vec2 p, float r, Vec2 delta, uint32_t mask,
                                      ColliderHandle ignore, uint32_t iterations) {
    circle(p, r);
    if (!finite2(delta) || !iterations || iterations > 16)
        throw std::invalid_argument("Invalid circle motion");
    CircleMove result{p, delta, {}};
    for (uint32_t i = 0; i < iterations; ++i) {
        bool pushed = false;
        for (auto h : overlapCircle(result.position, r, mask, ignore)) {
            auto v = penetration(result.position, r, impl_->slots[h.index].value);
            if (dot2(v, v) > 1e-12f) {
                result.position = vadd(result.position, v);
                result.contacts.push_back(h);
                pushed = true;
            }
        }
        if (!pushed)
            break;
    }
    for (uint32_t i = 0; i < iterations && dot2(result.remaining, result.remaining) > 1e-12f; ++i) {
        const auto end = vadd(result.position, result.remaining);
        const Vec2 lo{std::min(result.position.x, end.x) - r,
                      std::min(result.position.y, end.y) - r},
            hi{std::max(result.position.x, end.x) + r, std::max(result.position.y, end.y) + r};
        Sweep first;
        ColliderHandle hit;
        for (auto h : impl_->candidates(lo, hi, mask, ignore)) {
            auto contact = sweep(result.position, r, result.remaining, impl_->slots[h.index].value);
            if (contact.time < first.time) {
                first = contact;
                hit = h;
            }
        }
        if (!hit) {
            result.position = end;
            result.remaining = {};
            break;
        }
        result.contacts.push_back(hit);
        result.position =
            vadd(result.position, mul(result.remaining, std::max(0.f, first.time - 1e-4f)));
        result.remaining = mul(result.remaining, 1 - first.time);
        result.remaining =
            sub(result.remaining,
                mul(first.normal, std::min(0.f, dot2(result.remaining, first.normal))));
    }
    return result;
}
} // namespace renderer
