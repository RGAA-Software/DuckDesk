#include "panel_product_runtime.h"

#include "px_common/md5.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_device.h"

#include <filesystem>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <mutex>
#include <utility>

namespace px::panel::product {
namespace {

class ProductSettingsPort final : public ui::SettingsPort, public std::enable_shared_from_this<ProductSettingsPort> {
  public:
    explicit ProductSettingsPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {
        logDestination_ = (runtime_->Config()->DataDirectory().parent_path().parent_path() / "Desktop").string();
    }

    ui::SettingsSnapshot Snapshot() const override {
        auto result = runtime_->Config()->Settings();
        const std::scoped_lock lock{mutex_};
        result.passwordUpdate = passwordUpdate_;
        result.logCollection = logCollection_;
        result.logDestination = logDestination_;
        return result;
    }

    ui::GeneralSaveResult SaveGeneral(const ui::GeneralSettings& settings) override {
        if (settings.bitrateMbps <= 0)
            return ui::GeneralSaveResult::InvalidBitrate;
        if (settings.frameRate < 15 || settings.frameRate > 144)
            return ui::GeneralSaveResult::InvalidFrameRate;
        if (settings.resizeEnabled && (settings.width < 600 || settings.height < 200 || (settings.width & 1) != 0 || (settings.height & 1) != 0))
            return ui::GeneralSaveResult::InvalidResolution;
        if (settings.resizeEnabled) {
            const int maximumWidth{settings.codec == ui::VideoCodec::H264 ? 3840 : 7680};
            const int maximumHeight{settings.codec == ui::VideoCodec::H264 ? 2160 : 4320};
            if (settings.width > maximumWidth || settings.height > maximumHeight) return ui::GeneralSaveResult::UnsupportedResolution;
            const float ratio{static_cast<float>(std::max(settings.width, settings.height)) /
                              static_cast<float>(std::min(settings.width, settings.height))};
            if (ratio > 4.0F) return ui::GeneralSaveResult::InvalidAspectRatio;
        }
        return runtime_->Config()->SaveGeneral(settings) ? ui::GeneralSaveResult::Saved : ui::GeneralSaveResult::InvalidResolution;
    }

    void RestartRender() override {
        if (!runtime_->Service()->RestartRender())
            runtime_->Notify(true, "Pixels", "Render service is not connected");
    }
    void SaveController(const ui::ControllerSettings& settings) override {
        static_cast<void>(runtime_->Config()->SaveController(settings));
    }
    void SetDisconnectAutoLock(const bool enabled) override {
        static_cast<void>(runtime_->Config()->SaveDisconnectAutoLock(enabled));
    }
    bool SetSecurityPassword(const std::string& password, const std::string& confirmation) override {
        const bool valid = !password.empty() && password == confirmation;
        const std::string passwordHash{valid ? MD5::Hex(password) : std::string{}};
        if (!valid || !runtime_->Config()->SaveSecurityPasswordHash(passwordHash)) {
            const std::scoped_lock lock{mutex_};
            passwordUpdate_ = ui::PasswordUpdateState::RemoteFailed;
            return false;
        }
        runtime_->LocalServer()->RefreshPanelInfo();
        const auto endpoint = runtime_->Config()->Console();
        const auto identity = runtime_->Config()->Identity();
        if (!endpoint || identity.deviceId.empty()) {
            const std::scoped_lock lock{mutex_};
            passwordUpdate_ = ui::PasswordUpdateState::Updated;
            return true;
        }
        const auto generation = passwordGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
        {
            const std::scoped_lock lock{mutex_};
            passwordUpdate_ = ui::PasswordUpdateState::Updating;
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductSettingsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, endpoint = *endpoint, identity, passwordHash, generation] {
            const auto result = px_console::ConsoleDeviceApi::UpdateSafetyPwd(endpoint.host, endpoint.port, endpoint.appKey, identity.deviceId,
                                                                               passwordHash);
            const auto self = weakSelf.lock();
            if (!self || self->passwordGeneration_.load(std::memory_order_acquire) != generation) return;
            const bool updated = result && result.value() && result.value()->safety_pwd_md5_ == passwordHash;
            const std::scoped_lock lock{self->mutex_};
            self->passwordUpdate_ = updated ? ui::PasswordUpdateState::Updated : ui::PasswordUpdateState::RemoteFailed;
        }));
        return true;
    }
    void ClearData() override {
        runtime_->Launcher()->StopAll();
        static_cast<void>(runtime_->Console()->Logout());
        static_cast<void>(runtime_->AuditStore()->DeleteAll(ui::SecurityRecordKind::Visit));
        static_cast<void>(runtime_->AuditStore()->DeleteAll(ui::SecurityRecordKind::FileTransfer));
        runtime_->Config()->Clear();
        runtime_->LocalServer()->RefreshPanelInfo();
        static_cast<void>(runtime_->Service()->RestartRender());
        runtime_->Notify(false, "Pixels", "Local Panel data cleared");
    }
    void CheckForUpdates() override {
        runtime_->Notify(false, "Pixels", "This build is managed by the deployment package");
    }
    void SetLanguage(const ::px::ui::Language language) override {
        static_cast<void>(runtime_->Config()->SaveLanguage(language));
    }
    void SetTheme(const ::px::ui::Theme theme) override {
        static_cast<void>(runtime_->Config()->SaveTheme(theme));
    }
    void SetEnhancedVisualEffects(const bool enabled) override {
        static_cast<void>(runtime_->Config()->SaveEnhancedVisualEffects(enabled));
    }
    void CollectLogs(const std::string& destinationDirectory) override {
        if (destinationDirectory.empty()) {
            const std::scoped_lock lock{mutex_};
            logCollection_ = ui::LogCollectionState::Failed;
            return;
        }
        const std::filesystem::path source = runtime_->Config()->DataDirectory().parent_path() / "px_logs";
        const std::filesystem::path destination = std::filesystem::path{destinationDirectory} / "PixelsLogs";
        {
            const std::scoped_lock lock{mutex_};
            logCollection_ = ui::LogCollectionState::Collecting;
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductSettingsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([weakSelf, source, destination] {
            std::error_code error{};
            std::filesystem::create_directories(destination, error);
            if (!error && std::filesystem::exists(source)) {
                std::filesystem::copy(source, destination,
                                      std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
            }
            const auto self = weakSelf.lock();
            if (!self) return;
            const std::scoped_lock lock{self->mutex_};
            self->logCollection_ = error ? ui::LogCollectionState::Failed : ui::LogCollectionState::Completed;
            self->logDestination_ = destination.string();
        }));
    }

  private:
    std::shared_ptr<PanelProductRuntime> runtime_{};
    mutable std::mutex mutex_{};
    ui::PasswordUpdateState passwordUpdate_{ui::PasswordUpdateState::Idle};
    ui::LogCollectionState logCollection_{ui::LogCollectionState::Idle};
    std::string logDestination_{};
    std::atomic_uint64_t passwordGeneration_{};
};

} // namespace

std::shared_ptr<ui::SettingsPort> CreateProductSettingsPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductSettingsPort>(runtime);
}

} // namespace px::panel::product
