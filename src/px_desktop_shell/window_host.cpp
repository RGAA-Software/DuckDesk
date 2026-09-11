#include "window_host.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <utility>

namespace px::desktop {
namespace {

constexpr float kTitleBarHeight{48.0F};
constexpr int kResizeBorder{7};
constexpr int kCaptionButtonWidth{46};
constexpr int kCaptionButtonCount{3};

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

    const bool left{area->x < kResizeBorder};
    const bool right{area->x >= width - kResizeBorder};
    const bool top{area->y < kResizeBorder};
    const bool bottom{area->y >= height - kResizeBorder};
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

    const int captionButtonsStart{width - kCaptionButtonWidth * kCaptionButtonCount};
    if (area->y < static_cast<int>(kTitleBarHeight) && area->x < captionButtonsStart) {
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
    bool sdlInitialized{false};

    ~Impl() {
        window.reset();
        if (sdlInitialized) {
            SDL_Quit();
        }
    }
};

std::expected<WindowHost, std::string> WindowHost::Create(const std::string& title, const int width, const int height) {
    auto impl = std::make_unique<Impl>();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return std::unexpected{LastSdlError("SDL_Init")};
    }
    impl->sdlInitialized = true;

    constexpr SDL_WindowFlags flags{SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS};
    impl->window.reset(SDL_CreateWindow(title.c_str(), width, height, flags));
    if (!impl->window) {
        return std::unexpected{LastSdlError("SDL_CreateWindow")};
    }
    SDL_SetWindowMinimumSize(impl->window.get(), 900, 600);
    if (!SDL_SetWindowHitTest(impl->window.get(), HitTest, nullptr)) {
        return std::unexpected{LastSdlError("SDL_SetWindowHitTest")};
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

bool WindowHost::IsMaximized() const {
    return (SDL_GetWindowFlags(impl_->window.get()) & SDL_WINDOW_MAXIMIZED) != 0;
}

float WindowHost::DisplayScale() const {
    return SDL_GetWindowDisplayScale(impl_->window.get());
}

} // namespace px::desktop
