#pragma once

#include "panel_config_store.h"
#include "panel_audit_store.h"
#include "panel_system_information.h"

#include <asio2/http/http_server.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace px::panel::product {

struct LocalServerSnapshot final {
    bool listening{};
    bool rendererConnected{};
    int clientConnections{};
};

struct VoiceCallRequest final {
    std::string visitorDeviceId{};
    std::string streamId{};
    std::string callId{};
    std::uint64_t requestId{};
    std::uint64_t expiresAtUnixMs{};
};

class PanelLocalServer final : public std::enable_shared_from_this<PanelLocalServer> {
  public:
    static std::shared_ptr<PanelLocalServer> Create(const std::shared_ptr<PanelConfigStore>& config,
                                                    const std::shared_ptr<PanelAuditStore>& auditStore);
    PanelLocalServer(std::shared_ptr<PanelConfigStore> config, std::shared_ptr<PanelAuditStore> auditStore);
    ~PanelLocalServer();

    PanelLocalServer(const PanelLocalServer&) = delete;
    PanelLocalServer& operator=(const PanelLocalServer&) = delete;

    [[nodiscard]] LocalServerSnapshot Snapshot() const;
    [[nodiscard]] std::optional<PanelSystemInformation> SystemInformation() const;
    bool OpenFileTransfer(const std::string& streamId);
    [[nodiscard]] std::optional<VoiceCallRequest> PendingVoiceCall() const;
    void ResolveVoiceCall(const VoiceCallRequest& request, bool accepted, const std::string& reason);
    void SetRestartHandler(std::function<void()> handler);
    void RefreshPanelInfo();
    void Stop();

  private:
    void Start();
    void AddRoute(const std::string& path);
    void SendPanelInfo(const std::shared_ptr<asio2::http_session>& session) const;

    std::shared_ptr<PanelConfigStore> config_{};
    std::shared_ptr<PanelAuditStore> auditStore_{};
    std::shared_ptr<asio2::http_server> server_{};
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, std::shared_ptr<asio2::http_session>> clients_{};
    std::shared_ptr<asio2::http_session> rendererSession_{};
    std::optional<VoiceCallRequest> pendingVoiceCall_{};
    std::optional<PanelSystemInformation> systemInformation_{};
    std::function<void()> restartHandler_{};
    std::atomic_bool stopping_{};
    std::atomic_int rendererConnections_{};
    std::atomic_int clientConnections_{};
};

} // namespace px::panel::product
