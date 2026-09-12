#pragma once

#include "stream_launch_auth_workflow.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace px_console {
class ConsoleStream;
}

namespace px {

class MessageListener;
class PxContext;
class PxSettings;
class RunningStreamManager;
class StreamDBOperator;
class StreamResourceCatalog;
class StreamStateChecker;

enum class AppStreamListMode {
    kRemoteDevices,
    kCloudApplications,
};

// Headless owner of stream discovery, authorization and client launch.
// The historical filename is retained temporarily; this type owns no UI.
class StreamSessionController final : public std::enable_shared_from_this<StreamSessionController> {
  public:
    static std::shared_ptr<StreamSessionController> Create(const std::shared_ptr<PxContext>& context, AppStreamListMode mode);
    StreamSessionController(std::shared_ptr<PxContext> context, AppStreamListMode mode);
    ~StreamSessionController();

    StreamSessionController(const StreamSessionController&) = delete;
    StreamSessionController& operator=(const StreamSessionController&) = delete;

    void Reload();
    void RefreshResources();
    void Start(const std::shared_ptr<px_console::ConsoleStream>& item, bool viewOnly = false);
    bool Stop(const std::shared_ptr<px_console::ConsoleStream>& item);
    void StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item);
    std::vector<std::shared_ptr<px_console::ConsoleStream>> Snapshot();

  private:
    void Initialize();
    void RegisterListeners();
    void StartInternal(const std::shared_ptr<px_console::ConsoleStream>& item, bool viewOnly);
    void StartConsoleTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item, bool applicationTicket);
    void CompleteConsoleTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item, bool applicationTicket,
                                     std::uint64_t generation, StreamLaunchAuthResult result);
    void ContinueStart(const std::shared_ptr<px_console::ConsoleStream>& item, bool consoleTicket,
                       std::optional<bool> authenticatedDirectAvailable = std::nullopt);
    StreamLaunchAuthHooks MakeAuthHooks() const;
    void StartFileTransferTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item);
    void CompleteFileTransferTicketLaunch(const std::shared_ptr<px_console::ConsoleStream>& item, std::uint64_t generation,
                                          StreamLaunchAuthResult result);
    void StartResourceRefresh(bool identityChanged);
    void ClearIdentityResources();
    std::vector<std::shared_ptr<px_console::ConsoleStream>> CopyStreams();

    std::reference_wrapper<PxSettings> settings_;
    std::shared_ptr<PxContext> context_{};
    std::shared_ptr<StreamDBOperator> database_{};
    std::shared_ptr<RunningStreamManager> runningStreams_{};
    std::shared_ptr<MessageListener> listener_{};
    std::shared_ptr<StreamLaunchAuthWorkflow> authorization_{};
    std::shared_ptr<StreamStateChecker> stateChecker_{};
    std::shared_ptr<StreamResourceCatalog> resourceCatalog_{};
    std::mutex streamsMutex_{};
    std::vector<std::shared_ptr<px_console::ConsoleStream>> streams_{};
    std::vector<std::shared_ptr<px_console::ConsoleStream>> applicationStreams_{};
    std::unordered_map<std::string, bool> deviceOnlineStates_{};
    AppStreamListMode mode_{AppStreamListMode::kRemoteDevices};
};

} // namespace px
