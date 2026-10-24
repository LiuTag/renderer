#include "renderer/math.hpp"
#include "internal/frustum.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace renderer {

Vec3 Vec3::operator+(Vec3 b) const {
    return {x + b.x, y + b.y, z + b.z};
}
Vec3 Vec3::operator-(Vec3 b) const {
    return {x - b.x, y - b.y, z - b.z};
}
Vec3 Vec3::operator*(float s) const {
    return {x * s, y * s, z * s};
}
float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalized(Vec3 v) {
    const float length = std::sqrt(dot(v, v));
    if (!std::isfinite(length) || length < 1e-8f)
        throw std::invalid_argument("Cannot normalize a zero/non-finite vector");
    return v * (1.0f / length);
}
bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float radians(float degrees) {
    return degrees * 0.01745329251994329577f;
}
float &Mat4::operator()(int row, int column) {
    return elements[static_cast<size_t>(column * 4 + row)];
}
float Mat4::operator()(int row, int column) const {
    return elements[static_cast<size_t>(column * 4 + row)];
}
Mat4 Mat4::identity() {
    Mat4 m;
    for (int i = 0; i < 4; ++i)
        m(i, i) = 1;
    return m;
}
Mat4 Mat4::translation(Vec3 p) {
    Mat4 m = identity();
    m(0, 3) = p.x;
    m(1, 3) = p.y;
    m(2, 3) = p.z;
    return m;
}
Mat4 Mat4::scaling(Vec3 s) {
    Mat4 m = identity();
    m(0, 0) = s.x;
    m(1, 1) = s.y;
    m(2, 2) = s.z;
    return m;
}
Mat4 Mat4::rotationX(float a) {
    Mat4 m = identity();
    const float c = std::cos(a), s = std::sin(a);
    m(1, 1) = c;
    m(1, 2) = -s;
    m(2, 1) = s;
    m(2, 2) = c;
    return m;
}
Mat4 Mat4::rotationY(float a) {
    Mat4 m = identity();
    const float c = std::cos(a), s = std::sin(a);
    m(0, 0) = c;
    m(0, 2) = s;
    m(2, 0) = -s;
    m(2, 2) = c;
    return m;
}
Mat4 Mat4::rotationZ(float a) {
    Mat4 m = identity();
    const float c = std::cos(a), s = std::sin(a);
    m(0, 0) = c;
    m(0, 1) = -s;
    m(1, 0) = s;
    m(1, 1) = c;
    return m;
}
Mat4 Mat4::perspective(float fov, float aspect, float nearPlane, float farPlane) {
    if (!std::isfinite(fov) || !std::isfinite(aspect) || !std::isfinite(nearPlane) ||
        !std::isfinite(farPlane) || fov <= 0 || fov >= radians(179.0f) || aspect <= 0 ||
        nearPlane <= 0 || farPlane <= nearPlane)
        throw std::invalid_argument("Invalid perspective camera parameters");
    const float f = 1.0f / std::tan(fov * 0.5f);
    Mat4 m;
    m(0, 0) = f / aspect;
    m(1, 1) = f;
    m(2, 2) = farPlane / (nearPlane - farPlane);
    m(2, 3) = farPlane * nearPlane / (nearPlane - farPlane);
    m(3, 2) = -1;
    return m;
}
Mat4 Mat4::lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalized(target - eye), s = normalized(cross(f, up)), u = cross(s, f);
    Mat4 m = identity();
    m(0, 0) = s.x;
    m(0, 1) = s.y;
    m(0, 2) = s.z;
    m(0, 3) = -dot(s, eye);
    m(1, 0) = u.x;
    m(1, 1) = u.y;
    m(1, 2) = u.z;
    m(1, 3) = -dot(u, eye);
    m(2, 0) = -f.x;
    m(2, 1) = -f.y;
    m(2, 2) = -f.z;
    m(2, 3) = dot(f, eye);
    return m;
}
Mat4 Mat4::orthographic(float l, float r, float b, float t, float n, float f) {
    if (!std::isfinite(l) || !std::isfinite(r) || !std::isfinite(b) || !std::isfinite(t) ||
        !std::isfinite(n) || !std::isfinite(f) || r <= l || t <= b || f <= n)
        throw std::invalid_argument("Invalid orthographic volume");
    Mat4 m = identity();
    m(0, 0) = 2 / (r - l);
    m(1, 1) = 2 / (t - b);
    m(2, 2) = 1 / (n - f);
    m(0, 3) = -(r + l) / (r - l);
    m(1, 3) = -(t + b) / (t - b);
    m(2, 3) = n / (n - f);
    return m;
}
Mat4 Mat4::quaternion(float x, float y, float z, float w) {
    const float length = std::sqrt(x * x + y * y + z * z + w * w);
    if (!std::isfinite(length) || length < 1e-8f)
        throw std::invalid_argument("Invalid quaternion");
    x /= length;
    y /= length;
    z /= length;
    w /= length;
    Mat4 m = identity();
    m(0, 0) = 1 - 2 * (y * y + z * z);
    m(0, 1) = 2 * (x * y - z * w);
    m(0, 2) = 2 * (x * z + y * w);
    m(1, 0) = 2 * (x * y + z * w);
    m(1, 1) = 1 - 2 * (x * x + z * z);
    m(1, 2) = 2 * (y * z - x * w);
    m(2, 0) = 2 * (x * z - y * w);
    m(2, 1) = 2 * (y * z + x * w);
    m(2, 2) = 1 - 2 * (x * x + y * y);
    return m;
}
Mat4 Mat4::operator*(const Mat4 &b) const {
    Mat4 m;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                m(r, c) += (*this)(r, k) * b(k, c);
    return m;
}
Vec4 Mat4::operator*(Vec4 b) const {
    return {(*this)(0, 0) * b.x + (*this)(0, 1) * b.y + (*this)(0, 2) * b.z + (*this)(0, 3) * b.w,
            (*this)(1, 0) * b.x + (*this)(1, 1) * b.y + (*this)(1, 2) * b.z + (*this)(1, 3) * b.w,
            (*this)(2, 0) * b.x + (*this)(2, 1) * b.y + (*this)(2, 2) * b.z + (*this)(2, 3) * b.w,
            (*this)(3, 0) * b.x + (*this)(3, 1) * b.y + (*this)(3, 2) * b.z + (*this)(3, 3) * b.w};
}
Vec3 Mat4::transformPoint(Vec3 p) const {
    const Vec4 v = (*this) * Vec4{p.x, p.y, p.z, 1};
    return {v.x, v.y, v.z};
}
Mat4 Mat4::transposed() const {
    Mat4 m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            m(r, c) = (*this)(c, r);
    return m;
}
std::optional<Mat4> Mat4::inverse() const {
    if (!isFinite())
        return std::nullopt;
    double a[4][8]{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c] = (*this)(r, c);
            a[r][c + 4] = (r == c ? 1.0 : 0.0);
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c]))
                pivot = r;
        if (std::abs(a[pivot][c]) < 1e-12)
            return std::nullopt;
        for (int k = 0; k < 8; ++k)
            std::swap(a[c][k], a[pivot][k]);
        const double divisor = a[c][c];
        for (int k = 0; k < 8; ++k)
            a[c][k] /= divisor;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                const double factor = a[r][c];
                for (int k = 0; k < 8; ++k)
                    a[r][k] -= factor * a[c][k];
            }
    }
    Mat4 result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result(r, c) = static_cast<float>(a[r][c + 4]);
    return result.isFinite() ? std::optional<Mat4>{result} : std::nullopt;
}
bool Mat4::isFinite() const {
    return std::all_of(elements.begin(), elements.end(), [](float f) { return std::isfinite(f); });
}
bool Mat4::isAffine() const {
    return isFinite() && (*this)(3, 0) == 0 && (*this)(3, 1) == 0 && (*this)(3, 2) == 0 &&
           (*this)(3, 3) == 1;
}
float Mat4::determinant3x3() const {
    return (*this)(0, 0) * ((*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1)) -
           (*this)(0, 1) * ((*this)(1, 0) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 0)) +
           (*this)(0, 2) * ((*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0));
}
AABB AABB::fromPoints(std::span<const Vec3> points) {
    if (points.empty())
        throw std::invalid_argument("Empty bounds");
    AABB b{points.front(), points.front()};
    for (Vec3 p : points) {
        if (!finite(p))
            throw std::invalid_argument("Non-finite vertex");
        b.minimum = {std::min(b.minimum.x, p.x), std::min(b.minimum.y, p.y),
                     std::min(b.minimum.z, p.z)};
        b.maximum = {std::max(b.maximum.x, p.x), std::max(b.maximum.y, p.y),
                     std::max(b.maximum.z, p.z)};
    }
    return b;
}
AABB AABB::transformed(const Mat4 &m) const {
    const Vec3 center = m.transformPoint((minimum + maximum) * 0.5f),
               e = (maximum - minimum) * 0.5f;
    const Vec3 extent = {
        std::abs(m(0, 0)) * e.x + std::abs(m(0, 1)) * e.y + std::abs(m(0, 2)) * e.z,
        std::abs(m(1, 0)) * e.x + std::abs(m(1, 1)) * e.y + std::abs(m(1, 2)) * e.z,
        std::abs(m(2, 0)) * e.x + std::abs(m(2, 1)) * e.y + std::abs(m(2, 2)) * e.z};
    return {center - extent, center + extent};
}
detail::Frustum::Frustum(const Mat4 &m) {
    for (int i = 0; i < 6; ++i)
        for (int c = 0; c < 4; ++c) {
            const int axis = i / 2;
            const float v = (i == 4) ? m(2, c) : m(3, c) + (i % 2 == 0 ? 1.0f : -1.0f) * m(axis, c);
            if (c == 0)
                planes_[i].x = v;
            else if (c == 1)
                planes_[i].y = v;
            else if (c == 2)
                planes_[i].z = v;
            else
                planes_[i].w = v;
        }
}
bool detail::Frustum::intersects(const AABB &b) const {
    for (const Vec4 p : planes_) {
        const Vec3 v = {p.x >= 0 ? b.maximum.x : b.minimum.x, p.y >= 0 ? b.maximum.y : b.minimum.y,
                        p.z >= 0 ? b.maximum.z : b.minimum.z};
        if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0)
            return false;
    }
    return true;
}

} // namespace renderer
