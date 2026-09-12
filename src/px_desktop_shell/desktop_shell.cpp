#include "px_desktop_shell/desktop_shell.h"

#include "d3d11_renderer.h"
#include "imgui_session.h"
#include "title_bar.h"
#include "window_host.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace px::desktop {
namespace {

constexpr Uint32 kShowWindowEvent{SDL_EVENT_USER + 41};
constexpr Uint32 kExitApplicationEvent{SDL_EVENT_USER + 42};

struct SdlTrayDeleter final {
    void operator()(SDL_Tray* tray) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): SDL owned handle boundary
        SDL_DestroyTray(tray);
    }
};

using SdlTray = std::unique_ptr<SDL_Tray, SdlTrayDeleter>;

void SDLCALL OnTrayEntry(void*, SDL_TrayEntry* entry) { // NOLINT(gammaray-raw-pointer-boundary): SDL callback ABI
    const std::string_view label{SDL_GetTrayEntryLabel(entry)};
    SDL_Event event{};
    event.type = label == "Exit Pixels" ? kExitApplicationEvent : kShowWindowEvent;
    SDL_PushEvent(&event);
}

SdlTray CreateTray() {
    SdlTray tray{SDL_CreateTray(nullptr, "Pixels")};
    if (!tray || !SDL_CreateTrayMenu(tray.get())) {
        return {};
    }
    SDL_SetTrayEntryCallback(SDL_InsertTrayEntryAt(SDL_GetTrayMenu(tray.get()), -1, "Show Pixels", SDL_TRAYENTRY_BUTTON), OnTrayEntry, nullptr);
    SDL_SetTrayEntryCallback(SDL_InsertTrayEntryAt(SDL_GetTrayMenu(tray.get()), -1, "Exit Pixels", SDL_TRAYENTRY_BUTTON), OnTrayEntry, nullptr);
    return tray;
}

} // namespace

struct DesktopShell::Impl final {
    Impl(WindowHost windowValue, D3d11Renderer rendererValue, const bool minimizeToTrayValue)
        : window{std::move(windowValue)}, renderer{std::move(rendererValue)}, minimizeToTray{minimizeToTrayValue} {}

    WindowHost window;
    D3d11Renderer renderer;
    std::optional<ImGuiSession> imgui{};
    bool running{true};
    px::ui::Theme theme{px::ui::Theme::Dark};
    SdlTray tray{};
    bool minimizeToTray{false};
};

std::expected<DesktopShell, std::string> DesktopShell::Create(const WindowConfig& config) {
    auto windowResult = WindowHost::Create(config.title, config.width, config.height, config.initiallyVisible);
    if (!windowResult) {
        return std::unexpected{windowResult.error()};
    }
    auto rendererResult = D3d11Renderer::Create(windowResult.value());
    if (!rendererResult) {
        return std::unexpected{rendererResult.error()};
    }

    auto impl = std::make_unique<Impl>(std::move(windowResult.value()), std::move(rendererResult.value()), config.minimizeToTray);
    if (config.minimizeToTray) {
        impl->tray = CreateTray();
    }
    auto imguiResult = ImGuiSession::Create(impl->window, impl->renderer);
    if (!imguiResult) {
        return std::unexpected{imguiResult.error()};
    }
    impl->imgui.emplace(std::move(imguiResult.value()));
    return DesktopShell{std::move(impl)};
}

DesktopShell::DesktopShell(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
DesktopShell::DesktopShell(DesktopShell&&) noexcept = default;
DesktopShell& DesktopShell::operator=(DesktopShell&&) noexcept = default;
DesktopShell::~DesktopShell() = default;

int DesktopShell::Run(const RenderCallback& render, const InputCallback& input) {
    bool firstFrame{true};
    bool interactiveFrame{false};
    while (impl_->running) {
        SDL_Event event{};
        const int waitMilliseconds{firstFrame ? 0 : (interactiveFrame ? 16 : 200)};
        if (SDL_WaitEventTimeout(&event, waitMilliseconds)) {
            do {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (input) {
                    DesktopInputEvent translated{.type = event.type};
                    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                        translated.key = event.key.key;
                        translated.scanCode = event.key.scancode;
                    } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                        translated.text = event.text.text;
                    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                        translated.x = event.motion.x;
                        translated.y = event.motion.y;
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                        translated.mouseButton = event.button.button;
                        translated.x = event.button.x;
                        translated.y = event.button.y;
                    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                        translated.wheelX = event.wheel.x;
                        translated.wheelY = event.wheel.y;
                    }
                    input(translated);
                }
                if (event.type == kShowWindowEvent) {
                    impl_->window.ShowAndRaise();
                } else if (event.type == kExitApplicationEvent || event.type == SDL_EVENT_QUIT) {
                    impl_->running = false;
                } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    if (impl_->minimizeToTray) {
                        impl_->window.Hide();
                    } else {
                        impl_->running = false;
                    }
                }
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && !impl_->renderer.Resize(event.window.data1, event.window.data2)) {
                    return 2;
                }
                if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED &&
                    !impl_->imgui->ApplyAppearance(impl_->theme, impl_->window.DisplayScale())) {
                    return 3;
                }
            } while (SDL_PollEvent(&event));
        }
        if (!impl_->running) {
            break;
        }

        impl_->imgui->BeginFrame();
        const ImGuiViewport& viewport = *ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport.Pos);
        ImGui::SetNextWindowSize(viewport.Size);
        constexpr ImGuiWindowFlags rootFlags{ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings};
        ImGui::Begin("PixelsRoot", nullptr, rootFlags);
        if (!DrawTitleBar(impl_->window)) {
            if (impl_->minimizeToTray) {
                impl_->window.Hide();
            } else {
                impl_->running = false;
            }
        }
        render();
        ImGui::End();

        interactiveFrame = impl_->imgui->NeedsInteractiveRefresh();
        ImGui::Render();
        impl_->renderer.Render();
        firstFrame = false;
    }
    return 0;
}

bool DesktopShell::UpdateVideoTexture(const int width, const int height, const std::span<const std::uint8_t> bgra) {
    return impl_->renderer.UpdateVideoTexture(width, height, bgra);
}

std::uint64_t DesktopShell::VideoTextureId() const noexcept {
    return impl_->renderer.VideoTextureId();
}

bool DesktopShell::SetTheme(const px::ui::Theme theme) {
    impl_->theme = theme;
    return impl_->imgui->ApplyAppearance(theme, impl_->window.DisplayScale());
}

bool DesktopShell::ToggleFullscreen() {
    return impl_->window.ToggleFullscreen();
}

void DesktopShell::RequestExit() noexcept {
    impl_->running = false;
}

void DesktopShell::RequestShowAndRaise() noexcept {
    SDL_Event event{};
    event.type = kShowWindowEvent;
    SDL_PushEvent(&event);
}

} // namespace px::desktop
