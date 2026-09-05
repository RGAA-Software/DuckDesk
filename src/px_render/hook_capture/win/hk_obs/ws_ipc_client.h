//
// Created by RGAA on 2024/3/17.
//

#ifndef TC_APPLICATION_WS_IPC_CLIENT_H
#define TC_APPLICATION_WS_IPC_CLIENT_H

#include <memory>
#include <functional>
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <asio2/websocket/ws_client.hpp>

#include "px_common/async_result.h"
#include "px_common/async_runtime.h"

namespace px {

class Message;
class CaptureBaseMessage;
class PxReconnectSupervisor;
template <typename Client> class PxReconnectAdapterSlot;
template <typename T> class PxAsyncMailbox;

using WsIpcMessageCallback = std::function<void(const std::shared_ptr<CaptureBaseMessage>&)>;

// Plain WS client — must match render net_ws (asio2::http_server), not WSS.
class WsIpcClient : public std::enable_shared_from_this<WsIpcClient> {
  public:
    static std::shared_ptr<WsIpcClient> Make(int port);
    explicit WsIpcClient(int port);
    ~WsIpcClient();

    void Start();
    void Exit();
    [[nodiscard]] bool IsStarted() const;
    [[nodiscard]] bool IsConnected() const;
    [[nodiscard]] std::uint64_t ConnectionGeneration() const;
    [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(std::shared_ptr<WsIpcClient> owner, std::chrono::steady_clock::time_point deadline);

    void PostIpcMessage(const std::string& msg);
    void RegisterIpcMessageCallback(WsIpcMessageCallback&& cbk);

  private:
    struct AsyncStateSnapshot final {
        std::shared_ptr<PxAsyncRuntime> runtime{};
        std::shared_ptr<PxAsyncScope> scope{};
        std::shared_ptr<PxReconnectSupervisor> supervisor{};
        std::shared_ptr<PxAsyncMailbox<std::string>> mailbox{};
    };

    void DispatchIpcMessage(const std::string& data);
    std::shared_ptr<PxAsyncScope> BeginStop();
    void FinishStop();
    [[nodiscard]] AsyncStateSnapshot SnapshotAsyncState() const;
    static PxAwaitable<void> RunIncomingMessageLoop(std::weak_ptr<WsIpcClient> weak_client, std::shared_ptr<PxAsyncMailbox<std::string>> mailbox);

  private:
    int port_{0};
    std::shared_ptr<PxReconnectAdapterSlot<asio2::ws_client>> adapter_slot_{};
    std::shared_ptr<PxAsyncRuntime> async_runtime_{};
    std::shared_ptr<PxAsyncScope> async_scope_{};
    std::shared_ptr<PxReconnectSupervisor> connection_supervisor_{};
    std::shared_ptr<PxAsyncMailbox<std::string>> incoming_messages_{};
    mutable std::mutex callback_mutex_;
    WsIpcMessageCallback ipc_cbk_{};
    std::atomic_uint64_t null_drop_count_{0};
    std::atomic_uint64_t stopped_drop_count_{0};
    std::atomic_bool started_{false};
    std::atomic_bool exiting_{false};
    std::mutex operation_mutex_{};
    mutable std::mutex lifecycle_mutex_{};
};

} // namespace px

#endif // TC_APPLICATION_WS_IPC_CLIENT_H
