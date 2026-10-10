#pragma once

#include <memory>

#include "client_launch_config.h"

namespace px::client::imgui {

struct ClientSessionSnapshot;

// Owned/stopped by the main thread; callbacks retain only weak state references.
class ClientPanelReporter final {
public:
    explicit ClientPanelReporter(const ClientLaunchConfig& config);
    ~ClientPanelReporter();
    ClientPanelReporter(const ClientPanelReporter&) = delete;
    ClientPanelReporter& operator=(const ClientPanelReporter&) = delete;
    void Start();
    [[nodiscard]] bool NeedsObservation() const;
    void Observe(const ClientSessionSnapshot& snapshot);
    void ReportInitializationFailure();
    void Stop();

private:
    struct State;
    struct Transport;
    void Report(bool connected, int rejection);
    std::shared_ptr<State> state_{};
    std::unique_ptr<Transport> transport_{};
};

}  // namespace px::client::imgui
