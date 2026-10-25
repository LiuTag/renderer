/** @file @brief 声明同一渲染器内可反复更新、直接供 GPU 采样的相机输出纹理。 */
#ifndef RENDERER_INCLUDE_RENDERER_RENDER_TEXTURE_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_RENDER_TEXTURE_HPP_INCLUDED
#include <cstdint>
#include <memory>
#include <utility>
namespace renderer {
namespace detail {
struct RenderTextureAccess;
}
/** @brief 共享 PImpl 的实时 GPU 图像引用；复制保留生命周期，内容按渲染队列顺序更新。 */
class RenderTexture {
  public:
    /** @brief 创建空引用。@return 无。 */
    RenderTexture();
    /** @brief 释放引用，GPU 在后续收集及提交完成后安全回收。@return 无。 */
    ~RenderTexture();
    /** @brief 共享纹理引用。@param other 源引用。@return 无。 */
    RenderTexture(const RenderTexture &other);
    /** @brief 共享赋值。@param other 源引用。@return 当前对象。 */
    RenderTexture &operator=(const RenderTexture &other);
    /** @brief 转移引用。@param other 源引用。@return 无。 */
    RenderTexture(RenderTexture &&other) noexcept;
    /** @brief 转移赋值。@param other 源引用。@return 当前对象。 */
    RenderTexture &operator=(RenderTexture &&other) noexcept;
    /** @brief 查询尺寸。@return 宽高像素；空引用为零。 */
    std::pair<uint32_t, uint32_t> size() const;
    /** @brief 查询所属渲染器是否仍存活。@return 有效时 true。 */
    explicit operator bool() const;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend struct detail::RenderTextureAccess;
};
} // namespace renderer
#endif
