#include "panel_product_runtime.h"
#include "version_config.h"

#include <utility>

namespace px::panel::product {

std::shared_ptr<PanelProductRuntime> PanelProductRuntime::Create(const std::filesystem::path& executableDirectory,
                                                                 const std::shared_ptr<ui::NotificationCenter>& notifications) {
    const auto config = PanelConfigStore::Create(executableDirectory);
    if (!config)
        return {};
    const auto console = PanelConsoleSession::Create(config);
    const auto launcher = PanelClientLauncher::Create(config);
    const auto auditStore = PanelAuditStore::Create(config->DataDirectory());
    if (!auditStore)
        return {};
    const auto localServer = PanelLocalServer::Create(config, auditStore);
    if (!localServer || !localServer->Snapshot().listening)
        return {};
    std::shared_ptr<PanelServiceBridge> service{};
    std::shared_ptr<PanelNodePresence> nodePresence{};
    std::shared_ptr<PanelOsInfoSupervisor> osInfoSupervisor{};
#if PX_CAPABILITY_DESKTOP_HOST
    service = PanelServiceBridge::Create(config);
    const std::weak_ptr<PanelServiceBridge> weakService{service};
    localServer->SetRestartHandler([weakService] {
        if (const auto activeService = weakService.lock())
            static_cast<void>(activeService->RestartRender());
    });
    nodePresence = PanelNodePresence::Create(config);
#elif PX_CAPABILITY_SYSTEM_INFORMATION
    osInfoSupervisor = PanelOsInfoSupervisor::Create(executableDirectory, config->Ports().panel);
    if (!osInfoSupervisor)
        return {};
#endif
    const auto worker = PanelWorker::Create();
    return std::make_shared<PanelProductRuntime>(config, console, launcher, service, localServer, nodePresence, osInfoSupervisor, auditStore, worker,
                                                 notifications);
}

PanelProductRuntime::PanelProductRuntime(std::shared_ptr<PanelConfigStore> config, std::shared_ptr<PanelConsoleSession> console,
                                         std::shared_ptr<PanelClientLauncher> launcher, std::shared_ptr<PanelServiceBridge> service,
                                         std::shared_ptr<PanelLocalServer> localServer, std::shared_ptr<PanelNodePresence> nodePresence,
                                         std::shared_ptr<PanelOsInfoSupervisor> osInfoSupervisor, std::shared_ptr<PanelAuditStore> auditStore,
                                         std::shared_ptr<PanelWorker> worker, std::shared_ptr<ui::NotificationCenter> notifications)
    : config_{std::move(config)}, console_{std::move(console)}, launcher_{std::move(launcher)}, service_{std::move(service)},
      localServer_{std::move(localServer)}, nodePresence_{std::move(nodePresence)}, osInfoSupervisor_{std::move(osInfoSupervisor)},
      auditStore_{std::move(auditStore)}, worker_{std::move(worker)}, notifications_{std::move(notifications)} {}

PanelProductRuntime::~PanelProductRuntime() {
    if (worker_)
        worker_->Stop();
    if (nodePresence_)
        nodePresence_->Stop();
#if PX_CAPABILITY_SYSTEM_INFORMATION && !PX_CAPABILITY_DESKTOP_HOST
    if (osInfoSupervisor_)
        osInfoSupervisor_->Stop();
#endif
    if (service_)
        service_->Stop();
    if (localServer_)
        localServer_->Stop();
}

const std::shared_ptr<PanelConfigStore>& PanelProductRuntime::Config() const {
    return config_;
}
const std::shared_ptr<PanelConsoleSession>& PanelProductRuntime::Console() const {
    return console_;
}
const std::shared_ptr<PanelClientLauncher>& PanelProductRuntime::Launcher() const {
    return launcher_;
}
const std::shared_ptr<PanelServiceBridge>& PanelProductRuntime::Service() const {
    return service_;
}
const std::shared_ptr<PanelLocalServer>& PanelProductRuntime::LocalServer() const {
    return localServer_;
}
const std::shared_ptr<PanelAuditStore>& PanelProductRuntime::AuditStore() const {
    return auditStore_;
}
const std::shared_ptr<PanelWorker>& PanelProductRuntime::Worker() const {
    return worker_;
}
const std::shared_ptr<ui::NotificationCenter>& PanelProductRuntime::Notifications() const {
    return notifications_;
}

void PanelProductRuntime::Notify(const bool error, std::string title, std::string message) const {
    if (notifications_)
        notifications_->Publish({.level = error ? ui::NotificationLevel::Error : ui::NotificationLevel::Information,
                                 .title = std::move(title),
                                 .message = std::move(message)});
}

} // namespace px::panel::product
