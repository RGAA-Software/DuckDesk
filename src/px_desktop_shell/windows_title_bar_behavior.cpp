#include "windows_title_bar_behavior.h"

#include "title_bar.h"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <SDL3/SDL.h>

#include <memory>
#include <utility>

namespace px::desktop {
namespace {

constexpr int kResizeBorder{7};
constexpr int kCaptionButtonCount{3};
constexpr int kMinimumWindowWidth{900};
constexpr int kMinimumWindowHeight{600};
constexpr UINT_PTR kSubclassId{0x50584D42};
constexpr DWORD_PTR kAllowTitleBarMaximize{1};

bool IsMaximizeButton(const HWND window, const LPARAM position) {
    RECT clientBounds{};
    if (!GetClientRect(window, &clientBounds)) {
        return false;
    }

    POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    if (!ScreenToClient(window, &point)) {
        return false;
    }
    const UINT dpi{GetDpiForWindow(window)};
    const auto scale = [dpi](const int value) { return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); };
    const int buttonWidth{scale(kCaptionButtonLogicalWidth)};
    const int buttonStart{clientBounds.right - buttonWidth * (kCaptionButtonCount - 1)};
    const int buttonEnd{clientBounds.right - buttonWidth};
    return point.y >= scale(kResizeBorder) && point.y < scale(kTitleBarLogicalHeight) && point.x >= buttonStart && point.x < buttonEnd;
}

LRESULT CALLBACK TitleBarSubclass(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam, const UINT_PTR,
                                  const DWORD_PTR behaviorFlags) {
    if (message == kShowAndRaiseWindowMessage) {
        static_cast<void>(ShowWindow(window, SW_RESTORE));
        static_cast<void>(BringWindowToTop(window));
        static_cast<void>(SetForegroundWindow(window));
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        const LRESULT result{DefSubclassProc(window, message, wParam, lParam)};
        auto& limits{*reinterpret_cast<MINMAXINFO*>(lParam)}; // NOLINT(pixels-raw-pointer-boundary): Win32 message ABI.
        const UINT dpi{GetDpiForWindow(window)};
        limits.ptMinTrackSize.x = MulDiv(kMinimumWindowWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        limits.ptMinTrackSize.y = MulDiv(kMinimumWindowHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        return result;
    }
    if (message == WM_GETDPISCALEDSIZE) {
        // SDL keeps the physical client size fixed on Windows. Pixels UI instead follows the
        // platform's default linear DPI scaling so its logical size is stable across monitors.
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_DPICHANGED) {
        const RECT suggestedBounds{*reinterpret_cast<const RECT*>(lParam)}; // NOLINT(pixels-raw-pointer-boundary): Win32 message ABI.
        // SDL must observe the DPI transition so its display association and mouse-coordinate
        // state remain synchronized. It intentionally keeps the old physical client size, so
        // apply the platform's logical-size-preserving rectangle after SDL has returned.
        static_cast<void>(DefSubclassProc(window, message, wParam, lParam));
        static_cast<void>(SetWindowPos(window, nullptr, suggestedBounds.left, suggestedBounds.top, suggestedBounds.right - suggestedBounds.left,
                                       suggestedBounds.bottom - suggestedBounds.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER));
        return 0;
    }
    if (message == WM_NCLBUTTONDBLCLK && wParam == HTCAPTION && (behaviorFlags & kAllowTitleBarMaximize) == 0) {
        return 0;
    }
    if (message == WM_NCHITTEST && (behaviorFlags & kAllowTitleBarMaximize) != 0 && IsMaximizeButton(window, lParam)) {
        return HTMAXBUTTON;
    }
    if (message == WM_NCLBUTTONUP && wParam == HTMAXBUTTON) {
        ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

struct WindowsTitleBarBehavior::Impl final {
    HWND window{};

    ~Impl() {
        if (window) {
            RemoveWindowSubclass(window, TitleBarSubclass, kSubclassId);
        }
    }
};

std::expected<WindowsTitleBarBehavior, std::string> WindowsTitleBarBehavior::Create(SDL_Window& window, const bool allowTitleBarMaximize,
                                                                                    const bool useRoundedWindow, const bool resizable) {
    const HWND nativeWindow{
        reinterpret_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(&window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))};
    if (!nativeWindow) {
        return std::unexpected{"SDL did not expose a Win32 window handle"};
    }
    const DWORD_PTR behaviorFlags{allowTitleBarMaximize ? kAllowTitleBarMaximize : 0};
    if (!SetWindowSubclass(nativeWindow, TitleBarSubclass, kSubclassId, behaviorFlags)) {
        return std::unexpected{"Windows title-bar integration failed"};
    }
    if (!allowTitleBarMaximize) {
        LONG_PTR style{GetWindowLongPtrW(nativeWindow, GWL_STYLE)};
        style &= ~static_cast<LONG_PTR>(WS_MAXIMIZEBOX);
        if (!resizable) {
            style &= ~static_cast<LONG_PTR>(WS_THICKFRAME);
        }
        SetWindowLongPtrW(nativeWindow, GWL_STYLE, style);
        static_cast<void>(SetWindowPos(nativeWindow, nullptr, 0, 0, 0, 0,
                                       SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER));
    }
    if (useRoundedWindow) {
        constexpr DWMNCRENDERINGPOLICY renderingPolicy{DWMNCRP_ENABLED};
        static_cast<void>(DwmSetWindowAttribute(nativeWindow, DWMWA_NCRENDERING_POLICY, &renderingPolicy, sizeof(renderingPolicy)));
        constexpr DWM_WINDOW_CORNER_PREFERENCE cornerPreference{DWMWCP_ROUND};
        static_cast<void>(DwmSetWindowAttribute(nativeWindow, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference)));
        constexpr BOOL darkMode{TRUE};
        static_cast<void>(DwmSetWindowAttribute(nativeWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode)));
        constexpr MARGINS shadowMargins{1, 1, 1, 1};
        static_cast<void>(DwmExtendFrameIntoClientArea(nativeWindow, &shadowMargins));
    }
    auto impl = std::make_unique<Impl>();
    impl->window = nativeWindow;
    return WindowsTitleBarBehavior{std::move(impl)};
}

WindowsTitleBarBehavior::WindowsTitleBarBehavior(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
WindowsTitleBarBehavior::WindowsTitleBarBehavior(WindowsTitleBarBehavior&&) noexcept = default;
WindowsTitleBarBehavior& WindowsTitleBarBehavior::operator=(WindowsTitleBarBehavior&&) noexcept = default;
WindowsTitleBarBehavior::~WindowsTitleBarBehavior() = default;

} // namespace px::desktop
