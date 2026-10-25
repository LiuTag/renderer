/** @file @brief 声明可选世界文字模块：字体/回退、字形缓存、排版、节点绑定与不可变批量绘制。 */
#ifndef RENDERER_MODULES_TEXT_TEXT_HPP_INCLUDED
#define RENDERER_MODULES_TEXT_TEXT_HPP_INCLUDED
#include <filesystem>
#include <renderer/scene.hpp>
#include <renderer/sprite_batch.hpp>
#include <string>
namespace renderer {
using TextFontId = uint64_t;  ///< 当前 TextSystem 的字体身份，0 无效；不可跨系统使用。
using WorldTextId = uint64_t; ///< 当前 TextSystem 的标签身份，0 无效；不可跨系统使用。
/** @brief 每行在逻辑排版宽度内的水平对齐方式。 */
enum class TextAlignment { Left, Center, Right };
/** @brief 位图按字号缓存；Sdf 复用规范字号的距离场，适合缩放/描边。 */
enum class TextRasterMode { Bitmap, Sdf };
/** @brief WorldUnits 按 pixelsPerUnit 转世界单位；ScreenPixels 保持投影字号，只允许 ViewPlane。 */
enum class TextSizeMode { WorldUnits, ScreenPixels };
/** @brief 光栅化/缓存容量限制；字体和标签由单写线程管理，不启动工作线程。 */
struct TextSystemConfig {
    uint32_t atlasSize = 512; ///< 每页正方形像素边长，2 的幂，[64,4096]，含透明边框。
    uint32_t maxAtlasPages = 64, maxCachedGlyphs = 65536; ///< 全系统图集页/字形上限。
    uint32_t sdfFontSize = 64, sdfSpread = 8; ///< SDF 规范字号 [16,128]、距离范围 [2,32]。
    bool automaticEviction = true;            ///< 容量不足时淘汰本次操作未使用的最旧字形/页面。
    uint32_t maxLabels = 10000, maxBatchGlyphs = 200000; ///< 标签及一次批量发布字形上限。
    bool compactAtlas = true;     ///< 使用 Alpha8/R8 页面；false 用于 RGBA8 对照。
    bool incrementalAtlas = true; ///< 新版本可从旧 GPU 页面复制并局部上传。
};
/** @brief 排版参数，以逻辑字号像素计；SDF 内部使用规范字号，中文按字形换行，不执行复杂塑形。 */
struct TextStyle {
    TextFontId font = 0;
    uint32_t fontSize = 32;   ///< 逻辑像素字号 [1,256]；位图按字号缓存，SDF 共享规范字号缓存。
    float maxWidth = 0;       ///< 换行宽度，0 不自动换行；单字超宽允许溢出。
    float lineSpacing = 1.2f; ///< 行距相对 fontSize 的倍数，[0.5,4]。
    uint32_t tabSpaces = 4;   ///< Tab 停靠间隔相对空格宽度的倍数，[1,16]。
    TextAlignment alignment = TextAlignment::Left;
    TextRasterMode rasterMode = TextRasterMode::Bitmap;
    Vec4 outlineColor{}; ///< 线性描边色及 Alpha，0 不描边；位图内描边，SDF 外描边。
    float outlineWidth = 0,
          edgeSoftness = 1; ///< 逻辑字号像素宽度 [0,16]、屏幕抗锯齿强度 [0.25,4]。
};
/** @brief 排版结果副本；size 为逻辑宽高，inkMinimum/Maximum 是左上原点的可见字形范围。 */
struct TextMetrics {
    Vec2 size{}, inkMinimum{}, inkMaximum{};
    float ascent = 0, descent = 0, lineHeight = 0;
    uint32_t lines = 0, glyphs = 0; ///< glyphs 不包含空格/Tab 等没有位图的字符。
};
/** @brief 场景文字描述；文字平面局部 XY，Y 向上，anchor 采用左下原点归一化逻辑框坐标。 */
struct WorldTextDesc {
    TextStyle style;
    std::string text;                  ///< 严格 UTF-8，最多 1 MiB，支持 CR/LF/Tab。
    NodeHandle node;                   ///< 可选绑定节点；无效/已销毁/继承隐藏节点在 batch 时跳过。
    Mat4 transform = Mat4::identity(); ///< 相对绑定节点的局部矩阵，未绑定时为世界矩阵。
    Vec4 color{1, 1, 1, 1};            ///< 线性非负 RGB、[0,1] Alpha。
    Vec2 anchor{.5f, 0};               ///< [0,1] 逻辑框锚点，默认底部居中。
    float pixelsPerUnit = 64;          ///< WorldUnits 每世界单位的字体像素数，必须为正。
    BillboardMode billboard = BillboardMode::ViewPlane;
    TextSizeMode sizeMode = TextSizeMode::WorldUnits;
    bool depthTest = true; ///< false 关闭深度读写，仍处于场景后处理之前。
    bool visible = true;
    uint32_t layerMask = 0xffffffffu;
    int32_t sortingLayer = 0, sortingOrder = 0; ///< 更大的值后绘制；同标签字形共享排序点。
};
/** @brief 字体/字形缓存和最近发布状态；published 不包含核心后续的逐字形视锥裁剪。 */
struct TextStats {
    uint32_t fonts = 0, labels = 0, cachedGlyphs = 0, atlasPages = 0;
    uint32_t publishedLabels = 0, publishedGlyphs = 0;
    uint64_t atlasPixelBytes = 0;
    uint64_t cacheHits = 0, cacheMisses = 0, evictedGlyphs = 0, evictedPages = 0;
    uint64_t pageRevisions = 0, dirtyPixelBytes = 0; ///< 成功发布页面次数及 CPU 更新区域累计载荷。
    uint64_t fontFileBytes = 0; ///< 当前字体源文件字节，不包含 FreeType 内部开销。
};
/** @brief PImpl 世界文字服务，只使用 core 公共接口；可与 UI 独立启停，所有操作集中在创建线程。 */
class TextSystem {
  public:
    /** @brief 创建独立字体库及缓存。@param config 容量配置。@return 无；无效配置抛出异常。 */
    explicit TextSystem(const TextSystemConfig &config = {});
    /** @brief 释放字体和当前缓存；已发布批次/快照仍有效。@return 无。 */
    ~TextSystem();
    /** @brief 转移实现。@param other 源对象。@return 无；源仅可析构/赋值。 */
    TextSystem(TextSystem &&other) noexcept;
    /** @brief 转移赋值。@param other 源对象。@return 当前对象。 */
    TextSystem &operator=(TextSystem &&other) noexcept;
    /** @brief 禁止复制。@param other 源对象。@return 无；已删除。 */
    TextSystem(const TextSystem &other) = delete;
    /** @brief 禁止复制赋值。@param other 源对象。@return 无；已删除。 */
    TextSystem &operator=(const TextSystem &other) = delete;
    /** @brief 加载最多 64 MiB 的字体，支持 Unicode 路径。
     * @param path 字体文件。@param faceIndex 字体集合索引，非负。
     * @return 当前系统内的字体身份；失败抛出异常，不增加字体。
     */
    TextFontId loadFont(const std::filesystem::path &path, int faceIndex = 0);
    /** @brief 设置按顺序查找的回退字体，清除标签排版缓存，不丢弃已有字形。
     * @param fonts 当前系统中的有效字体身份，不可重复。@return 无；非法输入保留旧配置。
     */
    void setFallbackFonts(std::span<const TextFontId> fonts);
    /** @brief 测量排版，可按需生成 CPU 字形，不要求窗口/场景/GPU。
     * @param style 字体与排版参数。@param text UTF-8 文本。@return 独立度量结果。
     */
    TextMetrics measure(const TextStyle &style, const std::string &text);
    /** @brief 预热给定文字的排版/字形图集，适合加载界面阶段调用。
     * @param style 目标字号与字体。@param text 预先收集的 UTF-8 字符。@return 无；GPU
     * 仍在首次使用批次时上传。 */
    void prewarm(const TextStyle &style, const std::string &text);
    /** @brief 回收全部当前字形/页面，保留标签/字体；旧批次继续有效，下次 batch 重新排版。
     * @return 无；可在场景/语言切换时调用，避免不断累积中文字形。 */
    void resetGlyphCache();
    /** @brief LRU 回收不被可见标签使用的页面，旧批次仍有效。
     * @param targetPages 希望保留的页数，0 尽量全部回收，正在使用的页面受到保护。@return
     * 实际释放的页面数量。 */
    uint32_t trimCache(uint32_t targetPages = 0);
    /** @brief 卸载未被标签或回退列表使用的字体，并重建共享字形缓存。
     * @param font 本系统有效且未使用的字体身份。@return 无；仍被使用时拒绝并保持原状。 */
    void unloadFont(TextFontId font);
    /** @brief 创建世界文字标签。@param description 字体/文本/节点与表现。@return 稳定标签身份。 */
    WorldTextId create(const WorldTextDesc &description);
    /** @brief 原子式替换描述，排版按需更新。@param id 有效标签。@param description 新描述。@return
     * 无。 */
    void setDescription(WorldTextId id, const WorldTextDesc &description);
    /** @brief 读取描述副本。@param id 有效标签。@return 完整描述。 */
    WorldTextDesc description(WorldTextId id) const;
    /** @brief 设置文本，校验失败保留旧文本。@param id 有效标签。@param text UTF-8 文本。@return
     * 无。 */
    void setText(WorldTextId id, const std::string &text);
    /** @brief 设置局部/世界变换，不使字形缓存失效。@param id 有效标签。@param transform
     * 可逆仿射矩阵。@return 无。 */
    void setTransform(WorldTextId id, const Mat4 &transform);
    /** @brief 设置颜色，不重新栅格化。@param id 有效标签。@param color 线性颜色。@return 无。 */
    void setColor(WorldTextId id, Vec4 color);
    /** @brief 显示/隐藏标签。@param id 有效标签。@param visible 是否可见。@return 无。 */
    void setVisible(WorldTextId id, bool visible);
    /** @brief 删除标签，保留字体与共享字形。@param id 有效标签。@return 无。 */
    void destroy(WorldTextId id);
    /** @brief 删除全部标签，保留字体与共享字形缓存。@return 无。 */
    void clear();
    /** @brief 按相机生成独立不可变字形批次，自动处理绑定节点、朝向和屏幕字号。
     * @param scene 提供相机和绑定节点的场景。@param camera 有效可见 Camera 节点。
     * @param width 视口像素宽，[1,16384]。@param height 视口像素高，[1,16384]。
     * @return 可传给 RenderExtractor::extract 的 SpriteBatch；不占用应用
     * AssetStore，不依赖本系统存活。
     */
    SpriteBatch batch(const Scene &scene, NodeHandle camera, uint32_t width, uint32_t height);
    /** @brief 查询缓存/发布统计。@return 状态副本。 */
    TextStats statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Impl &checked() const;
};
} // namespace renderer
#endif // RENDERER_MODULES_TEXT_TEXT_HPP_INCLUDED
