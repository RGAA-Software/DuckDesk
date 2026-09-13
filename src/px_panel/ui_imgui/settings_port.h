#pragma once

#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

#include <cstdint>
#include <memory>
#include <string>

namespace px::panel::ui {

enum class VideoCodec : std::uint8_t { H264, H265 };
enum class PreferredDecoder : std::uint8_t { Automatic, Hardware, Software };
enum class PasswordUpdateState : std::uint8_t { Idle, Updating, Updated, RemoteFailed };
enum class LogCollectionState : std::uint8_t { Idle, Collecting, Completed, Failed };
enum class GeneralSaveResult : std::uint8_t { Saved, InvalidBitrate, InvalidFrameRate, InvalidResolution, UnsupportedResolution, InvalidAspectRatio };

struct GeneralSettings final {
    int bitrateMbps{10};
    int frameRate{60};
    VideoCodec codec{VideoCodec::H264};
    bool resizeEnabled{false};
    int width{1920};
    int height{1080};
    bool captureAudio{true};
};

struct ControllerSettings final {
    bool maximizeClient{false};
    bool displayClientLogo{true};
    bool colorfulTitleBar{true};
    int maximumScreens{2};
    PreferredDecoder preferredDecoder{PreferredDecoder::Automatic};
    std::string recordingPath{};
};

struct SettingsSnapshot final {
    GeneralSettings general{};
    ControllerSettings controller{};
    bool disconnectAutoLock{false};
    std::string version{};
    px::ui::Language language{px::ui::Language::SimplifiedChinese};
    px::ui::Theme theme{px::ui::Theme::Dark};
    bool enhancedVisualEffects{true};
    PasswordUpdateState passwordUpdate{PasswordUpdateState::Idle};
    LogCollectionState logCollection{LogCollectionState::Idle};
    std::string logDestination{};
};

class SettingsPort {
  public:
    virtual ~SettingsPort() = default;
    virtual SettingsSnapshot Snapshot() const = 0;
    virtual GeneralSaveResult SaveGeneral(const GeneralSettings& settings) = 0;
    virtual void RestartRender() = 0;
    virtual void SaveController(const ControllerSettings& settings) = 0;
    virtual void SetDisconnectAutoLock(bool enabled) = 0;
    virtual bool SetSecurityPassword(const std::string& password, const std::string& confirmation) = 0;
    virtual void ClearData() = 0;
    virtual void CheckForUpdates() = 0;
    virtual void SetLanguage(px::ui::Language language) = 0;
    virtual void SetTheme(px::ui::Theme theme) = 0;
    virtual void SetEnhancedVisualEffects(bool enabled) = 0;
    virtual void CollectLogs(const std::string& destinationDirectory) = 0;
};

std::shared_ptr<SettingsPort> CreatePreviewSettingsPort();

} // namespace px::panel::ui
