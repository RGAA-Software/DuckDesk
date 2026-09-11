#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "px_ui/px_ui_theme.h"

namespace px::desktop {

struct WindowConfig {
    std::string title{"Pixels"};
    int width{1180};
    int height{760};
};

class DesktopShell final {
  public:
    using RenderCallback = std::function<void()>;

    static std::expected<DesktopShell, std::string> Create(const WindowConfig& config);

    DesktopShell(DesktopShell&&) noexcept;
    DesktopShell& operator=(DesktopShell&&) noexcept;
    ~DesktopShell();

    DesktopShell(const DesktopShell&) = delete;
    DesktopShell& operator=(const DesktopShell&) = delete;

    int Run(const RenderCallback& render);
    bool SetTheme(px::ui::Theme theme);
    void RequestExit() noexcept;

  private:
    struct Impl;

    explicit DesktopShell(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
