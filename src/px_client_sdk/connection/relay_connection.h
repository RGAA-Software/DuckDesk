#pragma once

#include "connection.h"
#include "px_client_sdk/sdk_connection_params.h"

#include <atomic>
#include <memory>

namespace px {

class RelayClientSdk;

class RelayConnection final : public Connection, public std::enable_shared_from_this<RelayConnection> {
  public:
    RelayConnection(SdkConnectionParams params, const std::shared_ptr<MessageNotifier>& notifier);
    ~RelayConnection() override;

    void Start() override;
    void Stop() override;
    void PostBinaryMessage(std::shared_ptr<Data> message) override;
    void PostReliableBinaryMessage(std::shared_ptr<Data> message, std::function<void(bool)> completion) override;
    [[nodiscard]] int64_t GetQueuingMsgCount() override;
    void RequestPauseStream() override;
    void RequestResumeStream() override;
    void On16msTimeout() override;
    [[nodiscard]] bool IsAlive() override;
    [[nodiscard]] std::shared_ptr<FileTransferWritableSignal> AcquireFileTransferWritableSignal() override;

  private:
    const SdkConnectionParams params_{};
    std::shared_ptr<RelayClientSdk> relay_sdk_{};
    std::atomic_bool started_{false};
    std::atomic_bool stopped_{false};
    std::atomic_bool room_ready_{false};
};

} // namespace px
