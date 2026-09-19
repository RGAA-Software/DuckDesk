#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "panel_config_store.h"

namespace asio2 {
class ws_client;
}

namespace px::panel::product {

struct ServiceSnapshot final {
    bool connected{};
    bool renderRunning{};
    bool nodeControlReady{};
    std::string nodeId{};
    std::string deviceId{};
    std::string nodeAccessHost{};
    std::int64_t nodeGeneration{};
    std::int64_t controlEpoch{};
};

class PanelServiceBridge final {
public:
    static std::shared_ptr<PanelServiceBridge> Create(const std::shared_ptr<PanelConfigStore>& config);
    explicit PanelServiceBridge(std::shared_ptr<PanelConfigStore> config);
    ~PanelServiceBridge();

    PanelServiceBridge(const PanelServiceBridge&) = delete;
    PanelServiceBridge& operator=(const PanelServiceBridge&) = delete;

    [[nodiscard]] ServiceSnapshot Snapshot() const;
    bool RestartRender();
    void Stop();

private:
    struct State;
    static void Run(const std::shared_ptr<State>& state, std::stop_token stopToken);
    static void SendHeartbeat(const std::shared_ptr<State>& state);
    static void SendRenderCommand(const std::shared_ptr<State>& state, bool restart);

    std::shared_ptr<State> state_{};
    std::jthread thread_{};
};

}  // namespace px::panel::product
