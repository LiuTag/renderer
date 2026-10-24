/**
 * @file
 * @brief 定义不可变实例、相机、灯光快照记录与内部访问桥接，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_SNAPSHOT_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_SNAPSHOT_HPP_INCLUDED
#include "assets.hpp"
#include "renderer/render_snapshot.hpp"
#include "sprite.hpp"
#include <stdexcept>

namespace renderer::detail {
/**
 * @brief 快照中保有资源引用、变换、世界包围盒与可见层的网格实例。
 */
struct InstanceRecord {
    NodeHandle node;
    std::shared_ptr<const MeshData> mesh;
    std::vector<std::shared_ptr<const MaterialData>> materials;
    Mat4 world, normal;
    AABB bounds;
    uint32_t layerMask;
    bool mirrored;
    bool castShadow = true, receiveShadow = true;
    Vec4 uvRect{0, 0, 1, 1}, tint{1, 1, 1, 1};
    std::optional<SpriteSort> spriteSort;
    bool orderedSprite = false;
};
/**
 * @brief 快照中的源节点、相机组件与世界变换记录。
 */
struct CameraRecord {
    NodeHandle node;
    Camera camera;
    Mat4 world;
};
/**
 * @brief 快照中的灯光组件与世界变换记录。
 */
struct LightRecord {
    Light light;
    Mat4 world;
};
} // namespace renderer::detail

namespace renderer {
/**
 * @brief 共享不可变实例、相机、灯光和场景版本记录的快照实现。
 */
struct RenderSnapshot::Impl {
    uint64_t revision = 0;
    std::vector<detail::InstanceRecord> instances;
    std::vector<detail::SpriteRecord> sprites;
    std::vector<detail::CameraRecord> cameras;
    std::vector<detail::LightRecord> lights;
};
namespace detail {
/**
 * @brief 供帧准备等内部模块读取快照私有记录的友元桥。
 */
struct SnapshotAccess {
    /**
     * @brief 取得快照内部只读记录。
     * @param s 有效的已提取快照。
     * @return 内部数据引用；空快照抛出异常。
     */
    static const RenderSnapshot::Impl &get(const RenderSnapshot &s) {
        if (!s.impl_)
            throw std::invalid_argument("Empty render snapshot");
        return *s.impl_;
    }
};
} // namespace detail
} // namespace renderer

#endif // RENDERER_SRC_INTERNAL_SNAPSHOT_HPP_INCLUDED
