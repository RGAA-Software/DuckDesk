#pragma once

#include <expected>
#include <memory>
#include <string>

struct SDL_Window;

namespace px::desktop {

struct WindowChromeConfig final {
    bool showMinimizeButton{true};
    bool showMaximizeButton{true};
    bool allowTitleBarMaximize{true};
    bool useRoundedWindow{false};
    bool resizable{true};
};

class WindowHost final {
  public:
    static std::expected<WindowHost, std::string> Create(const std::string& title, int width, int height, bool initiallyVisible,
                                                         bool requestVulkanSurface, const WindowChromeConfig& chrome);

    WindowHost(WindowHost&&) noexcept;
    WindowHost& operator=(WindowHost&&) noexcept;
    ~WindowHost();

    WindowHost(const WindowHost&) = delete;
    WindowHost& operator=(const WindowHost&) = delete;

    SDL_Window& Native() const noexcept;
    void Minimize() const;
    void ToggleMaximize() const;
    bool ToggleFullscreen();
    bool IsMaximized() const;
    [[nodiscard]] bool VulkanSurfaceAvailable() const noexcept;
    float DisplayScale() const;
    void Hide() const;
    void ShowAndRaise() const;

  private:
    struct Impl;

    explicit WindowHost(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
