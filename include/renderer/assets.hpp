/**
 * @file
 * @brief 声明网格/纹理/材质仓库、实时 GPU 纹理引用、资源包和内存统计及基础几何体接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_ASSETS_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_ASSETS_HPP_INCLUDED

#include "renderer/handle.hpp"
#include "renderer/math.hpp"
#include "renderer/render_texture.hpp"
#include "renderer/texture_region.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
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
/** @brief 桌面块压缩格式；None 为 RGBA8，CPU 编码支持 BC1/3/4/5，BC2/7 仅导入。 */
enum class TextureCompression { None, BC1, BC2, BC3, BC4, BC5, BC7 };
/** @brief 有效载荷行原点；图集矩形仍采用逻辑左下坐标，采样自动处理差异。 */
enum class TextureOrigin { BottomLeft, TopLeft };
/** @brief RGBA8 颜色或单通道 Alpha8 覆盖率；Alpha8 必须使用线性空间。 */
enum class TextureChannels { Rgba8, Alpha8 };
/**
 * @brief CPU 二维/数组纹理、显式 mip/块载荷、颜色空间及采样描述。
 */
struct TextureDesc {
    uint32_t width = 0, height = 0;
    // pixels 保存基准级，通道由 channels 指定；默认左下原点 RGBA8 和 sRGB。
    // 数据纹理（法线、金属度/粗糙度、AO）应设置 srgb = false。
    std::vector<uint8_t> pixels;
    bool srgb = true;
    TextureSampling sampling;
    RenderTexture
        renderTexture; ///< 可选实时 GPU 来源；有效时 pixels 为空，只能在所属 Renderer 使用。
    uint32_t layers = 1, mipLevels = 1; ///< 数组层数和显式 mip 数，各层格式/尺寸相同。
    TextureCompression compression = TextureCompression::None;
    TextureOrigin origin = TextureOrigin::BottomLeft;
    std::vector<uint8_t> data; ///< 可选完整载荷，layer→mip→块行；有 data 时 pixels 为空。
    bool arrayTexture = false; ///< 强制单层也使用数组采样；layers>1 自动视为数组。
    TextureChannels channels = TextureChannels::Rgba8;
};
/** @brief Blend 材质的输出混合方式；源图片保持普通未预乘 RGBA，预乘由 Shader 输出阶段完成。 */
enum class MaterialBlend { Alpha, PremultipliedAlpha, Additive };
/** @brief 后端共用 GLSL 函数体；parameters 对应 effectParameters，时间对应 effectTime。
 * vertex 可修改 position/normal/texcoord（局部坐标）；surface 可修改 base（线性 RGBA），并读取
 * uv/worldPosition。 不应自行声明 main/资源绑定；裁剪/阴影由共用管线执行。 */
struct MaterialEffect {
    std::string vertex, surface;
    Vec4 parameters{};
    float boundsPadding = 0; ///< 世界空间保守包围盒扩大量，位移效果必须显式指定，非负。
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
    MaterialBlend blend = MaterialBlend::Alpha;
    MaterialEffect effect;
    uint32_t baseTextureLayer = 0; ///< 网格基础颜色数组层；精灵可逐实例覆盖。
};
using AlphaMode = MaterialDesc::AlphaMode;
class Model;

class RenderExtractor;
class AssetLoader;
/** @brief 仓库拥有的有效句柄和仍被快照/材质保留的 CPU 有效载荷统计，不含分配器开销。 */
struct AssetMemoryStats {
    uint64_t meshHandles = 0, textureHandles = 0, materialHandles = 0, spriteHandles = 0;
    uint64_t retainedMeshes = 0, retainedTextures = 0, meshBytes = 0, textureBytes = 0;
};
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
    /** @brief 局部更新并发布新的不可变纹理，原句柄及旧快照保持有效。
     * @param source 单层单 mip、关闭 mip 生成的未压缩 CPU 纹理。
     * @param region 按 source.origin 坐标的更新矩形。@param pixels 紧密 RGBA8 或 Alpha8 区域载荷。
     * @return 独立纹理句柄；GPU 可从已驻留原版本复制，仅上传区域。 */
    TextureHandle updateTexture(TextureHandle source, TextureRegion region,
                                std::span<const uint8_t> pixels);
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
    /** @brief 将同一渲染器的实时相机输出注册为纹理，不进行 CPU 回读。
     * @param texture 有效 GPU 输出引用，尺寸固定，内容实时更新。@return 仓库纹理句柄；CPU
     * 拾取将其视为不透明图片。 */
    TextureHandle createTexture(const RenderTexture &texture);
    /** @brief 复制不可变 CPU 纹理描述，供可选模块转换颜色或检查资源。
     * @param texture 当前仓库的有效纹理句柄。@return 独立描述；CPU 纹理复制像素，实时输出保留 GPU
     * 引用且 pixels 为空；无效句柄抛出异常。
     */
    TextureDesc textureDescription(TextureHandle texture) const;
    /** @brief 统计当前仓库及已接收资源包，包含已销毁句柄但被旧快照保留的数据。
     * @return 句柄数量及 CPU 网格/像素有效载荷字节，单写线程调用。 */
    AssetMemoryStats memoryStatistics() const;
    /** @brief 零拷贝接收另一个仓库，原资源句柄和模型模板保持有效。
     * @param source 独占且不再由其他线程修改的仓库；不能传入自身。@return 可用于 releasePackage
     * 的包身份；成功后 source 变为空仓库。
     */
    uint64_t adopt(AssetStore &&source);
    /** @brief 卸载一次 adopt 接收的完整资源包，原包内句柄失效，旧快照继续保留资源。
     * @param package adopt 返回的包身份，只能卸载直接接收的包。@return 无；身份无效时抛出异常。 */
    void releasePackage(uint64_t package);
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
