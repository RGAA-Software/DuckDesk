#include "panel_console_session.h"

#include "px_common/shared_preference.h"
#include "px_console_client/console_errors.h"
#include "px_console_client/console_user.h"
#include "px_console_client/console_user_api.h"
#include "px_console_client/console_user_device.h"

#include <Windows.h>
#include <wincred.h>

#include <algorithm>
#include <format>
#include <utility>

namespace px::panel::product {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count) != count)
        return {};
    return result;
}

} // namespace

std::shared_ptr<PanelConsoleSession> PanelConsoleSession::Create(const std::shared_ptr<PanelConfigStore>& config) {
    auto session = std::make_shared<PanelConsoleSession>(config);
    const auto preferences = SharedPreference::Instance();
    session->userId_ = preferences->Get("console_user:uid");
    session->username_ = preferences->Get("console_user:username");
    session->avatarPath_ = preferences->Get("console_user:avatar_path");
    return session;
}

PanelConsoleSession::PanelConsoleSession(std::shared_ptr<PanelConfigStore> config) : config_{std::move(config)} {}

ui::AccountSnapshot PanelConsoleSession::Account() const {
    const std::scoped_lock lock{mutex_};
    return {.loggedIn = !userId_.empty() && !username_.empty() && !ReadAccessToken().empty(),
            .username = username_,
            .avatarPath = avatarPath_,
            .operation = accountOperation_};
}

bool PanelConsoleSession::Login(const std::string& username, const std::string& password) {
    const auto endpoint = config_->Console();
    if (!endpoint)
        return false;
    auto result = px_console::ConsoleUserApi::Login(endpoint->host, endpoint->port, username, password);
    if (!result || !result.value().user || !WriteAccessToken(result.value().access_token))
        return false;
    const auto preferences = SharedPreference::Instance();
    {
        const std::scoped_lock lock{mutex_};
        userId_ = result.value().user->uid_;
        username_ = result.value().user->username_;
        avatarPath_ = result.value().user->avatar_path_;
        guestToken_.clear();
    }
    return preferences->Put("console_user:uid", result.value().user->uid_) &&
           preferences->Put("console_user:username", result.value().user->username_) &&
           preferences->Put("console_user:avatar_path", result.value().user->avatar_path_);
}

bool PanelConsoleSession::Register(const std::string& username, const std::string& password) {
    const auto endpoint = config_->Console();
    if (!endpoint)
        return false;
    auto [token, guest] = ResourceToken();
    if (!guest || token.empty())
        return false;
    const auto result = px_console::ConsoleUserApi::Register(endpoint->host, endpoint->port, token, username, password);
    return result.has_value() && Login(username, password);
}

bool PanelConsoleSession::UpdateProfile(const std::string& username) {
    const auto endpoint = config_->Console();
    const auto token = ReadAccessToken();
    if (!endpoint || token.empty() || username.empty())
        return false;
    const auto result = px_console::ConsoleUserApi::UpdateProfile(endpoint->host, endpoint->port, token, username);
    if (!result || !result.value())
        return false;
    {
        const std::scoped_lock lock{mutex_};
        username_ = result.value()->username_;
    }
    return SharedPreference::Instance()->Put("console_user:username", result.value()->username_);
}

bool PanelConsoleSession::UpdatePassword(const std::string& currentPassword, const std::string& newPassword) {
    const auto endpoint = config_->Console();
    const auto token = ReadAccessToken();
    if (!endpoint || token.empty() || currentPassword.empty() || newPassword.empty())
        return false;
    const auto result = px_console::ConsoleUserApi::UpdatePassword(endpoint->host, endpoint->port, token, currentPassword, newPassword);
    if (!result || !result.value().user || !WriteAccessToken(result.value().access_token))
        return false;
    {
        const std::scoped_lock lock{mutex_};
        username_ = result.value().user->username_;
        avatarPath_ = result.value().user->avatar_path_;
    }
    return SharedPreference::Instance()->Put("console_user:username", result.value().user->username_) &&
           SharedPreference::Instance()->Put("console_user:avatar_path", result.value().user->avatar_path_);
}

bool PanelConsoleSession::UpdateAvatar(const std::string& imagePath) {
    const auto endpoint = config_->Console();
    const auto token = ReadAccessToken();
    if (!endpoint || token.empty() || imagePath.empty())
        return false;
    const auto result = px_console::ConsoleUserApi::UpdateAvatar(endpoint->host, endpoint->port, token, imagePath);
    if (!result || !result.value())
        return false;
    {
        const std::scoped_lock lock{mutex_};
        avatarPath_ = result.value()->avatar_path_;
    }
    return SharedPreference::Instance()->Put("console_user:avatar_path", result.value()->avatar_path_);
}

bool PanelConsoleSession::Logout() {
    const auto endpoint = config_->Console();
    const auto token = ReadAccessToken();
    const bool remoteResult = endpoint && !token.empty() && px_console::ConsoleUserApi::Logout(endpoint->host, endpoint->port, token).has_value();
    DeleteAccessToken();
    const auto preferences = SharedPreference::Instance();
    static_cast<void>(preferences->Remove("console_user:uid"));
    static_cast<void>(preferences->Remove("console_user:username"));
    static_cast<void>(preferences->Remove("console_user:avatar_path"));
    {
        const std::scoped_lock lock{mutex_};
        userId_.clear();
        username_.clear();
        avatarPath_.clear();
        guestToken_.clear();
    }
    return remoteResult || token.empty();
}

void PanelConsoleSession::SetAccountOperation(const ui::AccountOperationState operation) {
    const std::scoped_lock lock{mutex_};
    accountOperation_ = operation;
}

std::vector<std::shared_ptr<px_console::ConsoleUserDevice>> PanelConsoleSession::QueryDevices() {
    const auto endpoint = config_->Console();
    const auto token = ReadAccessToken();
    if (!endpoint || token.empty())
        return {};
    auto result = px_console::ConsoleUserDeviceApi::QueryUserBindDevices(endpoint->host, endpoint->port, token);
    return result ? result.value() : std::vector<std::shared_ptr<px_console::ConsoleUserDevice>>{};
}

std::optional<px_console::ConsoleNativeDeviceConnection> PanelConsoleSession::QueryNativeDeviceConnection(const std::string& deviceId) {
    const auto endpoint = config_->Console();
    const auto token = ReadAccessToken();
    if (!endpoint || token.empty())
        return std::nullopt;
    auto result = px_console::ConsoleUserDeviceApi::QueryNativeConnection(endpoint->host, endpoint->port, token, deviceId);
    return result ? std::optional{std::move(result.value())} : std::nullopt;
}

std::vector<px_console::ConsoleUserApplication> PanelConsoleSession::QueryApplications() {
    const auto endpoint = config_->Console();
    if (!endpoint)
        return {};
    auto [token, guest] = ResourceToken();
    if (token.empty())
        return {};
    auto result = px_console::ConsoleUserAppApi::QueryApps(endpoint->host, endpoint->port, token, guest);
    if (!result) {
        return {};
    }
    auto applications = std::move(result.value());
    if (!guest) {
        return applications;
    }
    const auto instances = px_console::ConsoleUserAppApi::QueryInstances(endpoint->host, endpoint->port, token, true);
    if (!instances) {
        return applications;
    }
    for (const auto& instance : instances.value()) {
        if (instance.state != "starting" && instance.state != "running" && instance.state != "stopping") {
            continue;
        }
        const auto application = std::ranges::find(applications, instance.app_id, &px_console::ConsoleUserApplication::app_id);
        if (application != applications.end() && !application->running_instance) {
            application->running_instance = std::make_shared<px_console::ConsoleUserAppInstance>(instance);
        }
    }
    return applications;
}

px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError> PanelConsoleSession::StartApplication(const std::string& appId,
                                                                                                                  const std::string& nonce) {
    const auto endpoint = config_->Console();
    if (!endpoint)
        return std::unexpected{px_console::ConsoleApiError::kInvalidHostAddress};
    auto [token, guest] = ResourceToken();
    if (token.empty())
        return std::unexpected{px_console::ConsoleApiError::kAuthenticationRequired};
    return px_console::ConsoleUserAppApi::StartApp(endpoint->host, endpoint->port, token, appId, nonce, guest);
}

px::Result<px_console::ConsoleNativeApplicationConnection, px_console::ConsoleApiError>
PanelConsoleSession::QueryNativeApplicationConnection(const std::string& instanceId, const bool viewOnly) {
    const auto endpoint = config_->Console();
    if (!endpoint)
        return std::unexpected{px_console::ConsoleApiError::kInvalidHostAddress};
    auto [token, guest] = ResourceToken();
    if (token.empty())
        return std::unexpected{px_console::ConsoleApiError::kAuthenticationRequired};
    return px_console::ConsoleUserAppApi::QueryNativeConnection(endpoint->host, endpoint->port, token, instanceId, viewOnly, guest);
}

bool PanelConsoleSession::StopApplication(const std::string& instanceId) {
    const auto endpoint = config_->Console();
    if (!endpoint)
        return false;
    auto [token, guest] = ResourceToken();
    return !token.empty() && px_console::ConsoleUserAppApi::StopInstance(endpoint->host, endpoint->port, token, instanceId, guest).has_value();
}

std::tuple<std::string, bool> PanelConsoleSession::ResourceToken() {
    if (auto token = ReadAccessToken(); !token.empty())
        return {std::move(token), false};
    const auto endpoint = config_->Console();
    if (!endpoint)
        return {{}, true};
    {
        const std::scoped_lock lock{mutex_};
        if (!guestToken_.empty())
            return {guestToken_, true};
    }
    const auto nonce = std::format("{}-{}", GetCurrentProcessId(), GetTickCount64());
    auto result = px_console::ConsoleUserAppApi::CreateGuestSession(endpoint->host, endpoint->port, nonce);
    if (!result)
        return {{}, true};
    const std::scoped_lock lock{mutex_};
    if (guestToken_.empty())
        guestToken_ = result.value();
    return {guestToken_, true};
}

std::wstring PanelConsoleSession::CredentialTarget() const {
    const auto endpoint = config_->Console();
    return Utf8ToWide(endpoint ? std::format("Pixels.Console.UserSession.{}:{}", endpoint->host, endpoint->port)
                               : std::string{"Pixels.Console.UserSession.Unconfigured"});
}

std::string PanelConsoleSession::ReadAccessToken() const {
    PCREDENTIALW credential{}; // NOLINT(gammaray-raw-pointer-boundary): WinCred output parameter, immediately freed
    const auto target = CredentialTarget();
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return {};
    const std::string token{reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize};
    CredFree(credential);
    return token;
}

bool PanelConsoleSession::WriteAccessToken(const std::string& token) const {
    auto target = CredentialTarget();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(token.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(token.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"Pixels Console user session");
    return CredWriteW(&credential, 0) != FALSE;
}

void PanelConsoleSession::DeleteAccessToken() const {
    const auto target = CredentialTarget();
    static_cast<void>(CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0));
}

} // namespace px::panel::product
