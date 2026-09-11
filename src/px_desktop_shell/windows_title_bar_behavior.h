#pragma once

#include <expected>
#include <memory>
#include <string>

struct SDL_Window;

namespace px::desktop {

class WindowsTitleBarBehavior final {
  public:
    static std::expected<WindowsTitleBarBehavior, std::string> Create(SDL_Window& window);

    WindowsTitleBarBehavior(WindowsTitleBarBehavior&&) noexcept;
    WindowsTitleBarBehavior& operator=(WindowsTitleBarBehavior&&) noexcept;
    ~WindowsTitleBarBehavior();

    WindowsTitleBarBehavior(const WindowsTitleBarBehavior&) = delete;
    WindowsTitleBarBehavior& operator=(const WindowsTitleBarBehavior&) = delete;

  private:
    struct Impl;

    explicit WindowsTitleBarBehavior(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
