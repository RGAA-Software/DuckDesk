#include "window_host.h"

#include "windows_title_bar_behavior.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace px::desktop {
namespace {

constexpr float kTitleBarHeight{48.0F};
constexpr int kResizeBorder{7};
constexpr int kCaptionButtonWidth{46};
constexpr std::string_view kCaptionButtonCountProperty{"Pixels.Window.CaptionButtonCount"};
constexpr std::string_view kResizableProperty{"Pixels.Window.Resizable"};

float InitialDisplayScale() {
    const SDL_DisplayID display{SDL_GetPrimaryDisplay()};
    const float scale{display == 0 ? 1.0F : SDL_GetDisplayContentScale(display)};
    return scale > 0.0F ? scale : 1.0F;
}

int ScaledWindowDimension(const int logicalSize, const float scale, const int availableSize) {
    const int requested{static_cast<int>(static_cast<float>(logicalSize) * scale)};
    return availableSize > 0 ? std::min(requested, static_cast<int>(static_cast<float>(availableSize) * 0.92F)) : requested;
}

struct SdlWindowDeleter final {
    void operator()(SDL_Window* window) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        SDL_DestroyWindow(window);
    }
};

using SdlWindow = std::unique_ptr<SDL_Window, SdlWindowDeleter>;

SDL_HitTestResult SDLCALL HitTest(SDL_Window* window, const SDL_Point* area, void*) { // NOLINT(gammaray-raw-pointer-boundary)
    int width{};
    int height{};
    SDL_GetWindowSize(window, &width, &height);

    const float displayScale{SDL_GetWindowDisplayScale(window)};
    const auto scale = [displayScale](const int value) { return static_cast<int>(static_cast<float>(value) * displayScale); };
    if (SDL_GetBooleanProperty(SDL_GetWindowProperties(window), kResizableProperty.data(), true)) {
        const int resizeBorder{scale(kResizeBorder)};
        const bool left{area->x < resizeBorder};
        const bool right{area->x >= width - resizeBorder};
        const bool top{area->y < resizeBorder};
        const bool bottom{area->y >= height - resizeBorder};
        if (top && left) {
            return SDL_HITTEST_RESIZE_TOPLEFT;
        }
        if (top && right) {
            return SDL_HITTEST_RESIZE_TOPRIGHT;
        }
        if (bottom && left) {
            return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        }
        if (bottom && right) {
            return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        }
        if (top) {
            return SDL_HITTEST_RESIZE_TOP;
        }
        if (bottom) {
            return SDL_HITTEST_RESIZE_BOTTOM;
        }
        if (left) {
            return SDL_HITTEST_RESIZE_LEFT;
        }
        if (right) {
            return SDL_HITTEST_RESIZE_RIGHT;
        }
    }

    const int captionButtonCount{static_cast<int>(SDL_GetNumberProperty(SDL_GetWindowProperties(window), kCaptionButtonCountProperty.data(), 3))};
    const int captionButtonsStart{width - scale(kCaptionButtonWidth) * captionButtonCount};
    if (area->y < scale(static_cast<int>(kTitleBarHeight)) && area->x < captionButtonsStart) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

std::string LastSdlError(const std::string& operation) {
    return operation + ": " + SDL_GetError();
}

} // namespace

struct WindowHost::Impl final {
    SdlWindow window{};
    std::optional<WindowsTitleBarBehavior> titleBarBehavior{};
    bool sdlInitialized{false};
    bool fullscreen{};
    bool vulkanSurfaceAvailable{};

    ~Impl() {
        titleBarBehavior.reset();
        window.reset();
        if (sdlInitialized) {
            SDL_Quit();
        }
    }
};

std::expected<WindowHost, std::string> WindowHost::Create(const std::string& title, const int width, const int height, const bool initiallyVisible,
                                                          const bool requestVulkanSurface, const WindowChromeConfig& chrome) {
    auto impl = std::make_unique<Impl>();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return std::unexpected{LastSdlError("SDL_Init")};
    }
    impl->sdlInitialized = true;

    const float initialScale{InitialDisplayScale()};
    SDL_Rect usableBounds{};
    const SDL_DisplayID primaryDisplay{SDL_GetPrimaryDisplay()};
    if (primaryDisplay != 0) {
        SDL_GetDisplayUsableBounds(primaryDisplay, &usableBounds);
    }
    const int scaledWidth{ScaledWindowDimension(width, initialScale, usableBounds.w)};
    const int scaledHeight{ScaledWindowDimension(height, initialScale, usableBounds.h)};
    constexpr SDL_WindowFlags baseFlags{SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN};
    const SDL_WindowFlags commonFlags{chrome.resizable ? baseFlags | SDL_WINDOW_RESIZABLE : baseFlags};
    const SDL_WindowFlags requestedFlags{requestVulkanSurface ? commonFlags | SDL_WINDOW_VULKAN : commonFlags};
    impl->window.reset(SDL_CreateWindow(title.c_str(), scaledWidth, scaledHeight, requestedFlags));
    impl->vulkanSurfaceAvailable = requestVulkanSurface && impl->window;
    if (!impl->window && requestVulkanSurface) {
        SDL_ClearError();
        impl->window.reset(SDL_CreateWindow(title.c_str(), scaledWidth, scaledHeight, commonFlags));
    }
    if (!impl->window) {
        return std::unexpected{LastSdlError("SDL_CreateWindow")};
    }
    const Sint64 captionButtonCount{1 + (chrome.showMinimizeButton ? 1 : 0) + (chrome.showMaximizeButton ? 1 : 0)};
    SDL_SetNumberProperty(SDL_GetWindowProperties(impl->window.get()), kCaptionButtonCountProperty.data(), captionButtonCount);
    SDL_SetBooleanProperty(SDL_GetWindowProperties(impl->window.get()), kResizableProperty.data(), chrome.resizable);
    SDL_SetWindowMinimumSize(impl->window.get(), 900, 600);
    if (!SDL_SetWindowHitTest(impl->window.get(), HitTest, nullptr)) {
        return std::unexpected{LastSdlError("SDL_SetWindowHitTest")};
    }
    auto titleBarResult = WindowsTitleBarBehavior::Create(*impl->window, chrome.allowTitleBarMaximize, chrome.useRoundedWindow, chrome.resizable);
    if (!titleBarResult) {
        return std::unexpected{titleBarResult.error()};
    }
    impl->titleBarBehavior.emplace(std::move(titleBarResult.value()));
    if (!SDL_SetWindowPosition(impl->window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED) ||
        (initiallyVisible && !SDL_ShowWindow(impl->window.get()))) {
        return std::unexpected{LastSdlError("Show centered window")};
    }
    return WindowHost{std::move(impl)};
}

WindowHost::WindowHost(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
WindowHost::WindowHost(WindowHost&&) noexcept = default;
WindowHost& WindowHost::operator=(WindowHost&&) noexcept = default;
WindowHost::~WindowHost() = default;

SDL_Window& WindowHost::Native() const noexcept {
    return *impl_->window;
}

bool WindowHost::VulkanSurfaceAvailable() const noexcept {
    return impl_->vulkanSurfaceAvailable;
}

void WindowHost::Minimize() const {
    SDL_MinimizeWindow(impl_->window.get());
}

void WindowHost::ToggleMaximize() const {
    if (IsMaximized()) {
        SDL_RestoreWindow(impl_->window.get());
    } else {
        SDL_MaximizeWindow(impl_->window.get());
    }
}

bool WindowHost::ToggleFullscreen() {
    const bool requested{!impl_->fullscreen};
    if (!SDL_SetWindowFullscreen(impl_->window.get(), requested))
        return false;
    impl_->fullscreen = requested;
    return true;
}

bool WindowHost::IsMaximized() const {
    return (SDL_GetWindowFlags(impl_->window.get()) & SDL_WINDOW_MAXIMIZED) != 0;
}

float WindowHost::DisplayScale() const {
    return SDL_GetWindowDisplayScale(impl_->window.get());
}

void WindowHost::Hide() const {
    SDL_HideWindow(impl_->window.get());
}

void WindowHost::ShowAndRaise() const {
    SDL_ShowWindow(impl_->window.get());
    SDL_RestoreWindow(impl_->window.get());
    SDL_RaiseWindow(impl_->window.get());
}

} // namespace px::desktop
