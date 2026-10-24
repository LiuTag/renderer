/**
 * @file
 * @brief 定义 CPU 帧准备结果及与内置着色器匹配的 GPU 数据布局，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_FRAME_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_FRAME_HPP_INCLUDED
#include "assets.hpp"
#include "renderer/renderer.hpp"
#include <cstddef>

namespace renderer::detail {
/**
 * @brief 与内置着色器 std140 布局一致的帧级 uniform 数据。
 */
struct alignas(16) FrameUniforms {
    Mat4 viewProjection;
    Vec4 lightDirection;
    Vec4 ambient;
    Mat4 lightViewProjection;
    Vec4 cameraPosition;
    Vec4 shadowParameters;
    Vec4 postParameters;
    Vec4 counts;
    Mat4 inverseViewProjection;
    Vec4 colorMultiplySaturation{1, 1, 1, 1}, colorAdd{}, contactParameters{};
};
/**
 * @brief 上传给 GPU 的世界矩阵、法线矩阵及实例标记。
 */
struct InstanceGpu {
    Mat4 world, normal;
    Vec4 flags{1, 0, 0, 0};
    Vec4 uvRect{0, 0, 1, 1}, tint{1, 1, 1, 1};
    Vec4 colorOverlay{}, outlineColor{}, effects{};
};
static_assert(sizeof(InstanceGpu) == 224 && offsetof(InstanceGpu, flags) == 128 &&
              offsetof(InstanceGpu, uvRect) == 144 && offsetof(InstanceGpu, tint) == 160);
/**
 * @brief 上传给 GPU 的位置/类型、方向/范围、颜色/强度和聚光参数。
 */
struct LightGpu {
    Vec4 positionType, directionRange, colorIntensity, coneShadow;
};
/**
 * @brief 上传给 GPU 的基础颜色、PBR 因子与自发光参数。
 */
struct MaterialGpu {
    Vec4 baseColor, factors, emissive, misc;
};
/**
 * @brief 共享网格、材质、子网格和镜像状态的一段连续实例绘制批次。
 */
struct DrawBatch {
    std::shared_ptr<const MeshData> mesh;
    std::shared_ptr<const MaterialData> material;
    uint32_t submesh;
    bool mirrored;
    uint32_t firstInstance, instanceCount;
};
/**
 * @brief 帧准备阶段生成的 uniform、实例、灯光、场景/阴影批次与统计。
 */
struct PreparedFrame {
    FrameUniforms uniforms;
    std::vector<InstanceGpu> instances;
    std::vector<DrawBatch> batches;
    std::vector<DrawBatch> shadowBatches;
    std::vector<DrawBatch> orderedSpriteBatches; // OIT 合成后单独排序的精灵；常规模式混排。
    std::vector<LightGpu> lights;
    Vec4 clearColor;
    RenderStats stats;
    bool shadows = false;
    uint32_t shadowResolution = 1024;
    TransparencyMode transparency = TransparencyMode::SortedAlpha;
    AntiAliasing antiAliasing = AntiAliasing::None;
};
/**
 * @brief 从不可变快照生成裁剪、排序、合批、光照和阴影用的 CPU 帧数据。
 * @param snapshot 已提取的场景快照。
 * @param view 本次相机与渲染设置。
 * @param width 目标像素宽度。
 * @param height 目标像素高度。
 * @return 可直接交给高层后端绘制的帧数据。
 */
PreparedFrame prepareFrame(const RenderSnapshot &snapshot, const RenderView &view, uint32_t width,
                           uint32_t height);
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_FRAME_HPP_INCLUDED
