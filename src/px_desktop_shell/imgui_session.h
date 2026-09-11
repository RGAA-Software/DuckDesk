#pragma once

#include <expected>
#include <functional>
#include <string>

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

  private:
    ImGuiSession(std::reference_wrapper<D3d11Renderer> renderer, bool sdlBackendInitialized) noexcept;

    std::reference_wrapper<D3d11Renderer> renderer_;
    bool sdlBackendInitialized_{false};
};

} // namespace px::desktop
