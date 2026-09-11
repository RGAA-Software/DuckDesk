#pragma once

#include <expected>
#include <memory>
#include <string>

struct SDL_Window;

namespace px::desktop {

class WindowHost final {
  public:
    static std::expected<WindowHost, std::string> Create(const std::string& title, int width, int height);

    WindowHost(WindowHost&&) noexcept;
    WindowHost& operator=(WindowHost&&) noexcept;
    ~WindowHost();

    WindowHost(const WindowHost&) = delete;
    WindowHost& operator=(const WindowHost&) = delete;

    SDL_Window& Native() const noexcept;
    void Minimize() const;
    void ToggleMaximize() const;
    bool IsMaximized() const;
    float DisplayScale() const;

  private:
    struct Impl;

    explicit WindowHost(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
