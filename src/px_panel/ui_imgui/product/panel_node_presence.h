#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace asio2 {
class wss_client;
}

namespace px::panel::product {

class PanelConfigStore;

class PanelNodePresence final : public std::enable_shared_from_this<PanelNodePresence> {
  public:
    static std::shared_ptr<PanelNodePresence> Create(const std::shared_ptr<PanelConfigStore>& config);
    explicit PanelNodePresence(std::shared_ptr<PanelConfigStore> config);
    ~PanelNodePresence();

    void Start();
    void Stop();
    [[nodiscard]] bool IsOnline() const noexcept;

  private:
    void Run(std::stop_token stopToken);
    void ConfigureClient(const std::shared_ptr<asio2::wss_client>& client);
    void SendHello();
    void SendHeartbeat();
    void Send(const std::string& message);

    std::shared_ptr<PanelConfigStore> config_{};
    mutable std::mutex clientMutex_{};
    std::shared_ptr<asio2::wss_client> client_{};
    std::jthread worker_{};
    std::atomic_bool stopping_{};
    std::atomic_bool connecting_{};
    std::atomic_bool online_{};
    std::atomic_bool useLegacyPath_{};
    std::atomic_uint64_t heartbeatIndex_{};
};

} // namespace px::panel::product
