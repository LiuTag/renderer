/** @file @brief Windows WGL pbuffer；内部引导窗口只在构造期间存在，不作为渲染目标。 */
#include "offscreen_context.hpp"
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <mutex>
#include <windows.h>
namespace renderer::detail {
namespace {
using Choose = BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
using CreateBuffer = HANDLE(WINAPI *)(HDC, int, int, int, const int *);
using GetDC = HDC(WINAPI *)(HANDLE);
using ReleaseDC = int(WINAPI *)(HANDLE, HDC);
using Destroy = BOOL(WINAPI *)(HANDLE);
using CreateContext = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
void require(bool condition, const char *operation) {
    if (!condition)
        throw std::runtime_error(std::string(operation) + " (Win32 " +
                                 std::to_string(GetLastError()) + ")");
}
struct Bootstrap {
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    ~Bootstrap() {
        if (context) {
            if (wglGetCurrentContext() == context)
                wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(context);
        }
        if (dc)
            ::ReleaseDC(window, dc);
        if (window)
            DestroyWindow(window);
    }
    void create() {
        static std::once_flag registration;
        std::call_once(registration, [] {
            WNDCLASSW wc{};
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"Renderer_Offscreen_WGL_Bootstrap";
            require(RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                    "Register WGL bootstrap class");
        });
        window = CreateWindowExW(0, L"Renderer_Offscreen_WGL_Bootstrap", L"", WS_POPUP, 0, 0, 1, 1,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Create internal WGL bootstrap");
        dc = ::GetDC(window);
        require(dc != nullptr, "Get bootstrap DC");
        PIXELFORMATDESCRIPTOR p{};
        p.nSize = sizeof(p);
        p.nVersion = 1;
        p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        p.iPixelType = PFD_TYPE_RGBA;
        p.cColorBits = 32;
        const auto format = ChoosePixelFormat(dc, &p);
        require(format != 0, "Choose bootstrap format");
        require(SetPixelFormat(dc, format, &p) != FALSE, "Set bootstrap format");
        context = wglCreateContext(dc);
        require(context != nullptr, "Create bootstrap context");
        require(wglMakeCurrent(dc, context) != FALSE, "Activate bootstrap context");
    }
};
template <class T> T extension(const char *name) {
    const auto value = wglGetProcAddress(name);
    require(value && value != reinterpret_cast<PROC>(1) && value != reinterpret_cast<PROC>(2) &&
                value != reinterpret_cast<PROC>(3) && value != reinterpret_cast<PROC>(-1),
            name);
    return reinterpret_cast<T>(value);
}
} // namespace
struct OffscreenGLContext::Impl {
    HANDLE buffer = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    ReleaseDC releaseDC = nullptr;
    Destroy destroy = nullptr;
    ~Impl() {
        if (context) {
            if (wglGetCurrentContext() == context)
                wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(context);
        }
        if (dc && releaseDC)
            releaseDC(buffer, dc);
        if (buffer && destroy)
            destroy(buffer);
    }
};
OffscreenGLContext::OffscreenGLContext(bool debug) : impl_(std::make_unique<Impl>()) {
    Bootstrap bootstrap;
    bootstrap.create();
    const auto choose = extension<Choose>("wglChoosePixelFormatARB");
    const auto create = extension<CreateBuffer>("wglCreatePbufferARB");
    const auto getDC = extension<GetDC>("wglGetPbufferDCARB");
    impl_->releaseDC = extension<ReleaseDC>("wglReleasePbufferDCARB");
    impl_->destroy = extension<Destroy>("wglDestroyPbufferARB");
    const auto createContext = extension<CreateContext>("wglCreateContextAttribsARB");
    // DRAW_TO_PBUFFER、SUPPORT_OPENGL、RGBA、COLOR_BITS；离屏 FBO 不依赖该 1x1 表面的尺寸。
    const int attributes[] = {0x202d, TRUE, 0x2010, TRUE,  0x2013, 0x202b,
                              0x2014, 32,   0x2011, FALSE, 0};
    int format = 0;
    UINT count = 0;
    require(choose(bootstrap.dc, attributes, nullptr, 1, &format, &count) != FALSE && count > 0,
            "Choose pbuffer format");
    const int bufferAttributes[] = {0};
    impl_->buffer = create(bootstrap.dc, format, 1, 1, bufferAttributes);
    require(impl_->buffer != nullptr, "Create WGL pbuffer");
    impl_->dc = getDC(impl_->buffer);
    require(impl_->dc != nullptr, "Get WGL pbuffer DC");
    const int contextAttributes[] = {0x2091, 4, 0x2092, 5, 0x9126, 1, 0x2094, debug ? 1 : 0, 0};
    impl_->context = createContext(impl_->dc, nullptr, contextAttributes);
    require(impl_->context != nullptr, "Create OpenGL 4.5 pbuffer context");
    makeCurrent(); // 引导窗口在本构造函数返回前销毁，后续只保留 pbuffer/DC/GLRC。
}
OffscreenGLContext::~OffscreenGLContext() = default;
void OffscreenGLContext::makeCurrent() {
    require(wglMakeCurrent(impl_->dc, impl_->context) != FALSE, "Activate offscreen context");
}
void OffscreenGLContext::releaseCurrent() {
    require(wglMakeCurrent(nullptr, nullptr) != FALSE, "Release offscreen context");
}
OffscreenGLContext::Proc OffscreenGLContext::getProc(const char *name) {
    const auto p = wglGetProcAddress(name);
    if (p && p != reinterpret_cast<PROC>(1) && p != reinterpret_cast<PROC>(2) &&
        p != reinterpret_cast<PROC>(3) && p != reinterpret_cast<PROC>(-1))
        return reinterpret_cast<Proc>(p);
    return reinterpret_cast<Proc>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name));
}
} // namespace renderer::detail
#else
namespace renderer::detail {
struct OffscreenGLContext::Impl {};
OffscreenGLContext::OffscreenGLContext(bool) {
    throw std::runtime_error("OpenGL offscreen context currently requires Windows WGL; use Vulkan "
                             "offscreen on this platform");
}
OffscreenGLContext::~OffscreenGLContext() = default;
void OffscreenGLContext::makeCurrent() {}
void OffscreenGLContext::releaseCurrent() {}
OffscreenGLContext::Proc OffscreenGLContext::getProc(const char *) {
    return nullptr;
}
} // namespace renderer::detail
#endif
