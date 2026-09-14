#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

struct SDL_Window;

namespace px::desktop {

inline constexpr std::uint32_t kShowAndRaiseWindowMessage{0x8050U};

class WindowsTitleBarBehavior final {
  public:
    static std::expected<WindowsTitleBarBehavior, std::string> Create(SDL_Window& window, bool allowTitleBarMaximize, bool useRoundedWindow,
                                                                      bool resizable);

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
