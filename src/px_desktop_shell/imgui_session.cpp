#include "imgui_session.h"

#include "brand_logo.h"
#include "desktop_renderer.h"
#include "font_loader.h"
#include "window_host.h"

#include "px_ui/px_ui_theme.h"

#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <functional>
#include <utility>

namespace px::desktop {

std::expected<ImGuiSession, std::string> ImGuiSession::Create(WindowHost& window, DesktopRenderer& renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    if (!ConfigureFonts()) {
        ImGui::DestroyContext();
        return std::unexpected{"UI font initialization failed"};
    }
    auto logoResult = BrandLogo::Load();
    if (!logoResult) {
        ImGui::DestroyContext();
        return std::unexpected{logoResult.error()};
    }
    auto platformIconsResult = PlatformIconAtlas::Load();
    if (!platformIconsResult) {
        ImGui::DestroyContext();
        return std::unexpected{platformIconsResult.error()};
    }
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, window.DisplayScale());

    const bool sdlInitialized{renderer.UsesVulkan() ? ImGui_ImplSDL3_InitForVulkan(&window.Native()) : ImGui_ImplSDL3_InitForD3D(&window.Native())};
    if (!sdlInitialized) {
        ImGui::DestroyContext();
        return std::unexpected{"Dear ImGui SDL3 backend initialization failed"};
    }
    if (!renderer.InitializeImGuiBackend()) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return std::unexpected{"Dear ImGui D3D11 backend initialization failed"};
    }
    auto session = ImGuiSession{renderer, std::move(logoResult.value()), std::move(platformIconsResult.value()), true};
    session.displayScale_ = window.DisplayScale();
    return session;
}

ImGuiSession::ImGuiSession(std::reference_wrapper<DesktopRenderer> renderer, BrandLogo logo, PlatformIconAtlas platformIcons,
                           const bool sdlBackendInitialized) noexcept
    : renderer_{renderer}, logo_{std::move(logo)}, platformIcons_{std::move(platformIcons)}, sdlBackendInitialized_{sdlBackendInitialized} {}

ImGuiSession::ImGuiSession(ImGuiSession&& other) noexcept
    : renderer_{other.renderer_}, logo_{std::move(other.logo_)}, platformIcons_{std::move(other.platformIcons_)},
      sdlBackendInitialized_{std::exchange(other.sdlBackendInitialized_, false)}, theme_{other.theme_}, displayScale_{other.displayScale_},
      enhancedVisualEffects_{other.enhancedVisualEffects_} {}

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

const BrandLogo& ImGuiSession::Logo() const noexcept {
    return logo_;
}

const PlatformIconAtlas& ImGuiSession::PlatformIcons() const noexcept {
    return platformIcons_;
}

bool ImGuiSession::ApplyAppearance(const px::ui::Theme theme, const float displayScale, const bool enhancedVisualEffects) {
    const float safeScale{displayScale > 0.0F ? displayScale : 1.0F};
    if (theme == theme_ && safeScale == displayScale_ && enhancedVisualEffects == enhancedVisualEffects_) {
        return true;
    }

    theme_ = theme;
    displayScale_ = safeScale;
    enhancedVisualEffects_ = enhancedVisualEffects;
    px::ui::ApplyPixelsTheme(theme_, displayScale_, enhancedVisualEffects_);
    return true;
}

} // namespace px::desktop
