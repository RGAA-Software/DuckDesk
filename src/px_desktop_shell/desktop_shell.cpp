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
#include <utility>

namespace px::desktop {

struct DesktopShell::Impl final {
    Impl(WindowHost windowValue, D3d11Renderer rendererValue) : window{std::move(windowValue)}, renderer{std::move(rendererValue)} {}

    WindowHost window;
    D3d11Renderer renderer;
    std::optional<ImGuiSession> imgui{};
    bool running{true};
};

std::expected<DesktopShell, std::string> DesktopShell::Create(const WindowConfig& config) {
    auto windowResult = WindowHost::Create(config.title, config.width, config.height);
    if (!windowResult) {
        return std::unexpected{windowResult.error()};
    }
    auto rendererResult = D3d11Renderer::Create(windowResult.value());
    if (!rendererResult) {
        return std::unexpected{rendererResult.error()};
    }

    auto impl = std::make_unique<Impl>(std::move(windowResult.value()), std::move(rendererResult.value()));
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

int DesktopShell::Run(const RenderCallback& render) {
    bool firstFrame{true};
    bool interactiveFrame{false};
    while (impl_->running) {
        SDL_Event event{};
        const int waitMilliseconds{firstFrame ? 0 : (interactiveFrame ? 16 : 200)};
        if (SDL_WaitEventTimeout(&event, waitMilliseconds)) {
            do {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    impl_->running = false;
                }
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && !impl_->renderer.Resize(event.window.data1, event.window.data2)) {
                    return 2;
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
        impl_->running = DrawTitleBar(impl_->window);
        render();
        ImGui::End();

        interactiveFrame = impl_->imgui->NeedsInteractiveRefresh();
        ImGui::Render();
        impl_->renderer.Render();
        firstFrame = false;
    }
    return 0;
}

} // namespace px::desktop
