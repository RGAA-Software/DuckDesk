#pragma once

#include "console_device_state.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace px_console {
class ConsoleStream;
}

namespace px {

class PxContext;
class PxUserManager;
class StreamDBOperator;
class StreamResourceRefreshGate;

enum class StreamCatalogMode { RemoteDevices, CloudApplications };

struct StreamResourceSnapshot final {
    std::optional<ConsoleDeviceOnlineStates> onlineStates{};
    std::optional<std::vector<std::shared_ptr<px_console::ConsoleStream>>> applicationStreams{};
};

using StreamResourceCompletion = std::function<void(StreamResourceSnapshot)>;

class StreamResourceCatalog final : public std::enable_shared_from_this<StreamResourceCatalog> {
  public:
    static std::shared_ptr<StreamResourceCatalog> Create(std::shared_ptr<PxContext> context,
                                                         std::shared_ptr<StreamDBOperator> database,
                                                         std::shared_ptr<PxUserManager> users, StreamCatalogMode mode,
                                                         std::string consoleHost, int consolePort);
    StreamResourceCatalog(std::shared_ptr<PxContext> context, std::shared_ptr<StreamDBOperator> database,
                          std::shared_ptr<PxUserManager> users, StreamCatalogMode mode, std::string consoleHost, int consolePort);
    ~StreamResourceCatalog();

    StreamResourceCatalog(const StreamResourceCatalog&) = delete;
    StreamResourceCatalog& operator=(const StreamResourceCatalog&) = delete;

    void Refresh(bool identityChanged, StreamResourceCompletion completion);
    void Stop();

  private:
    std::shared_ptr<PxContext> context_{};
    std::shared_ptr<StreamDBOperator> database_{};
    std::shared_ptr<PxUserManager> users_{};
    std::shared_ptr<StreamResourceRefreshGate> gate_{};
    StreamCatalogMode mode_{StreamCatalogMode::RemoteDevices};
    std::string consoleHost_{};
    int consolePort_{0};
};

} // namespace px
