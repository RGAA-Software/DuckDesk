#include <mutex>
#include <optional>
#include <utility>

#include "panel_product_runtime.h"
#include "px_console_client/console_api.h"
#include "px_ui/product_brand.h"

namespace px::panel::product {
namespace {

class ProductNetworkSettingsPort final : public ui::NetworkSettingsPort, public std::enable_shared_from_this<ProductNetworkSettingsPort> {
public:
    explicit ProductNetworkSettingsPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {
        const auto ports = runtime_->Config()->Ports();
        state_.settings = {.consoleAddress = runtime_->Config()->ConsoleAddress(),
                           .consoleAddressEditable = runtime_->Config()->ConsoleAddressEditable(),
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

    void ParseConsoleAddress(std::string consoleAddress) override {
        const auto endpoint = runtime_->Config()->ParseConsoleAddress(consoleAddress);
        const std::scoped_lock lock{mutex_};
        state_.settings.consoleAddress = std::move(consoleAddress);
        ApplyEndpointLocked(endpoint);
        state_.operation = endpoint ? ui::NetworkOperation::Idle : ui::NetworkOperation::InvalidConsoleAddress;
        state_.detail.clear();
    }

    void Verify(std::string consoleAddress) override {
        const auto endpoint = runtime_->Config()->ParseConsoleAddress(consoleAddress);
        if (!endpoint) {
            SetFailure(ui::NetworkOperation::InvalidConsoleAddress, {});
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            state_.settings.consoleAddress = std::move(consoleAddress);
            ApplyEndpointLocked(endpoint);
            state_.operation = ui::NetworkOperation::Verifying;
            state_.detail.clear();
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductNetworkSettingsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, endpoint = *endpoint] {
            const auto result = px_console::QueryConsoleReady(endpoint.host, endpoint.port);
            const auto self = weakSelf.lock();
            if (!self) return;
            if (result)
                self->SetFailure(ui::NetworkOperation::Verified, {});
            else
                self->SetFailure(ui::NetworkOperation::Failed, "Console verification failed");
        }));
    }

    void Save(std::string consoleAddress) override {
        const auto endpoint = runtime_->Config()->ParseConsoleAddress(consoleAddress);
        if (!endpoint) {
            SetFailure(ui::NetworkOperation::InvalidConsoleAddress, {});
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            state_.operation = ui::NetworkOperation::Saving;
            state_.detail.clear();
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductNetworkSettingsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, consoleAddress = endpoint->baseUrl, endpoint = *endpoint] {
            if (!px_console::QueryConsoleReady(endpoint.host, endpoint.port).value_or(false)) {
                if (const auto self = weakSelf.lock()) self->SetFailure(ui::NetworkOperation::Failed, "Console is not ready");
                return;
            }
            if (!runtime->Config()->SaveNetwork(consoleAddress, endpoint)) {
                if (const auto self = weakSelf.lock()) self->SetFailure(ui::NetworkOperation::Failed, "Unable to save network settings");
                return;
            }
            runtime->Console()->ForgetAccountIfConsoleChanged(consoleAddress);
            const auto self = weakSelf.lock();
            if (!self) return;
            {
                const std::scoped_lock lock{self->mutex_};
                self->state_.settings.consoleAddress = std::move(consoleAddress);
                self->ApplyEndpointLocked(endpoint);
                self->state_.operation = ui::NetworkOperation::SavedNeedsRestart;
                self->state_.detail.clear();
            }
            runtime->Notify(false, std::string{px::ui::ApplicationName()}, "Network settings saved");
        }));
    }

    void RestartRender() override {
        const auto service = runtime_->Service();
        if (!service || !service->RestartRender()) {
            runtime_->Notify(true, std::string{px::ui::ApplicationName()}, "Render service is not connected");
        }
        Acknowledge();
    }

    void Acknowledge() override { SetFailure(ui::NetworkOperation::Idle, {}); }

private:
    void ApplyEndpoint(const std::optional<ConsoleEndpoint>& endpoint) {
        const std::scoped_lock lock{mutex_};
        ApplyEndpointLocked(endpoint);
    }
    void ApplyEndpointLocked(const std::optional<ConsoleEndpoint>& endpoint) {
        state_.settings.consolePort = endpoint ? std::optional{endpoint->port} : std::nullopt;
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

}  // namespace

std::shared_ptr<ui::NetworkSettingsPort> CreateProductNetworkSettingsPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductNetworkSettingsPort>(runtime);
}

}  // namespace px::panel::product
