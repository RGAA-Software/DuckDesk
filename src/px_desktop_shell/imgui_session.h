#pragma once

#include <expected>
#include <functional>
#include <string>

#include "px_ui/px_ui_theme.h"

namespace px::desktop {

class D3d11Renderer;
class WindowHost;

class ImGuiSession final {
  public:
    static std::expected<ImGuiSession, std::string> Create(WindowHost& window, D3d11Renderer& renderer);

    ImGuiSession(ImGuiSession&&) noexcept;
    ImGuiSession& operator=(ImGuiSession&&) = delete;
    ~ImGuiSession();

    ImGuiSession(const ImGuiSession&) = delete;
    ImGuiSession& operator=(const ImGuiSession&) = delete;

    void BeginFrame() const;
    bool NeedsInteractiveRefresh() const;
    bool ApplyAppearance(px::ui::Theme theme, float displayScale);

  private:
    ImGuiSession(std::reference_wrapper<D3d11Renderer> renderer, bool sdlBackendInitialized) noexcept;

    std::reference_wrapper<D3d11Renderer> renderer_;
    bool sdlBackendInitialized_{false};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    float displayScale_{1.0F};
};

} // namespace px::desktop
