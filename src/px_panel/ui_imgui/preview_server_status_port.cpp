#include "server_status_port.h"

namespace px::panel::ui {
namespace {

class PreviewServerStatusPort final : public ServerStatusPort {
  public:
    ServerStatusState Snapshot() const override {
        return {
            .controllerDriverReady = true,
            .renderReady = true,
            .serviceReady = true,
            .addresses = {{"192.168.1.10", true}},
            .panelPort = 4999,
            .renderPort = 4601,
            .audioSamples = 48000,
            .audioChannels = 2,
            .audioBits = 16,
        };
    }
    void RestartRender() override {}
    void InstallControllerDriver() override {}
};

} // namespace

std::shared_ptr<ServerStatusPort> CreatePreviewServerStatusPort() {
    return std::make_shared<PreviewServerStatusPort>();
}

} // namespace px::panel::ui
