/**
 * @file
 * @brief 声明 CPU 网格、纹理、材质资源仓库及基础几何体生成接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_ASSETS_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_ASSETS_HPP_INCLUDED

#include "renderer/handle.hpp"
#include "renderer/math.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace renderer {
struct MeshTag;
struct MaterialTag;
struct TextureTag;
struct SpriteTag;
struct SpriteDesc;
using MeshHandle = Handle<MeshTag>;
using MaterialInstanceHandle = Handle<MaterialTag>;
using TextureHandle = Handle<TextureTag>;
using SpriteHandle = Handle<SpriteTag>;

/**
 * @brief 网格顶点，包含位置、法线及二维 UV。
 */
struct Vertex {
    Vec3 position;
    Vec3 normal{0, 1, 0};
    Vec2 uv;
};
/**
 * @brief 索引缓冲中的连续绘制范围与材质槽编号。
 */
struct Submesh {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialSlot = 0; // 对应 MeshRenderer::materials 中的下标。
};
/**
 * @brief 用于一次性创建不可变网格的完整 CPU 数据。
 */
struct MeshDesc {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
};
/** @brief 纹理的最近点或线性过滤方式。 */
enum class TextureFilter { Nearest, Linear };
/** @brief CPU 纹理的重复、钳制或镜像寻址方式。 */
enum class TextureAddress { Repeat, Clamp, Mirror };
/**
 * @brief CPU 纹理资源的过滤、寻址及 mip 采样配置。
 */
struct TextureSampling {
    TextureFilter minFilter = TextureFilter::Linear, magFilter = TextureFilter::Linear,
                  mipFilter = TextureFilter::Linear;
    TextureAddress addressU = TextureAddress::Repeat, addressV = TextureAddress::Repeat;
    bool mipmapped = true;
};
/**
 * @brief CPU 二维纹理的尺寸、RGBA8 像素、颜色空间及采样描述。
 */
struct TextureDesc {
    uint32_t width = 0, height = 0;
    // 手工创建时使用左下原点 RGBA8；颜色纹理默认按 sRGB 解码。
    // 数据纹理（法线、金属度/粗糙度、AO）应设置 srgb = false。
    std::vector<uint8_t> pixels;
    bool srgb = true;
    TextureSampling sampling;
};
/**
 * @brief 线性颜色空间下的 PBR 材质及纹理引用描述。
 */
struct MaterialDesc {
    Vec4 baseColor{1, 1, 1, 1}; // 线性 RGBA，不要预先进行 sRGB 编码。
    TextureHandle baseColorTexture;
    bool unlit = false;
    /** @brief 不透明、Alpha 阈值裁剪或混合透明的材质模式。 */
    enum class AlphaMode { Opaque, Mask, Blend } alphaMode = AlphaMode::Opaque;
    float metallic = 0, roughness = 0.5f, alphaCutoff = 0.5f, normalScale = 1,
          occlusionStrength = 1;
    Vec3 emissive{};
    // MR 纹理的 G/B 通道分别为粗糙度/金属度；AO 使用 R 通道。
    TextureHandle normalTexture, metallicRoughnessTexture, occlusionTexture, emissiveTexture;
    bool doubleSided = false;
};
using AlphaMode = MaterialDesc::AlphaMode;
class Model;

class RenderExtractor;
namespace detail {
struct AssetAccess;
}

// CPU 不可变资源仓库，按单写线程使用；创建后通过新资源替换来表达修改。
// 服务接口采用 PImpl，公共头文件不暴露内部容器与图形 API 类型。
/**
 * @brief 以 PImpl 管理不可变 CPU 资源；按单写线程使用并通过新资源表达修改。
 */
class AssetStore {
  public:
    /**
     * @brief 创建空的资源仓库。
     * @return 无（构造函数）。
     */
    AssetStore();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~AssetStore();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    AssetStore(AssetStore &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    AssetStore &operator=(AssetStore &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    AssetStore(const AssetStore &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    AssetStore &operator=(const AssetStore &other) = delete;

    /**
     * @brief 校验并创建不可变 CPU 网格资源。
     * @param description 顶点、32 位索引及子网格范围；数据按值移入仓库。
     * @return 属于当前仓库的新网格句柄。
     */
    MeshHandle createMesh(MeshDesc description);
    /**
     * @brief 校验并创建不可变 CPU 纹理资源。
     * @param description 尺寸、左下原点 RGBA8 像素、颜色空间及采样方式。
     * @return 属于当前仓库的新纹理句柄。
     */
    TextureHandle createTexture(TextureDesc description);
    /**
     * @brief 创建材质并解析其纹理引用；无效纹理句柄抛出异常。
     * @param description 线性颜色、PBR 参数与当前仓库的纹理句柄。
     * @return 属于当前仓库的新材质句柄。
     */
    MaterialInstanceHandle createMaterial(const MaterialDesc &description);
    // 独立图片解码为左下原点 RGBA8；模型导入按模型自身的 UV 约定处理。
    /**
     * @brief 解码图片并创建左下原点 RGBA8 纹理；读取失败抛出异常。
     * @param path 图片文件路径。
     * @param srgb 颜色纹理设为 true；法线、MR、AO 等数据纹理设为 false。
     * @return 解码得到的纹理句柄。
     */
    TextureHandle loadTexture(const std::filesystem::path &path, bool srgb = true);
    /**
     * @brief 解码图片并指定采样方式，便于图集关闭 mip 或使用最近点过滤。
     * @param path 图片文件路径。
     * @param srgb 是否将颜色纹理按 sRGB 解码。
     * @param sampling 不可变采样配置，在创建纹理时保存。
     * @return 左下原点 RGBA8 纹理句柄；加载失败抛出异常。
     */
    TextureHandle loadTexture(const std::filesystem::path &path, bool srgb,
                              const TextureSampling &sampling);
    /**
     * @brief 导入 glTF、GLB 或 OBJ 静态模型；失败时回滚本次创建的资源。
     * @param path 模型文件路径，其相对资源相对于模型所在目录解析。
     * @return 引用当前仓库资源的模型模板。
     */
    Model loadModel(const std::filesystem::path &path);
    /**
     * @brief 创建不可变图片资源；同纹理及材质参数的精灵共用材质和四边形。
     * @param description 像素区域、原始尺寸、裁剪偏移及材质描述，见 sprite.hpp。
     * @return 当前仓库的精灵句柄；参数或纹理无效时抛出异常。
     */
    SpriteHandle createSprite(const SpriteDesc &description);
    /**
     * @brief 读取精灵的规范化描述，原始尺寸的默认值已展开。
     * @param sprite 当前仓库有效的精灵句柄。
     * @return 描述副本；其纹理句柄可能已被显式销毁，内部共享纹理仍存活。
     */
    SpriteDesc spriteDescription(SpriteHandle sprite) const;
    /**
     * @brief 销毁精灵句柄；已提取的快照继续保有资源。
     * @param sprite 当前仓库有效的精灵句柄。
     * @return 无。
     */
    void destroySprite(SpriteHandle sprite);
    // 销毁使仓库句柄立即失效；已提取快照仍持有其引用的资源数据。
    /**
     * @brief 销毁仓库资源并使句柄立即失效；已提取快照继续保有数据引用。
     * @param mesh 当前仓库中仍有效的资源句柄。
     * @return 无。
     */
    void destroyMesh(MeshHandle mesh);
    /**
     * @brief 销毁仓库资源并使句柄立即失效；已提取快照继续保有数据引用。
     * @param texture 当前仓库中仍有效的资源句柄。
     * @return 无。
     */
    void destroyTexture(TextureHandle texture);
    /**
     * @brief 销毁仓库资源并使句柄立即失效；已提取快照继续保有数据引用。
     * @param material 当前仓库中仍有效的资源句柄。
     * @return 无。
     */
    void destroyMaterial(MaterialInstanceHandle material);

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend struct detail::AssetAccess;
};

namespace geometry {
/**
 * @brief 生成以原点为中心、边长为 1 的立方体网格。
 * @return 可直接交给 createMesh 的网格描述。
 */
MeshDesc cube(); // 以原点为中心、边长为 1 的立方体。
/**
 * @brief 生成位于 XZ 平面、法线朝 +Y 的正方形网格。
 * @param size 正方形的正边长。
 * @return 以原点为中心的平面网格描述。
 */
MeshDesc plane(float size = 1.0f); // 位于 XZ 平面，法线朝 +Y。
/**
 * @brief 生成以原点为中心、直径为 1 的球体网格。
 * @param segments 经向分段数，范围 [3, 256]。
 * @param rings 纬向分段数，范围 [2, 256]。
 * @return 包含位置、法线、UV 及索引的球体网格描述。
 */
MeshDesc sphere(uint32_t segments = 32, uint32_t rings = 16); // 直径为 1 的球体。
/**
 * @brief 生成棋盘格颜色纹理。
 * @param size 正方形纹理的像素边长，范围 [1, 4096]。
 * @param cells 每一行和每一列的格子数，范围 [1, size]。
 * @return 使用默认采样方式的 sRGB RGBA8 纹理描述。
 */
TextureDesc checkerboard(uint32_t size = 64, uint32_t cells = 8);
} // namespace geometry
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_ASSETS_HPP_INCLUDED
