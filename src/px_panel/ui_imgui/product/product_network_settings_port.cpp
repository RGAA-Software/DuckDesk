#include "panel_product_runtime.h"

#include "px_console_client/console_device.h"
#include "px_console_client/console_device_api.h"

#include <Windows.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>

namespace px::panel::product {
namespace {

std::string DefaultDeviceName() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    DWORD length{static_cast<DWORD>(buffer.size())};
    if (!GetComputerNameW(buffer.data(), &length))
        return "Pixels Node";
    const int count = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return "Pixels Node";
    std::string result(static_cast<std::size_t>(count), '\0');
    return WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(length), result.data(), count, nullptr, nullptr) == count ? result
                                                                                                                                     : "Pixels Node";
}

bool ValidPublicAddress(const std::string& value) {
    if (value.empty())
        return true;
    return !value.contains("://") && value.find_first_of(" /\\?#@") == std::string::npos && value != "0.0.0.0" && value != "::";
}

class ProductNetworkSettingsPort final : public ui::NetworkSettingsPort, public std::enable_shared_from_this<ProductNetworkSettingsPort> {
  public:
    explicit ProductNetworkSettingsPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {
        const auto ports = runtime_->Config()->Ports();
        state_.settings = {.authorizationInfo = runtime_->Config()->Authorization(),
                           .nodePublicAddress = runtime_->Config()->NodePublicAddress(),
                           .serviceManagementPort = ports.service,
                           .desktopConnectionPort = ports.desktop,
                           .applicationPorts = {ports.applicationFirst, ports.applicationLast},
                           .rtcPorts = {ports.rtcFirst, ports.rtcLast},
                           .panelListeningPort = ports.panel};
        ApplyEndpoint(runtime_->Config()->Console());
    }

    ui::NetworkSettingsState Snapshot() const override {
        const std::scoped_lock lock{mutex_};
        return state_;
    }

    void ParseAuthorization(std::string authorizationInfo) override {
        const auto endpoint = runtime_->Config()->ParseAuthorization(authorizationInfo);
        const std::scoped_lock lock{mutex_};
        state_.settings.authorizationInfo = std::move(authorizationInfo);
        ApplyEndpointLocked(endpoint);
        state_.operation = endpoint ? ui::NetworkOperation::Idle : ui::NetworkOperation::InvalidAuthorization;
        state_.detail.clear();
    }

    void Verify(std::string authorizationInfo) override {
        const auto endpoint = runtime_->Config()->ParseAuthorization(authorizationInfo);
        if (!endpoint) {
            SetFailure(ui::NetworkOperation::InvalidAuthorization, {});
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            state_.settings.authorizationInfo = std::move(authorizationInfo);
            ApplyEndpointLocked(endpoint);
            state_.operation = ui::NetworkOperation::Verifying;
            state_.detail.clear();
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductNetworkSettingsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, endpoint = *endpoint] {
            const auto result = px_console::ConsoleDeviceApi::Ping(endpoint.host, endpoint.port, endpoint.appKey);
            const auto self = weakSelf.lock();
            if (!self)
                return;
            if (result && result.value())
                self->SetFailure(ui::NetworkOperation::Verified, {});
            else
                self->SetFailure(ui::NetworkOperation::Failed, "Console verification failed");
        }));
    }

    void Save(std::string authorizationInfo, std::string nodePublicAddress) override {
        const auto endpoint = runtime_->Config()->ParseAuthorization(authorizationInfo);
        if (!endpoint) {
            SetFailure(ui::NetworkOperation::InvalidAuthorization, {});
            return;
        }
        if (!ValidPublicAddress(nodePublicAddress)) {
            SetFailure(ui::NetworkOperation::InvalidPublicAddress, {});
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            state_.operation = ui::NetworkOperation::Saving;
            state_.detail.clear();
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductNetworkSettingsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, authorizationInfo = std::move(authorizationInfo),
                                                    nodePublicAddress = std::move(nodePublicAddress), endpoint = *endpoint] {
            if (!runtime->Config()->SaveNetwork(authorizationInfo, nodePublicAddress, endpoint)) {
                if (const auto self = weakSelf.lock())
                    self->SetFailure(ui::NetworkOperation::Failed, "Unable to save network settings");
                return;
            }
            auto identity = runtime->Config()->Identity();
            bool identityReady = !identity.deviceId.empty();
            if (identityReady) {
                const auto queried = px_console::ConsoleDeviceApi::QueryDevice(endpoint.host, endpoint.port, endpoint.appKey, identity.deviceId);
                identityReady = queried.has_value() && queried.value();
            }
            if (!identityReady) {
                const auto created =
                    px_console::ConsoleDeviceApi::RequestNewDevice(endpoint.host, endpoint.port, endpoint.appKey, DefaultDeviceName(), "");
                if (!created || !created.value()) {
                    if (const auto self = weakSelf.lock())
                        self->SetFailure(ui::NetworkOperation::Failed, "Device registration failed");
                    return;
                }
                identity = {.deviceId = created.value()->device_id_,
                            .deviceName = created.value()->device_name_,
                            .randomPassword = created.value()->gen_random_pwd_,
                            .securityPasswordHash = identity.securityPasswordHash};
                if (!runtime->Config()->SaveIdentity(identity)) {
                    if (const auto self = weakSelf.lock())
                        self->SetFailure(ui::NetworkOperation::Failed, "Unable to save device identity");
                    return;
                }
            }
            const auto self = weakSelf.lock();
            if (!self)
                return;
            {
                const std::scoped_lock lock{self->mutex_};
                self->state_.settings.authorizationInfo = std::move(authorizationInfo);
                self->state_.settings.nodePublicAddress = std::move(nodePublicAddress);
                self->ApplyEndpointLocked(endpoint);
                self->state_.operation = ui::NetworkOperation::SavedNeedsRestart;
                self->state_.detail.clear();
            }
            runtime->Notify(false, "Pixels", "Network settings saved");
        }));
    }

    void RestartRender() override {
        if (!runtime_->Service()->RestartRender())
            runtime_->Notify(true, "Pixels", "Render service is not connected");
        Acknowledge();
    }

    void Acknowledge() override {
        SetFailure(ui::NetworkOperation::Idle, {});
    }

  private:
    void ApplyEndpoint(const std::optional<ConsoleEndpoint>& endpoint) {
        const std::scoped_lock lock{mutex_};
        ApplyEndpointLocked(endpoint);
    }
    void ApplyEndpointLocked(const std::optional<ConsoleEndpoint>& endpoint) {
        state_.settings.consolePort = endpoint ? std::optional{endpoint->port} : std::nullopt;
        state_.settings.relayPort = endpoint ? std::optional{endpoint->relayPort} : std::nullopt;
    }
    void SetFailure(const ui::NetworkOperation operation, std::string detail) {
        const std::scoped_lock lock{mutex_};
        state_.operation = operation;
        state_.detail = std::move(detail);
    }

    std::shared_ptr<PanelProductRuntime> runtime_{};
    mutable std::mutex mutex_{};
    ui::NetworkSettingsState state_{};
};

} // namespace

std::shared_ptr<ui::NetworkSettingsPort> CreateProductNetworkSettingsPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductNetworkSettingsPort>(runtime);
}

} // namespace px::panel::product
