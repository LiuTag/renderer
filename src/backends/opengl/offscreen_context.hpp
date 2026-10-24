/** @file @brief 不公开原生图形类型的 OpenGL pbuffer 上下文，仅供后端内部使用。 */
#ifndef RENDERER_SRC_BACKENDS_OPENGL_OFFSCREEN_CONTEXT_HPP_INCLUDED
#define RENDERER_SRC_BACKENDS_OPENGL_OFFSCREEN_CONTEXT_HPP_INCLUDED
#include <memory>
namespace renderer::detail {
class OffscreenGLContext {
  public:
    using Proc = void (*)();
    explicit OffscreenGLContext(bool debug);
    ~OffscreenGLContext();
    void makeCurrent();
    void releaseCurrent();
    static Proc getProc(const char *name);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer::detail
#endif // RENDERER_SRC_BACKENDS_OPENGL_OFFSCREEN_CONTEXT_HPP_INCLUDED
