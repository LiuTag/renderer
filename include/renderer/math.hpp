/**
 * @file
 * @brief 提供右手坐标、列主序矩阵、向量与轴对齐包围盒数学接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_MATH_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_MATH_HPP_INCLUDED

#include <array>
#include <optional>
#include <span>

namespace renderer {

/**
 * @brief 二维浮点向量。
 */
struct Vec2 {
    float x = 0, y = 0;
};
/**
 * @brief 三维浮点向量及基本算术操作。
 */
struct Vec3 {
    float x = 0, y = 0, z = 0;
    /**
     * @brief 逐分量相加。
     * @param b 右侧向量。
     * @return 相加后的向量。
     */
    Vec3 operator+(Vec3 b) const;
    /**
     * @brief 逐分量相减。
     * @param b 右侧向量。
     * @return 当前向量减去 b 的结果。
     */
    Vec3 operator-(Vec3 b) const;
    /**
     * @brief 对向量进行标量缩放。
     * @param s 缩放系数。
     * @return 各分量乘以 s 后的向量。
     */
    Vec3 operator*(float s) const;
};
/**
 * @brief 四维浮点或齐次向量。
 */
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};
/**
 * @brief 计算三维向量点积。
 * @param a 左侧向量。
 * @param b 右侧向量。
 * @return 点积标量。
 */
float dot(Vec3 a, Vec3 b);
/**
 * @brief 按右手法则计算叉积。
 * @param a 左侧向量。
 * @param b 右侧向量。
 * @return 垂直于 a、b 的叉积向量。
 */
Vec3 cross(Vec3 a, Vec3 b);
/**
 * @brief 归一化向量；零长度或非有限输入抛出异常。
 * @param v 需要归一化的非零有限向量。
 * @return 方向相同的单位向量。
 */
Vec3 normalized(Vec3 v);
/**
 * @brief 检查向量的所有分量是否有限。
 * @param v 待检查向量。
 * @return 不含 NaN 或无穷时为 true。
 */
bool finite(Vec3 v);
/**
 * @brief 将角度转换为弧度。
 * @param degrees 以度为单位的角度。
 * @return 以弧度为单位的角度。
 */
float radians(float degrees);

// 右手坐标系、列主序、列向量；组合变换 A * B 先应用 B，再应用 A。
// 角度使用弧度，投影的 NDC 深度范围统一为 [0, 1]。
/**
 * @brief 右手、列主序、列向量的 4×4 矩阵，投影 NDC 深度范围为 [0, 1]。
 */
struct Mat4 {
    std::array<float, 16> elements{};
    /**
     * @brief 访问可修改的矩阵元素。
     * @param row 行下标，范围 [0, 3]。
     * @param column 列下标，范围 [0, 3]。
     * @return 对应元素的引用；不执行边界检查。
     */
    float &operator()(int row, int column);
    /**
     * @brief 读取矩阵元素。
     * @param row 行下标，范围 [0, 3]。
     * @param column 列下标，范围 [0, 3]。
     * @return 对应元素的数值；不执行边界检查。
     */
    float operator()(int row, int column) const;
    /**
     * @brief 构建单位矩阵。
     * @return 主对角线为 1 的矩阵。
     */
    static Mat4 identity();
    /**
     * @brief 构建平移矩阵。
     * @param position 沿 X、Y、Z 轴的平移量。
     * @return 对应的仿射平移矩阵。
     */
    static Mat4 translation(Vec3 position);
    /**
     * @brief 构建缩放矩阵。
     * @param scale 沿 X、Y、Z 轴的缩放系数。
     * @return 对应的仿射缩放矩阵。
     */
    static Mat4 scaling(Vec3 scale);
    /**
     * @brief 构建绕 X 轴的右手旋转矩阵。
     * @param angle 旋转角，单位为弧度。
     * @return 对应的仿射旋转矩阵。
     */
    static Mat4 rotationX(float angle);
    /**
     * @brief 构建绕 Y 轴的右手旋转矩阵。
     * @param angle 旋转角，单位为弧度。
     * @return 对应的仿射旋转矩阵。
     */
    static Mat4 rotationY(float angle);
    /**
     * @brief 构建绕 Z 轴的右手旋转矩阵。
     * @param angle 旋转角，单位为弧度。
     * @return 对应的仿射旋转矩阵。
     */
    static Mat4 rotationZ(float angle);
    /**
     * @brief 构建右手透视投影；非法视锥参数抛出异常。
     * @param verticalFov 垂直视场角，弧度，范围 (0, radians(179))。
     * @param aspect 正的视口宽高比。
     * @param nearPlane 正的近裁剪面距离。
     * @param farPlane 大于近裁剪距离的远裁剪面距离。
     * @return NDC 深度为 [0, 1] 的投影矩阵。
     */
    static Mat4 perspective(float verticalFov, float aspect, float nearPlane, float farPlane);
    /**
     * @brief 构建右手正交投影；退化或非法范围抛出异常。
     * @param left 视锥左边界。
     * @param right 视锥右边界，须大于 left。
     * @param bottom 视锥下边界。
     * @param top 视锥上边界，须大于 bottom。
     * @param nearPlane 近裁剪面距离，允许为负，须小于 farPlane。
     * @param farPlane 远裁剪面距离，须大于 nearPlane。
     * @return NDC 深度为 [0, 1] 的投影矩阵。
     */
    static Mat4 orthographic(float left, float right, float bottom, float top, float nearPlane,
                             float farPlane);
    /**
     * @brief 由四元数生成旋转矩阵，内部归一化四元数。
     * @param x 四元数 X 分量。
     * @param y 四元数 Y 分量。
     * @param z 四元数 Z 分量。
     * @param w 四元数实部分量；四元数须有限且非零。
     * @return 对应的旋转矩阵。
     */
    static Mat4 quaternion(float x, float y, float z, float w);
    // 返回世界到相机的视图矩阵；作为相机节点变换时需要取逆。
    /**
     * @brief 构建世界到相机的视图矩阵；用于相机节点时需取逆。
     * @param eye 相机的世界坐标位置。
     * @param target 观察目标的世界坐标。
     * @param up 参考上方向，须与视线不平行。
     * @return 相机沿局部 -Z 观察的视图矩阵。
     */
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up = {0, 1, 0});
    /**
     * @brief 组合矩阵变换，先应用 b，再应用当前矩阵。
     * @param b 右侧变换矩阵。
     * @return 矩阵乘积。
     */
    Mat4 operator*(const Mat4 &b) const;
    /**
     * @brief 将矩阵应用到齐次列向量。
     * @param b 待变换的齐次向量。
     * @return 变换后的齐次向量。
     */
    Vec4 operator*(Vec4 b) const;
    /**
     * @brief 将矩阵应用到三维位置，直接取结果 XYZ，不执行齐次除法。
     * @param point 输入位置，隐含齐次分量 w=1；通常用于仿射变换。
     * @return 变换后的三维位置。
     */
    Vec3 transformPoint(Vec3 point) const;
    /**
     * @brief 交换矩阵的行与列。
     * @return 转置矩阵。
     */
    Mat4 transposed() const;
    /**
     * @brief 计算矩阵的逆。
     * @return 可逆时返回逆矩阵；奇异或数值上不可逆时返回空值。
     */
    std::optional<Mat4> inverse() const; // 奇异或数值上不可逆时返回空值。
    /**
     * @brief 检查矩阵的全部元素是否有限。
     * @return 全部元素均为有限数时为 true。
     */
    bool isFinite() const;
    /**
     * @brief 检查矩阵是否为有限仿射变换。
     * @return 矩阵有限且最后一行严格等于 [0, 0, 0, 1] 时为 true。
     */
    bool isAffine() const;
    /**
     * @brief 计算左上角 3×3 线性部分的行列式。
     * @return 行列式；负值表示变换包含镜像。
     */
    float determinant3x3() const;
};

// 直接保留仿射矩阵，使 KeepWorld 换父节点时能够保留剪切，无需有损 TRS 分解。
using Transform = Mat4;

/**
 * @brief 由最小和最大角点定义的三维轴对齐包围盒。
 */
struct AABB {
    Vec3 minimum{}, maximum{};
    /**
     * @brief 计算点集的轴对齐包围盒；空点集或非有限点抛出异常。
     * @param points 用于确定包围范围的非空位置集合。
     * @return 包含全部输入点的最小轴对齐包围盒。
     */
    static AABB fromPoints(std::span<const Vec3> points);
    /**
     * @brief 使用中心与半尺寸计算仿射变换后的轴对齐范围。
     * @param transform 应用到包围盒的仿射变换矩阵，不支持投影矩阵。
     * @return 变换后包围盒的轴对齐范围。
     */
    AABB transformed(const Mat4 &transform) const;
};

} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_MATH_HPP_INCLUDED
