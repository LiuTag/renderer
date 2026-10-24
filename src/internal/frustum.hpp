/**
 * @file
 * @brief 声明用于世界包围盒裁剪的六平面视锥实现，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_FRUSTUM_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_FRUSTUM_HPP_INCLUDED
#include "renderer/math.hpp"

namespace renderer::detail {
/**
 * @brief 由六个裁剪平面组成、用于保守包围盒裁剪的内部视锥。
 */
class Frustum {
  public:
    /**
     * @brief 从统一 [0, 1] 深度视投影矩阵提取六个裁剪平面。
     * @param viewProjection 世界坐标到裁剪空间的矩阵。
     * @return 无（构造函数）。
     */
    explicit Frustum(const Mat4 &viewProjection);
    /**
     * @brief 保守检查世界轴对齐包围盒与视锥相交情况。
     * @param bounds 世界坐标系的轴对齐包围盒。
     * @return 盒体未被任意平面完全排除时为 true。
     */
    bool intersects(const AABB &bounds) const;

  private:
    std::array<Vec4, 6> planes_{};
};
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_FRUSTUM_HPP_INCLUDED
