/**
 * @file
 * @brief 声明图片精灵、图集、Billboard 与基于落地点的排序组件；不暴露图形 API。
 */
#ifndef RENDERER_INCLUDE_RENDERER_SPRITE_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_SPRITE_HPP_INCLUDED
#include "renderer/assets.hpp"
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace renderer {
/** @brief 图集像素矩形；原点在纹理左下角，不包含外扩边框。 */
struct SpriteRect {
    uint32_t x = 0, y = 0, width = 0, height = 0;
};
/** @brief 不可变精灵资源描述；尺寸和裁剪偏移使用原始图片的像素单位。 */
struct SpriteDesc {
    TextureHandle texture;                  ///< 当前仓库的纹理；创建精灵后保有共享引用。
    SpriteRect rect;                        ///< 图集中的内容区域，宽高必须为正。
    Vec2 originalSize{};                    ///< 裁剪前尺寸；两者均为 0 时取 rect 的宽高。
    Vec2 trimOffset{};                      ///< 内容左下角在裁剪前图片中的位置。
    Vec2 pivot{0.5f, 0};                    ///< 原始图片归一化锚点，默认脚底中心，范围 [0,1]。
    float pixelsPerUnit = 100;              ///< 每世界单位对应的像素数，必须为正。
    AlphaMode alphaMode = AlphaMode::Blend; ///< Blend 保留柔和边缘并参与精灵排序。
    float alphaCutoff = 0.5f;               ///< Mask 的 Alpha 裁剪阈值。
    bool unlit = true;                      ///< true 保持图片原色；false 使用现有场景光照。
};
/** @brief 图集中的命名条目；texture 由 SpriteAtlas 构造参数统一指定。 */
struct SpriteAtlasEntry {
    std::string name;
    SpriteDesc sprite;
};
/**
 * @brief 以不可变共享 PImpl 保存名称到精灵句柄的索引，精灵资源由 AssetStore 拥有。
 * 不执行图片打包；输入已打包纹理及元数据。析构图集不销毁仓库中的精灵。
 */
class SpriteAtlas {
  public:
    /**
     * @brief 在仓库创建图集各条目；无效条目或重名时回滚本次精灵创建。
     * @param assets 接收精灵资源的仓库，按单写线程调用。
     * @param texture 图集纹理句柄，覆盖条目中的 texture 字段。
     * @param entries 非空且名称唯一的图集条目。
     * @return 无（构造函数）；失败抛出异常。
     */
    SpriteAtlas(AssetStore &assets, TextureHandle texture,
                std::span<const SpriteAtlasEntry> entries);
    /** @brief 释放图集索引引用。@return 无。 */
    ~SpriteAtlas();
    /** @brief 共享复制图集索引。@param other 源图集。@return 无。 */
    SpriteAtlas(const SpriteAtlas &other);
    /** @brief 共享复制图集索引。@param other 源图集。@return 当前图集。 */
    SpriteAtlas &operator=(const SpriteAtlas &other);
    /** @brief 转移索引引用。@param other 源图集。@return 无。 */
    SpriteAtlas(SpriteAtlas &&other) noexcept;
    /** @brief 转移索引引用。@param other 源图集。@return 当前图集。 */
    SpriteAtlas &operator=(SpriteAtlas &&other) noexcept;
    /**
     * @brief 根据名称查询精灵；未知名称抛出 out_of_range。
     * @param name 区分大小写的条目名称。
     * @return 创建在原仓库中的精灵句柄。
     */
    SpriteHandle sprite(std::string_view name) const;
    /** @brief 读取条目数。@return 图集中精灵的数量。 */
    size_t size() const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};
/** @brief Fixed 使用节点 XY 平面；Upright 保持世界 Y 竖直；ViewPlane 平行相机平面。 */
enum class BillboardMode { Fixed, Upright, ViewPlane };
/** @brief Sorted 始终按落地点排序；FollowView 服从视图的排序 Alpha 或 OIT 设置。 */
enum class SpriteTransparency { Sorted, FollowView };
/** @brief 场景节点的图片表现；节点原点为锚点，Billboard 不修改节点世界矩阵。 */
struct SpriteRenderer {
    SpriteHandle sprite;
    Vec4 tint{1, 1, 1, 1};             ///< 线性非负 RGB 和 [0,1] Alpha。
    Vec2 sizeMultiplier{1, 1};         ///< 相对资源尺寸的正缩放。
    bool flipX = false, flipY = false; ///< 围绕锚点翻转内容及裁剪偏移。
    BillboardMode billboard = BillboardMode::Upright;
    float rotation = 0; ///< Billboard 平面内旋转弧度；Fixed 时也作为额外局部旋转。
    uint32_t layerMask = 0xFFFFFFFFu;
    int32_t sortingLayer = 0, sortingOrder = 0; ///< 较大的层/顺序后绘制。
    Vec3 sortOffset{};                          ///< 节点局部排序点偏移；默认使用落地点。
    SpriteTransparency transparency = SpriteTransparency::Sorted;
    bool castShadow = false, receiveShadow = false; ///< 投影沿当前主视图的图片几何生成。
};
/**
 * @brief 将子树精灵作为一个整体与外界排序；最外层分组生效，部件仍按自身顺序排序。
 * 分组节点通常放在地面，图片子节点可抬高；只影响 Blend，不覆盖深度测试结果。
 */
struct SortingGroup {
    int32_t sortingLayer = 0, sortingOrder = 0;
    Vec3 sortOffset{}; ///< 分组节点局部的排序点偏移。
};
} // namespace renderer
#endif // RENDERER_INCLUDE_RENDERER_SPRITE_HPP_INCLUDED
