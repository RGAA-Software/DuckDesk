//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_WS_PANEL_CLIENT_H
#define PX_WS_PANEL_CLIENT_H

#include <memory>
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <asio2/asio2.hpp>

#include "px_common/async_result.h"
#include "px_common/async_runtime.h"

namespace px {
namespace render {
class RenderCompositionRoot;
}

class Data;
class RdContext;
class RdStatistics;
class MessageListener;
class RdSettings;
class RenderModuleRegistry;
class PxReconnectSupervisor;
template <typename Client> class PxReconnectAdapterSlot;
template <typename T> class PxAsyncMailbox;

class WsPanelClient : public std::enable_shared_from_this<WsPanelClient> {
  public:
    explicit WsPanelClient(const std::shared_ptr<RdContext>& ctx);
    ~WsPanelClient();
    void Start();
    void Exit();
    [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(std::shared_ptr<WsPanelClient> owner, std::chrono::steady_clock::time_point deadline);
    bool PostNetMessage(std::shared_ptr<Data> msg);
    bool Alive() const;

    void ReportMonitorChanged();

  private:
    struct AsyncStateSnapshot final {
        std::shared_ptr<PxAsyncScope> scope{};
        std::shared_ptr<PxReconnectSupervisor> supervisor{};
        std::shared_ptr<PxAsyncMailbox<std::string>> mailbox{};
    };

    void ReportStatistics();
    void SendStatisticsInternal();
    void SendModulesInfoInternal();
    void ParseNetMessage(const std::string& msg);
    void ProcessCommandEnableModule(const std::string& module_id);
    void ProcessCommandDisableModule(const std::string& module_id);
    std::shared_ptr<PxAsyncScope> BeginStop();
    void FinishStop();
    void ScheduleDeferredExit();
    [[nodiscard]] AsyncStateSnapshot SnapshotAsyncState() const;
    static PxAwaitable<void> RunIncomingMessageLoop(std::weak_ptr<WsPanelClient> weak_client, std::shared_ptr<PxAsyncMailbox<std::string>> mailbox);

  private:
    std::shared_ptr<RdStatistics> statistics_{};
    std::reference_wrapper<RdSettings> settings_;
    std::shared_ptr<RdContext> context_{};
    std::shared_ptr<PxReconnectAdapterSlot<asio2::ws_client>> adapter_slot_{};
    std::shared_ptr<MessageListener> msg_listener_{};
    std::shared_ptr<MessageListener> state_msg_listener_{};
    std::shared_ptr<PxAsyncScope> async_scope_{};
    std::shared_ptr<PxReconnectSupervisor> connection_supervisor_{};
    std::shared_ptr<PxAsyncMailbox<std::string>> incoming_messages_{};
    std::shared_ptr<RenderModuleRegistry> module_registry_{};
    std::shared_ptr<render::RenderCompositionRoot> composition_root_{};
    std::atomic_int queuing_message_count_{0};
    std::atomic_bool started_{false};
    std::atomic_bool exiting_{false};
    std::atomic_bool deferred_exit_scheduled_{false};
    std::mutex operation_mutex_{};
    mutable std::mutex lifecycle_mutex_{};
    // Stable across connection generations and changes when the render process restarts.
    std::string instance_id_{};
};

} // namespace px

#endif // PX_WS_PANEL_CLIENT_H
