#pragma once

#include <memory>
#include <string>
#include <vector>

namespace px::panel::ui {

struct NetworkAddress final {
    std::string address{};
    bool wired{false};
};

struct ServerStatusState final {
    bool controllerDriverReady{false};
    bool renderReady{false};
    bool serviceReady{false};
    std::vector<NetworkAddress> addresses{};
    int panelPort{};
    int renderPort{};
    int audioSamples{};
    int audioChannels{};
    int audioBits{};
};

class ServerStatusPort {
  public:
    virtual ~ServerStatusPort() = default;
    virtual ServerStatusState Snapshot() const = 0;
    virtual void RestartRender() = 0;
    virtual void InstallControllerDriver() = 0;
};

std::shared_ptr<ServerStatusPort> CreatePreviewServerStatusPort();

} // namespace px::panel::ui
