/**
 * @file
 * @brief 定义不可变 CPU 资源数据与资源仓库内部访问桥接接口，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_ASSETS_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_ASSETS_HPP_INCLUDED
#include "renderer/assets.hpp"

namespace renderer::detail {
struct SpriteData;
/**
 * @brief 具有稳定内部 ID、网格数据及局部包围盒的不可变共享资源。
 */
struct MeshData {
    uint64_t id;
    MeshDesc description;
    AABB bounds;
};
/**
 * @brief 具有稳定内部 ID 和 CPU 像素描述的不可变共享纹理。
 */
struct TextureData {
    uint64_t id;
    TextureDesc description;
    uint64_t parentId = 0;
    TextureRegion dirty;
};
/**
 * @brief 保有已解析纹理引用和材质参数的不可变共享资源。
 */
struct MaterialData {
    uint64_t id;
    Vec4 baseColor;
    std::shared_ptr<const TextureData> texture;
    bool unlit;
    MaterialDesc description;
    std::shared_ptr<const TextureData> normalTexture, metallicRoughnessTexture, occlusionTexture,
        emissiveTexture;
};
/**
 * @brief 供引擎内部解析资源仓库私有数据的友元桥。
 */
struct AssetAccess {
    static std::pair<uint64_t, uint64_t> stamp(const AssetStore &assets);
    static std::shared_ptr<const TextureData> texture(const AssetStore &assets,
                                                      TextureHandle texture);
    static std::shared_ptr<const SpriteData> sprite(const AssetStore &assets, SpriteHandle sprite);
    /**
     * @brief 解析并共享不可变网格数据。
     * @param assets 句柄所属资源仓库。
     * @param mesh 有效网格句柄。
     * @return 网格共享数据；无效句柄抛出异常。
     */
    static std::shared_ptr<const MeshData> mesh(const AssetStore &assets, MeshHandle mesh);
    /**
     * @brief 解析并共享包含已解析纹理引用的材质数据。
     * @param assets 句柄所属资源仓库。
     * @param material 有效材质句柄。
     * @return 材质共享数据；无效句柄抛出异常。
     */
    static std::shared_ptr<const MaterialData> material(const AssetStore &assets,
                                                        MaterialInstanceHandle material);
};
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_ASSETS_HPP_INCLUDED
