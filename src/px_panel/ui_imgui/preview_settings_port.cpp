#include "settings_port.h"

#include <mutex>

namespace px::panel::ui {
namespace {

class PreviewSettingsPort final : public SettingsPort {
  public:
    SettingsSnapshot Snapshot() const override {
        const std::scoped_lock lock{mutex_};
        return state_;
    }
    GeneralSaveResult SaveGeneral(const GeneralSettings& settings) override {
        if (settings.bitrateMbps <= 0) {
            return GeneralSaveResult::InvalidBitrate;
        }
        if (settings.frameRate < 15 || settings.frameRate > 144) {
            return GeneralSaveResult::InvalidFrameRate;
        }
        if (settings.resizeEnabled && (settings.width < 600 || settings.height < 200 || settings.width % 2 != 0 || settings.height % 2 != 0)) {
            return GeneralSaveResult::InvalidResolution;
        }
        const std::scoped_lock lock{mutex_};
        state_.general = settings;
        return GeneralSaveResult::Saved;
    }
    void RestartRender() override {}
    void SaveController(const ControllerSettings& settings) override {
        const std::scoped_lock lock{mutex_};
        state_.controller = settings;
    }
    void SetDisconnectAutoLock(const bool enabled) override {
        const std::scoped_lock lock{mutex_};
        state_.disconnectAutoLock = enabled;
    }
    bool SetSecurityPassword(const std::string& password, const std::string& confirmation) override {
        const bool accepted{!password.empty() && password == confirmation};
        if (accepted) {
            const std::scoped_lock lock{mutex_};
            state_.passwordUpdate = PasswordUpdateState::Updated;
        }
        return accepted;
    }
    void ClearData() override {}
    void CheckForUpdates() override {}
    void SetLanguage(const px::ui::Language language) override {
        const std::scoped_lock lock{mutex_};
        state_.language = language;
    }
    void SetTheme(const px::ui::Theme theme) override {
        const std::scoped_lock lock{mutex_};
        state_.theme = theme;
    }
    void CollectLogs(const std::string& destinationDirectory) override {
        const std::scoped_lock lock{mutex_};
        state_.logDestination = destinationDirectory;
        state_.logCollection = destinationDirectory.empty() ? LogCollectionState::Failed : LogCollectionState::Completed;
    }

  private:
    mutable std::mutex mutex_{};
    SettingsSnapshot state_{.version = "preview"};
};

} // namespace

std::shared_ptr<SettingsPort> CreatePreviewSettingsPort() {
    return std::make_shared<PreviewSettingsPort>();
}

} // namespace px::panel::ui
