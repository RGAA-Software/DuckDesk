#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "px_ui/px_ui_theme.h"

namespace px::desktop {

struct DesktopInputEvent final {
    std::uint32_t type{};
    std::int32_t key{};
    std::int32_t scanCode{};
    std::uint8_t mouseButton{};
    float x{};
    float y{};
    float wheelX{};
    float wheelY{};
    std::string text{};
};

struct WindowConfig {
    std::string title{"Pixels"};
    int width{1180};
    int height{760};
    bool initiallyVisible{true};
    bool minimizeToTray{false};
    bool continuousTextInput{false};
    bool continuousRendering{false};
};

class DesktopShell final {
  public:
    using RenderCallback = std::function<void()>;
    using InputCallback = std::function<void(const DesktopInputEvent&)>;

    static std::expected<DesktopShell, std::string> Create(const WindowConfig& config);

    DesktopShell(DesktopShell&&) noexcept;
    DesktopShell& operator=(DesktopShell&&) noexcept;
    ~DesktopShell();

    DesktopShell(const DesktopShell&) = delete;
    DesktopShell& operator=(const DesktopShell&) = delete;

    int Run(const RenderCallback& render, const InputCallback& input = {});
    bool UpdateVideoTexture(int width, int height, std::span<const std::uint8_t> bgra);
    [[nodiscard]] std::uint64_t VideoTextureId() const noexcept;
    bool SetTheme(px::ui::Theme theme);
    bool ToggleFullscreen();
    void RequestExit() noexcept;
    void RequestShowAndRaise() noexcept;

  private:
    struct Impl;

    explicit DesktopShell(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
