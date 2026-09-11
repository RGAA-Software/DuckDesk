#include "imgui_session.h"

#include "d3d11_renderer.h"
#include "font_loader.h"
#include "window_host.h"

#include "px_ui/px_ui_theme.h"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_dx11.h>
#include <imgui.h>

#include <functional>
#include <utility>

namespace px::desktop {

std::expected<ImGuiSession, std::string> ImGuiSession::Create(WindowHost& window, D3d11Renderer& renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    if (!ConfigureFonts(window.DisplayScale())) {
        ImGui::DestroyContext();
        return std::unexpected{"UI font initialization failed"};
    }
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, window.DisplayScale());

    if (!ImGui_ImplSDL3_InitForD3D(&window.Native())) {
        ImGui::DestroyContext();
        return std::unexpected{"Dear ImGui SDL3 backend initialization failed"};
    }
    if (!renderer.InitializeImGuiBackend()) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return std::unexpected{"Dear ImGui D3D11 backend initialization failed"};
    }
    auto session = ImGuiSession{renderer, true};
    session.displayScale_ = window.DisplayScale();
    return session;
}

ImGuiSession::ImGuiSession(std::reference_wrapper<D3d11Renderer> renderer, const bool sdlBackendInitialized) noexcept
    : renderer_{renderer}, sdlBackendInitialized_{sdlBackendInitialized} {}

ImGuiSession::ImGuiSession(ImGuiSession&& other) noexcept
    : renderer_{other.renderer_}, sdlBackendInitialized_{std::exchange(other.sdlBackendInitialized_, false)}, theme_{other.theme_},
      displayScale_{other.displayScale_} {}

ImGuiSession::~ImGuiSession() {
    if (sdlBackendInitialized_) {
        renderer_.get().ShutdownImGuiBackend();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
}

void ImGuiSession::BeginFrame() const {
    renderer_.get().BeginImGuiFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

bool ImGuiSession::NeedsInteractiveRefresh() const {
    return ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
}

bool ImGuiSession::ApplyAppearance(const px::ui::Theme theme, const float displayScale) {
    const float safeScale{displayScale > 0.0F ? displayScale : 1.0F};
    if (theme == theme_ && safeScale == displayScale_) {
        return true;
    }

    theme_ = theme;
    if (safeScale != displayScale_) {
        displayScale_ = safeScale;
        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui::GetIO().Fonts->Clear();
        if (!ConfigureFonts(displayScale_) || !ImGui_ImplDX11_CreateDeviceObjects()) {
            return false;
        }
    }
    px::ui::ApplyPixelsTheme(theme_, displayScale_);
    return true;
}

} // namespace px::desktop
