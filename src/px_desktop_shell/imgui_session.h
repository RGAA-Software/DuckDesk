#pragma once

#include <expected>
#include <functional>
#include <string>

#include "brand_logo.h"
#include "px_desktop_shell/platform_icon_atlas.h"
#include "px_ui/px_ui_theme.h"

namespace px::desktop {

class DesktopRenderer;
class WindowHost;

class ImGuiSession final {
  public:
    static std::expected<ImGuiSession, std::string> Create(WindowHost& window, DesktopRenderer& renderer);

    ImGuiSession(ImGuiSession&&) noexcept;
    ImGuiSession& operator=(ImGuiSession&&) = delete;
    ~ImGuiSession();

    ImGuiSession(const ImGuiSession&) = delete;
    ImGuiSession& operator=(const ImGuiSession&) = delete;

    void BeginFrame() const;
    bool NeedsInteractiveRefresh() const;
    bool ApplyAppearance(px::ui::Theme theme, float displayScale, bool enhancedVisualEffects = true);
    [[nodiscard]] const BrandLogo& Logo() const noexcept;
    [[nodiscard]] const PlatformIconAtlas& PlatformIcons() const noexcept;

  private:
    ImGuiSession(std::reference_wrapper<DesktopRenderer> renderer, BrandLogo logo, PlatformIconAtlas platformIcons,
                 bool sdlBackendInitialized) noexcept;

    std::reference_wrapper<DesktopRenderer> renderer_;
    BrandLogo logo_;
    PlatformIconAtlas platformIcons_;
    bool sdlBackendInitialized_{false};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    float displayScale_{1.0F};
    bool enhancedVisualEffects_{true};
};

} // namespace px::desktop
