/** @file @brief 声明设备无关的屏幕覆盖层几何和不可变绘制快照，供 UI 等可选模块使用。 */
#ifndef RENDERER_INCLUDE_RENDERER_OVERLAY_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_OVERLAY_HPP_INCLUDED
#include "renderer/assets.hpp"
#include <memory>
#include <span>
#include <utility>
namespace renderer {
namespace detail {
struct OverlayAccess;
}
/** @brief 屏幕覆盖层顶点：左上原点像素坐标、左下原点纹理 UV、线性预乘 Alpha 颜色。 */
struct OverlayVertex {
    Vec2 position{}, uv{};
    Vec4 color{1, 1, 1, 1};
};
/** @brief 按提交顺序执行的一段索引绘制；裁剪矩形使用画布左上原点像素，零尺寸不绘制。 */
struct OverlayDraw {
    uint32_t firstIndex = 0, indexCount = 0;
    TextureHandle texture; ///< 空句柄表示白纹理；纹理 RGB 必须为线性预乘 Alpha。
    int32_t clipX = 0, clipY = 0;
    uint32_t clipWidth = 0, clipHeight = 0;
};
/** @brief 一次创建覆盖层快照的数据；实际目标尺寸不同时等比坐标分别缩放到完整目标。 */
struct OverlayDesc {
    uint32_t width = 0, height = 0; ///< 画布像素尺寸，每边 [1,16384]。
    std::vector<OverlayVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<OverlayDraw> draws; ///< 保持绘制顺序，不按纹理重排。
};
/** @brief 共享不可变 PImpl 快照，独立保留 CPU 几何与纹理，可复制到渲染线程或离屏任务。 */
class OverlaySnapshot {
  public:
    /** @brief 创建空快照。@return 无。 */
    OverlaySnapshot();
    /** @brief 释放当前引用。@return 无。 */
    ~OverlaySnapshot();
    /** @brief 共享复制。@param other 源快照。@return 无。 */
    OverlaySnapshot(const OverlaySnapshot &other);
    /** @brief 共享复制赋值。@param other 源快照。@return 当前对象。 */
    OverlaySnapshot &operator=(const OverlaySnapshot &other);
    /** @brief 转移引用。@param other 源快照。@return 无。 */
    OverlaySnapshot(OverlaySnapshot &&other) noexcept;
    /** @brief 转移赋值。@param other 源快照。@return 当前对象。 */
    OverlaySnapshot &operator=(OverlaySnapshot &&other) noexcept;
    /** @brief 验证数据并解析/保留纹理，源仓库随后可销毁资源。
     * @param assets 纹理所属仓库。@param description 要转移的画布、几何、索引与绘制命令。
     * @return 独立快照；非法尺寸/索引/颜色/句柄抛出异常，单帧最多 400 万顶点/1200 万索引/65536
     * 命令。
     */
    static OverlaySnapshot create(const AssetStore &assets, OverlayDesc description);
    /** @brief 读取画布像素尺寸。@return 宽高，默认空快照为 (0,0)。 */
    std::pair<uint32_t, uint32_t> size() const;
    /** @brief 读取顶点。@return 当前快照存活期间有效的只读范围。 */
    std::span<const OverlayVertex> vertices() const;
    /** @brief 读取索引。@return 当前快照存活期间有效的只读范围。 */
    std::span<const uint32_t> indices() const;
    /** @brief 读取有序命令。@return 当前快照存活期间有效的只读范围。 */
    std::span<const OverlayDraw> draws() const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend struct detail::OverlayAccess;
};
} // namespace renderer
#endif // RENDERER_INCLUDE_RENDERER_OVERLAY_HPP_INCLUDED
