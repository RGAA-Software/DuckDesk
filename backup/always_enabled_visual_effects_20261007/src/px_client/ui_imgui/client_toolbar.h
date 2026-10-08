#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "client_statistics_overlay.h"
#include "client_ui_settings.h"
#include "px_ui/components/feedback.h"

namespace px::desktop {
class BrandLogo;
struct DesktopInputEvent;
}  // namespace px::desktop

namespace px::client::imgui {

class ClientSession;
struct ClientSessionSnapshot;

struct ClientToolbarAction final {
    bool toggleLanguage{};
    bool toggleTheme{};
    bool toggleEnhancedVisualEffects{};
    bool toggleFullscreen{};
    bool requestExit{};
};

class ClientToolbar final {
public:
    ClientToolbar(bool enhancedVisualEffects, ClientUiSettings settings);
    [[nodiscard]] ClientToolbarAction Draw(const std::shared_ptr<ClientSession>& session, const px::desktop::BrandLogo& logo, bool english,
                                           bool darkTheme, bool fullscreen);
    [[nodiscard]] bool CapturesPointer(float x, float y) const noexcept;
    [[nodiscard]] bool HandlePointerEvent(const px::desktop::DesktopInputEvent& event);

private:
    enum class Section : std::uint8_t { Display, Control, Tools, Voice, Settings, Exit };

    [[nodiscard]] bool DrawLauncher(const px::desktop::BrandLogo& logo);
    [[nodiscard]] bool DrawNavigation(const ClientSessionSnapshot& snapshot, const px::desktop::BrandLogo& logo, bool english,
                                      ClientToolbarAction& action);
    [[nodiscard]] bool DrawSection(const std::shared_ptr<ClientSession>& session, const ClientSessionSnapshot& snapshot, bool english, bool darkTheme,
                                   bool fullscreen, ClientToolbarAction& action);

    struct Bounds final {
        float x{};
        float y{};
        float width{};
        float height{};

        [[nodiscard]] bool Contains(float pointX, float pointY) const noexcept;
    };

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
    ClientStatisticsOverlay statisticsOverlay_{};
    bool audioEnabled_{true};
    bool microphoneMuted_{};
    bool speakerMuted_{};
    bool enhancedVisualEffects_{true};
    px::ui::ToastHost captureToasts_{};
    std::optional<std::chrono::steady_clock::time_point> recordingStartedAt_{};
    int frameRate_{60};
    int resolutionWidth_{};
    int resolutionHeight_{};
    float launcherX_{};
    float launcherY_{};
    float dragOriginX_{};
    float dragOriginY_{};
    float dragOriginLauncherX_{};
    float dragOriginLauncherY_{};
    ControllerArea controllerArea_{};
    ClientUiSettings settings_;
    std::optional<ControllerPosition> launcherPosition_{};
};

}  // namespace px::client::imgui
