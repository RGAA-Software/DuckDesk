#pragma once

#include <chrono>
#include <vector>

#include "client_controller_position.h"
#include "client_statistics_format.h"

namespace px::client::imgui {

class ClientSession;

class ClientStatisticsOverlay final {
public:
    void Draw(const ClientSession& session, const ControllerArea& contentArea);
    void SetVisible(bool visible) noexcept;
    void SetLanguage(bool english) noexcept;

private:
    bool visible_{};
    bool english_{};
    std::chrono::steady_clock::time_point nextRefresh_{};
    std::vector<ClientStatisticsRow> rows_{};
};

}  // namespace px::client::imgui
