#pragma once

#include "px_common/async_blocking_call.h"
#include "px_common/async_runtime.h"
#include "px_common/latest_serial_request_gate.h"
#include "px_console_client/console_api.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace px_console {
class ConsoleDevice;
}

namespace px {

class PxDeviceManager;

struct NetworkEndpointRequest final {
    std::string host{};
    int consolePort{};
    std::string appkey{};
};

struct VerifyNetworkResult final {
    enum class Failure {
        None,
        Async,
        Console,
    };

    Failure failure{Failure::None};
    PxAsyncError asyncError{};
    px_console::ConsoleApiError consoleError{px_console::ConsoleApiError::kInternalError};
    std::string consoleMessage{};
};

struct SaveNetworkResult final {
    enum class Failure {
        None,
        Async,
        Device,
    };

    Failure failure{Failure::None};
    PxAsyncError asyncError{};
    px_console::ConsoleApiError deviceError{px_console::ConsoleApiError::kInternalError};
    std::shared_ptr<px_console::ConsoleDevice> newDevice{};
};

bool IsValidNodeAccessHost(std::string_view value);

PxAwaitable<void> RunVerifyNetwork(std::shared_ptr<LatestSerialRequestGate> gate, LatestSerialRequestGate::Request request,
                                   PxBlockingTaskPoster poster, NetworkEndpointRequest endpoint, std::function<void(VerifyNetworkResult)> done);

PxAwaitable<void> RunSaveNetwork(std::shared_ptr<LatestSerialRequestGate> gate, LatestSerialRequestGate::Request request, PxBlockingTaskPoster poster,
                                 std::shared_ptr<PxDeviceManager> deviceManager, std::string deviceId, std::string defaultDeviceName,
                                 std::function<void(SaveNetworkResult)> done);

} // namespace px
