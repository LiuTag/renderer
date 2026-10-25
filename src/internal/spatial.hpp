/** @file @brief 不可变 AABB BVH；查询结果恢复源顺序，避免改变透明排序与同深度拾取。 */
#ifndef RENDERER_SRC_INTERNAL_SPATIAL_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_SPATIAL_HPP_INCLUDED
#include "renderer/math.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <vector>
namespace renderer::detail {
class AabbIndex {
    struct Node {
        AABB bounds;
        uint32_t left = 0, right = 0, first = 0, count = 0;
    };
    std::vector<Node> nodes_;
    std::vector<uint32_t> ids_;
    static AABB merged(AABB a, const AABB &b) {
        return {{std::min(a.minimum.x, b.minimum.x), std::min(a.minimum.y, b.minimum.y),
                 std::min(a.minimum.z, b.minimum.z)},
                {std::max(a.maximum.x, b.maximum.x), std::max(a.maximum.y, b.maximum.y),
                 std::max(a.maximum.z, b.maximum.z)}};
    }
    uint32_t build(std::span<const AABB> boxes, uint32_t first, uint32_t count) {
        auto bound = boxes[ids_[first]];
        for (uint32_t n = 1; n < count; ++n)
            bound = merged(bound, boxes[ids_[first + n]]);
        const uint32_t at = uint32_t(nodes_.size());
        nodes_.push_back({bound, 0, 0, first, count});
        if (count <= 8)
            return at;
        auto extent = bound.maximum - bound.minimum;
        unsigned axis = extent.x >= extent.y && extent.x >= extent.z ? 0
                        : extent.y >= extent.z                       ? 1
                                                                     : 2;
        auto center = [&](uint32_t i) {
            const auto c = boxes[i].minimum * .5f + boxes[i].maximum * .5f;
            return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
        };
        auto middle = first + count / 2;
        std::nth_element(ids_.begin() + first, ids_.begin() + middle, ids_.begin() + first + count,
                         [&](auto a, auto b) {
                             const auto x = center(a), y = center(b);
                             return x == y ? a < b : x < y;
                         });
        auto l = build(boxes, first, count / 2), r = build(boxes, middle, count - count / 2);
        nodes_[at].left = l;
        nodes_[at].right = r;
        nodes_[at].count = 0;
        return at;
    }

  public:
    bool refit(std::span<const AABB> boxes) {
        if (boxes.size() != ids_.size())
            return false;
        for (size_t i = nodes_.size(); i > 0; --i) {
            auto &n = nodes_[i - 1];
            if (n.count) {
                n.bounds = boxes[ids_[n.first]];
                for (uint32_t k = 1; k < n.count; ++k)
                    n.bounds = merged(n.bounds, boxes[ids_[n.first + k]]);
            } else
                n.bounds = merged(nodes_[n.left].bounds, nodes_[n.right].bounds);
        }
        return true;
    }
    void reset(std::span<const AABB> boxes) {
        nodes_.clear();
        ids_.resize(boxes.size());
        std::iota(ids_.begin(), ids_.end(), 0u);
        nodes_.reserve(boxes.size() * 2);
        if (!boxes.empty())
            build(boxes, 0, uint32_t(boxes.size()));
    }
    template <class Predicate> std::vector<uint32_t> query(Predicate intersects) const {
        std::vector<uint32_t> found;
        if (nodes_.empty())
            return found;
        std::vector<uint32_t> stack{0};
        while (!stack.empty()) {
            auto id = stack.back();
            stack.pop_back();
            const auto &n = nodes_[id];
            if (!intersects(n.bounds))
                continue;
            if (n.count)
                found.insert(found.end(), ids_.begin() + n.first, ids_.begin() + n.first + n.count);
            else {
                stack.push_back(n.left);
                stack.push_back(n.right);
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    }
    static bool ray(const AABB &b, Vec3 origin, Vec3 direction) {
        float near = 0, far = std::numeric_limits<float>::infinity();
        const float lo[] = {b.minimum.x, b.minimum.y, b.minimum.z},
                    hi[] = {b.maximum.x, b.maximum.y, b.maximum.z},
                    o[] = {origin.x, origin.y, origin.z},
                    d[] = {direction.x, direction.y, direction.z};
        for (unsigned a = 0; a < 3; ++a) {
            if (d[a] == 0) {
                if (o[a] < lo[a] || o[a] > hi[a])
                    return false;
                continue;
            }
            auto x = (lo[a] - o[a]) / d[a], y = (hi[a] - o[a]) / d[a];
            if (x > y)
                std::swap(x, y);
            near = std::max(near, x);
            far = std::min(far, y);
            if (near > far)
                return false;
        }
        return true;
    }
};
} // namespace renderer::detail
#endif
