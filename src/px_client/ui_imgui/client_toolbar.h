#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace px::desktop {
class BrandLogo;
struct DesktopInputEvent;
} // namespace px::desktop

namespace px::client::imgui {

class ClientFileTransferPanel;
class ClientSession;
struct ClientSessionSnapshot;

struct ClientToolbarAction final {
    bool toggleLanguage{};
    bool toggleTheme{};
    bool toggleEnhancedVisualEffects{};
    bool toggleFullscreen{};
};

class ClientToolbar final {
  public:
    ClientToolbar(std::shared_ptr<ClientFileTransferPanel> fileTransfer, bool enhancedVisualEffects);
    [[nodiscard]] ClientToolbarAction Draw(const std::shared_ptr<ClientSession>& session, const px::desktop::BrandLogo& logo, bool english,
                                           bool darkTheme);
    [[nodiscard]] bool CapturesPointer(float x, float y) const noexcept;
    [[nodiscard]] bool HandlePointerEvent(const px::desktop::DesktopInputEvent& event);

  private:
    enum class Section : std::uint8_t { Display, Control, Tools, Voice, Settings };

    [[nodiscard]] bool DrawLauncher(const px::desktop::BrandLogo& logo);
    [[nodiscard]] bool DrawNavigation(const ClientSessionSnapshot& snapshot, const px::desktop::BrandLogo& logo, bool english);
    [[nodiscard]] bool DrawSection(const std::shared_ptr<ClientSession>& session, const ClientSessionSnapshot& snapshot, bool english, bool darkTheme,
                                   ClientToolbarAction& action);

    struct Bounds final {
        float x{};
        float y{};
        float width{};
        float height{};

        [[nodiscard]] bool Contains(float pointX, float pointY) const noexcept;
    };

    std::shared_ptr<ClientFileTransferPanel> fileTransfer_{};
    Bounds launcherBounds_{};
    Bounds navigationBounds_{};
    Bounds sectionBounds_{};
    Section section_{Section::Display};
    bool expanded_{};
    bool sectionExpanded_{};
    bool navigationNeedsFocus_{};
    bool sectionNeedsFocus_{};
    bool dismissPointerDown_{};
    bool launcherPointerDown_{};
    bool launcherDragged_{};
    bool showStatistics_{};
    bool audioEnabled_{true};
    bool microphoneMuted_{};
    bool speakerMuted_{};
    bool enhancedVisualEffects_{true};
    std::string screenshotStatus_{};
    int frameRate_{60};
    int resolutionWidth_{};
    int resolutionHeight_{};
    float launcherX_{};
    float launcherY_{};
    float dragOriginX_{};
    float dragOriginY_{};
    float dragOriginLauncherX_{};
    float dragOriginLauncherY_{};
    float workX_{};
    float workY_{};
    float workWidth_{};
    float workHeight_{};
    float launcherDiameter_{};
    bool launcherPositionInitialized_{};
};

} // namespace px::client::imgui
