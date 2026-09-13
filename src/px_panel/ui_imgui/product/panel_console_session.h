#pragma once

#include "account_port.h"
#include "panel_config_store.h"

#include "px_console_client/console_user_app_api.h"
#include "px_console_client/console_user_device_api.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace px::panel::product {

class PanelConsoleSession final {
  public:
    static std::shared_ptr<PanelConsoleSession> Create(const std::shared_ptr<PanelConfigStore>& config);

    explicit PanelConsoleSession(std::shared_ptr<PanelConfigStore> config);

    [[nodiscard]] ui::AccountSnapshot Account() const;
    bool Login(const std::string& username, const std::string& password);
    bool Register(const std::string& username, const std::string& password);
    bool UpdateProfile(const std::string& username);
    bool UpdatePassword(const std::string& currentPassword, const std::string& newPassword);
    bool UpdateAvatar(const std::string& imagePath);
    bool Logout();
    void SetAccountOperation(ui::AccountOperationState operation);

    [[nodiscard]] std::vector<std::shared_ptr<px_console::ConsoleUserDevice>> QueryDevices();
    [[nodiscard]] std::optional<px_console::ConsoleNativeDeviceConnection> QueryNativeDeviceConnection(const std::string& deviceId);
    [[nodiscard]] std::vector<px_console::ConsoleUserApplication> QueryApplications();
    [[nodiscard]] px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError> StartApplication(const std::string& appId,
                                                                                                               const std::string& nonce);
    [[nodiscard]] px::Result<px_console::ConsoleNativeApplicationConnection, px_console::ConsoleApiError>
    QueryNativeApplicationConnection(const std::string& instanceId, bool viewOnly);
    bool StopApplication(const std::string& instanceId);

  private:
    [[nodiscard]] std::tuple<std::string, bool> ResourceToken();
    [[nodiscard]] std::wstring CredentialTarget() const;
    [[nodiscard]] std::string ReadAccessToken() const;
    bool WriteAccessToken(const std::string& token) const;
    void DeleteAccessToken() const;

    std::shared_ptr<PanelConfigStore> config_{};
    mutable std::mutex mutex_{};
    std::string userId_{};
    std::string username_{};
    std::string avatarPath_{};
    std::string guestToken_{};
    ui::AccountOperationState accountOperation_{ui::AccountOperationState::Idle};
};

} // namespace px::panel::product
