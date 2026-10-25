/** @file @brief 声明独立游戏 UI：RML/RCSS 文档、控件操作、事件订阅、输入路由及不可变覆盖层输出。 */
#ifndef RENDERER_MODULES_UI_UI_HPP_INCLUDED
#define RENDERER_MODULES_UI_UI_HPP_INCLUDED
#include <functional>
#include <renderer/events.hpp>
#include <renderer/overlay.hpp>
#include <string>
namespace renderer {
struct RenderStats;
struct RendererMemoryStats;
/** @brief 应用注入的平台服务；闭包生命周期由应用保证，不使 ui 依赖窗口/图形后端。 */
struct UiPlatformServices {
    std::function<void(const std::string &)> setClipboard;
    std::function<std::string()> getClipboard;
};
using UiDocumentId = uint64_t; ///< 进程唯一文档身份，0 无效，不允许跨上下文使用。
/** @brief UI 像素尺寸、输入坐标尺寸及设备无关缩放；输入尺寸为 0 时使用像素尺寸。 */
struct UiViewport {
    uint32_t width = 1280, height = 720;
    double inputWidth = 0, inputHeight = 0; ///< Window 客户区坐标尺寸，可与 framebuffer 不同。
    float density = 1;                      ///< RCSS dp 到像素的倍率，(0,16]。
};
/** @brief 控件的左上原点像素矩形。 */
struct UiRect {
    float x = 0, y = 0, width = 0, height = 0;
};
/** @brief 控件状态的独立副本，界限为 update 后的布局矩形，不含任意 CSS 变换；markup 为内部
 * RML，value 为表单值。 */
struct UiElementState {
    std::string id, tag, markup, value;
    UiRect bounds;
    bool visible = false, enabled = true, checked = false;
};
/** @brief 控件事件的独立副本；targetId 为原始目标，elementId 为订阅控件，value/checked
 * 为事件时状态。 */
struct UiEvent {
    UiDocumentId document = 0;
    std::string type, targetId, elementId, value;
    bool checked = false;
};
/** @brief 输入处理后的捕获状态；鼠标拖动持续捕获，keyboard 表示当前 UI 焦点占用键盘。 */
struct UiCapture {
    bool mouse = false, keyboard = false;
};
/** @brief PImpl RAII 订阅令牌，可早于或晚于上下文析构，取消后不再分派后续事件。 */
class UiSubscription {
  public:
    /** @brief 创建空令牌。@return 无。 */
    UiSubscription();
    /** @brief 自动取消。@return 无。 */
    ~UiSubscription();
    /** @brief 转移订阅。@param other 源令牌。@return 无。 */
    UiSubscription(UiSubscription &&other) noexcept;
    /** @brief 转移赋值并取消原订阅。@param other 源令牌。@return 当前对象。 */
    UiSubscription &operator=(UiSubscription &&other) noexcept;
    /** @brief 禁止复制。@param other 源令牌。@return 无；已删除。 */
    UiSubscription(const UiSubscription &other) = delete;
    /** @brief 禁止复制赋值。@param other 源令牌。@return 无；已删除。 */
    UiSubscription &operator=(const UiSubscription &other) = delete;
    /** @brief 显式取消，可重复调用；可从其他线程取消。@return 无。 */
    void cancel();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class UiContext;
};
/** @brief PImpl 游戏 UI 服务；所有上下文须在进程同一 UI 线程创建/使用/析构，不创建窗口或设备。
 * RML/RCSS、字体和输入均在 UI 线程处理；snapshot 返回独立覆盖层，渲染线程不调用 RmlUi。
 */
class UiContext {
  public:
    /** @brief 创建独立文档上下文。@param viewport 像素/输入尺寸和 dp 倍率。@return 无。 */
    explicit UiContext(const UiViewport &viewport = {});
    /** @brief 卸载文档并释放 UI 资源；旧绘制快照仍有效。@return 无。 */
    ~UiContext();
    /** @brief 转移实现。@param other 源对象，转移后仅可析构或赋值。@return 无。 */
    UiContext(UiContext &&other) noexcept;
    /** @brief 转移实现。@param other 源对象。@return 当前对象。 */
    UiContext &operator=(UiContext &&other) noexcept;
    /** @brief 禁止复制。@param other 源对象。@return 无；已删除。 */
    UiContext(const UiContext &other) = delete;
    /** @brief 禁止复制赋值。@param other 源对象。@return 无；已删除。 */
    UiContext &operator=(const UiContext &other) = delete;
    /** @brief 加载字体，可注册多种字族/回退字体。
     * @param path 支持 Unicode 的字体路径。@param fallback 缺字时用作回退。@param faceIndex
     * 字体集合索引。
     * @return 无；读取失败抛出异常。字体由共享运行时保留到最后一个上下文销毁。
     */
    void loadFont(const std::filesystem::path &path, bool fallback = false, int faceIndex = 0);
    /** @brief 从文件加载并显示文档，相对 RCSS/图片路径以文档位置为基准。
     * @param path RML 路径，支持 Unicode。@return 文档身份；加载失败抛出异常。
     */
    UiDocumentId loadDocument(const std::filesystem::path &path);
    /** @brief 从内存加载并显示文档。
     * @param rml UTF-8 RML。@param sourceUrl 资源基准路径/诊断名称。@return 文档身份。
     */
    UiDocumentId loadDocumentFromMemory(const std::string &rml,
                                        const std::string &sourceUrl = "memory.rml");
    /** @brief 卸载文档并取消其订阅。@param document 当前上下文中的有效文档。@return 无。 */
    void unloadDocument(UiDocumentId document);
    /** @brief 显示/隐藏文档。@param document 文档身份。@param visible 是否可见。@return 无。 */
    void showDocument(UiDocumentId document, bool visible = true);
    /** @brief 读取控件状态副本。@param document 文档身份。@param id 控件 ID。@return
     * 控件状态；不存在抛出异常。 */
    UiElementState element(UiDocumentId document, const std::string &id) const;
    /** @brief 设置纯文本，自动转义 RML。@param document 文档身份。@param id 控件 ID。@param text
     * UTF-8 文本。@return 无。 */
    void setText(UiDocumentId document, const std::string &id, const std::string &text);
    /** @brief 设置子元素 RML。@param document 文档身份。@param id 控件 ID。@param rml
     * 应用提供的可信 RML。@return 无。 */
    void setMarkup(UiDocumentId document, const std::string &id, const std::string &rml);
    /** @brief 设置 RCSS 属性。@param document 文档身份。@param id 控件 ID。@param property
     * 属性名。@param value RCSS 值。@return 无；不合法属性抛出异常。 */
    void setProperty(UiDocumentId document, const std::string &id, const std::string &property,
                     const std::string &value);
    /** @brief 设置表单值/进度值。@param document 文档身份。@param id 控件 ID。@param value UTF-8
     * 值。@return 无。 */
    void setValue(UiDocumentId document, const std::string &id, const std::string &value);
    /** @brief 设置复选状态。@param document 文档身份。@param id 控件 ID。@param checked
     * 是否选中。@return 无。 */
    void setChecked(UiDocumentId document, const std::string &id, bool checked);
    /** @brief 设置表单禁用状态。@param document 文档身份。@param id 表单控件 ID。@param enabled
     * 是否启用。@return 无；非表单控件抛出异常。 */
    void setEnabled(UiDocumentId document, const std::string &id, bool enabled);
    /** @brief 请求控件焦点。@param document 文档身份。@param id 控件 ID。@return 是否成功。 */
    bool focus(UiDocumentId document, const std::string &id);
    /** @brief 注册图片，RML 用 src="ui:名称" 引用；重复名称被拒绝。
     * @param name 非空唯一名称，不含冒号。@param image 未预乘 CPU 图片，支持 DDS/压缩/数组。
     * @param layer 数组层号，默认第 0 层；解码基准 mip 后转为普通二维图片。
     * @return 无；内部转换到线性预乘 Alpha 并保存，不暴露 GPU 句柄。
     */
    void registerImage(const std::string &name, TextureDesc image, uint32_t layer = 0);
    /** @brief 将实时相机纹理注册为 UI 图片，RML 使用 ui:名称，完全保留在 GPU。
     * @param name 唯一图片名称。@param image 所属 Renderer 的有效输出。@return
     * 无；不进行颜色预乘转换。 */
    void registerImage(const std::string &name, const RenderTexture &image);
    /** @brief 接入系统剪贴板等服务。@param services 闭包，可留空以使用内部剪贴板。@return 无。 */
    void setPlatformServices(UiPlatformServices services);
    /** @brief 回收共享 UI 字体运行时未使用的字形/纹理并重建活跃文字。
     * @return 无；影响同线程全部 UiContext，旧 OverlaySnapshot 继续保留其资源。 */
    void releaseFontCache();
    /** @brief 查询当前编辑控件区域，应用每帧传给 Window::setTextInputArea。
     * @return 客户区坐标，已按 UiViewport 的输入尺寸转换；无编辑焦点时 inactive。 */
    TextInputArea textInputArea() const;
    /** @brief 创建/更新只读性能调试面板，绘制与资源统计来自最近完成帧。
     * @param stats 帧统计。@param gpu 渲染器估算。@param cpu 仓库统计。@param visible 是否显示。
     * @return 面板文档身份，可用普通文档接口继续调整；使用 Lato 字体和已注册回退。 */
    UiDocumentId setDebugPanel(const RenderStats &stats, const RendererMemoryStats &gpu,
                               const AssetMemoryStats &cpu, bool visible = true);
    /** @brief 修改画布/DPI/输入坐标映射。@param viewport 正尺寸配置。@return
     * 无；无效配置保留原状态。 */
    void setViewport(const UiViewport &viewport);
    /** @brief 接收引擎事件，返回此事件是否应由 UI 独占；不自动订阅 Window。
     * @param event 键鼠、已提交文本、焦点、尺寸和 DPI 事件。@return 是否捕获；其他事件返回 false。
     */
    bool processEvent(const WindowEvent &event);
    /** @brief 查询鼠标/键盘占用。@return 捕获状态；游戏应允许已有按键的 Release 清理运动。 */
    UiCapture capture() const;
    /** @brief 推进布局/动画并分派 UI 事件。
     * @param seconds [0,4] 的有限秒数；0 仅刷新。@return 无；回调异常记录到
     * takeMessages，不中断其他回调。
     */
    void update(double seconds);
    /** @brief 生成拥有几何和纹理的独立快照，可传给 RenderView::overlay。
     * @return 与当前画布尺寸匹配的快照；必要时刷新布局，不分派回调。
     */
    OverlaySnapshot snapshot();
    /** @brief 订阅控件及其后代冒泡到文档的事件，在 update 后按顺序分派。
     * @param document 文档身份。@param id 控件 ID。@param type 事件名，例如 click/change/submit。
     * @param callback 接收值副本引用的函数，不可重入 update/snapshot/processEvent。
     * @return RAII 订阅令牌；必须由调用者保留。
     */
    UiSubscription subscribe(UiDocumentId document, const std::string &id, const std::string &type,
                             std::function<void(const UiEvent &)> callback);
    /** @brief 取走已发生的控件事件副本。@return 最多 4096 项，溢出记录诊断；不受订阅取消影响。 */
    std::vector<UiEvent> takeEvents();
    /** @brief 取走解析/图片/回调等诊断信息。@return UTF-8 信息列表。 */
    std::vector<std::string> takeMessages();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Impl &checked() const;
};
} // namespace renderer
#endif // RENDERER_MODULES_UI_UI_HPP_INCLUDED
