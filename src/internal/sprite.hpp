/**
 * @file
 * @brief 定义精灵共享资源、视图几何与排序帮助函数，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_SPRITE_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_SPRITE_HPP_INCLUDED
#include "assets.hpp"
#include "renderer/scene.hpp"
#include "renderer/sprite.hpp"

namespace renderer::detail {
struct SpriteData {
    SpriteDesc description;
    std::shared_ptr<const MeshData> mesh;
    std::shared_ptr<const MaterialData> material;
};
struct SpriteSort {
    int32_t layer = 0, order = 0, memberOrder = 0;
    Vec3 point;
    uint32_t groupIndex = 0;
    uint64_t groupGeneration = 0;
};
struct SpriteRecord {
    NodeHandle node;
    std::shared_ptr<const SpriteData> sprite;
    SpriteRenderer component;
    Mat4 world;
    SpriteSort sort;
    uint64_t instanceId = 0;
};
struct SpriteGeometry {
    Mat4 world, normal;
    Vec4 uvRect;
    AABB bounds;
};
// 以实际主相机计算一次几何，阴影与拾取复用同一结果，不能使用光源相机重转图片。
std::optional<SpriteGeometry> spriteGeometry(const SpriteRecord &record, const Mat4 &cameraWorld);
Mat4 cameraProjection(const Camera &camera, uint32_t width, uint32_t height);
bool spriteSortLess(const SpriteSort &a, float depthA, const SpriteSort &b, float depthB);
void validateSprite(const SpriteRenderer &sprite);
} // namespace renderer::detail
#endif // RENDERER_SRC_INTERNAL_SPRITE_HPP_INCLUDED
