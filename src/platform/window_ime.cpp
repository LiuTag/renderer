/** @file @brief Windows IMM32 组合文本/候选窗口适配；原生消息只排队，不调用应用回调。 */
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "../internal/window.hpp"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <commctrl.h>
#include <imm.h>
#include <string>
namespace renderer::detail {
namespace {
constexpr UINT_PTR hookId = 0x52454e44;
struct InputContext {
    HWND window;
    HIMC context;
    explicit InputContext(HWND w) : window(w), context(ImmGetContext(w)) {}
    ~InputContext() {
        if (context)
            ImmReleaseContext(window, context);
    }
};
std::wstring composition(HIMC context, DWORD field) {
    auto count = ImmGetCompositionStringW(context, field, nullptr, 0);
    if (count < 0 || count > 131072 || count % 2)
        return {};
    std::wstring out(size_t(count) / sizeof(wchar_t), L'\0');
    if (count && ImmGetCompositionStringW(context, field, out.data(), DWORD(count)) != count)
        return {};
    return out;
}
std::string utf8(const std::wstring &text) {
    auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return {};
    std::string out(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(),
                        count, nullptr, nullptr);
    return out;
}
uint32_t characters(const std::wstring &text, size_t units) {
    units = std::min(units, text.size());
    uint32_t count = 0;
    for (size_t i = 0; i < units; ++i) {
        ++count;
        if (text[i] >= 0xd800 && text[i] <= 0xdbff && i + 1 < units && text[i + 1] >= 0xdc00 &&
            text[i + 1] <= 0xdfff)
            ++i;
    }
    return count;
}
LRESULT CALLBACK inputProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                           DWORD_PTR data) {
    auto &state = *reinterpret_cast<WindowState *>(data);
    try {
        if (state.textInputArea.active) {
            if (message == WM_IME_SETCONTEXT)
                lparam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
            if (message == WM_IME_STARTCOMPOSITION) {
                state.postEvent(TextCompositionEvent{CompositionPhase::Begin});
                updateTextInputArea(state);
                return 0;
            }
            if (message == WM_IME_COMPOSITION) {
                InputContext input(window);
                if (input.context) {
                    if (lparam & GCS_RESULTSTR) {
                        auto text = composition(input.context, GCS_RESULTSTR);
                        state.postEvent(TextCompositionEvent{CompositionPhase::Commit, utf8(text),
                                                             characters(text, text.size())});
                    }
                    if (lparam & GCS_COMPSTR) {
                        auto text = composition(input.context, GCS_COMPSTR);
                        const auto cursor =
                            ImmGetCompositionStringW(input.context, GCS_CURSORPOS, nullptr, 0);
                        state.postEvent(TextCompositionEvent{
                            CompositionPhase::Update, utf8(text),
                            characters(text, cursor < 0 ? text.size() : size_t(cursor))});
                    }
                }
                return 0; // 结果已通过组合事件传递，阻止默认处理再次生成 WM_CHAR。
            }
            if (message == WM_IME_ENDCOMPOSITION) {
                state.postEvent(TextCompositionEvent{CompositionPhase::End});
                return 0;
            }
            if (message == WM_IME_CHAR)
                return 0;
        }
    } catch (...) {
        state.eventError(std::current_exception());
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
} // namespace
void installTextInputHook(WindowState &state) {
    if (!SetWindowSubclass(glfwGetWin32Window(state.native), inputProc, hookId,
                           reinterpret_cast<DWORD_PTR>(&state)))
        throw std::runtime_error("Cannot install Windows text composition handler");
}
void removeTextInputHook(WindowState &state) {
    RemoveWindowSubclass(glfwGetWin32Window(state.native), inputProc, hookId);
}
void updateTextInputArea(WindowState &state) {
    InputContext input(glfwGetWin32Window(state.native));
    if (!input.context)
        return;
    const auto &a = state.textInputArea;
    if (!a.active) {
        ImmNotifyIME(input.context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        return;
    }
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = {LONG(a.x), LONG(a.y + a.height)};
    candidate.rcArea = {LONG(a.x), LONG(a.y), LONG(a.x + a.width), LONG(a.y + a.height)};
    ImmSetCandidateWindow(input.context, &candidate);
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = candidate.ptCurrentPos;
    ImmSetCompositionWindow(input.context, &composition);
}
} // namespace renderer::detail
#else
namespace renderer::detail {
void installTextInputHook(WindowState &) {}
void removeTextInputHook(WindowState &) {}
void updateTextInputArea(WindowState &) {}
} // namespace renderer::detail
#endif
