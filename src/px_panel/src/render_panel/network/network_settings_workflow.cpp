#include "network_settings_workflow.h"

#include "render_panel/devices/px_device_manager.h"

#include "px_console_client/console_device.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_http_client.h"

#include <QUrl>

#include <atomic>
#include <chrono>
#include <optional>
#include <utility>

namespace px {
namespace {

struct ConsolePingResult final {
    Result<bool, px_console::ConsoleApiError> result;
    std::string serverMessage{};
};

template <typename T>
PxAwaitable<PxResult<std::optional<T>>> AwaitGatedBlockingCall(std::shared_ptr<LatestSerialRequestGate> gate,
                                                               LatestSerialRequestGate::Request request, PxBlockingTaskPoster poster,
                                                               std::chrono::steady_clock::time_point deadline, std::string stage,
                                                               std::function<T(const std::shared_ptr<std::atomic_bool>&)> call) {
    const auto executor = co_await asio::this_coro::executor;
    co_return co_await AwaitBlockingCall<std::optional<T>>(
        poster, executor, deadline, request.cancellation, std::move(stage),
        [gate, request, call = std::move(call)](const std::shared_ptr<std::atomic_bool>& cancellation) mutable {
            std::optional<T> result;
            static_cast<void>(gate->RunIfCurrent(request.generation, [&result, &call, &cancellation]() { result.emplace(call(cancellation)); }));
            return result;
        });
}

} // namespace

bool IsValidNodeAccessHost(const std::string_view value) {
    if (value.empty()) {
        return true;
    }
    const QString input{QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()))};
    const QUrl endpoint{"https://" + input, QUrl::StrictMode};
    return input.trimmed() == input && endpoint.isValid() && !endpoint.host().isEmpty() && endpoint.userInfo().isEmpty() && endpoint.port(-1) == -1 &&
           !endpoint.hasQuery() && !endpoint.hasFragment() && (endpoint.path().isEmpty() || endpoint.path() == "/") && endpoint.host() != "0.0.0.0" &&
           endpoint.host() != "::";
}

PxAwaitable<void> RunVerifyNetwork(std::shared_ptr<LatestSerialRequestGate> gate, LatestSerialRequestGate::Request request,
                                   PxBlockingTaskPoster poster, NetworkEndpointRequest endpoint, std::function<void(VerifyNetworkResult)> done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    auto consoleCall = co_await AwaitGatedBlockingCall<ConsolePingResult>(
        gate, request, poster, deadline, "panel-network.verify-console", [endpoint](const std::shared_ptr<std::atomic_bool>& cancellation) {
            auto result = px_console::ConsoleDeviceApi::Ping(endpoint.host, endpoint.consolePort, endpoint.appkey, cancellation);
            return ConsolePingResult{.result = std::move(result), .serverMessage = px_console::ConsoleApiLastErrorMessage()};
        });
    if (!consoleCall) {
        done(VerifyNetworkResult{.failure = VerifyNetworkResult::Failure::Async, .asyncError = consoleCall.Error()});
        co_return;
    }
    auto consoleCompletion = consoleCall.TakeValue();
    if (!consoleCompletion) {
        co_return;
    }
    if (!consoleCompletion->result.has_value() || !consoleCompletion->result.value()) {
        done(VerifyNetworkResult{
            .failure = VerifyNetworkResult::Failure::Console,
            .consoleError =
                consoleCompletion->result.has_value() ? px_console::ConsoleApiError::kServiceUnavailable : consoleCompletion->result.error(),
            .consoleMessage = std::move(consoleCompletion->serverMessage),
        });
        co_return;
    }
    done({});
}

PxAwaitable<void> RunSaveNetwork(std::shared_ptr<LatestSerialRequestGate> gate, LatestSerialRequestGate::Request request, PxBlockingTaskPoster poster,
                                 std::shared_ptr<PxDeviceManager> deviceManager, std::string deviceId, std::string defaultDeviceName,
                                 std::function<void(SaveNetworkResult)> done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool requestNewDevice{deviceId.empty()};
    if (!requestNewDevice) {
        auto queryCall = co_await AwaitGatedBlockingCall<Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>>(
            gate, request, poster, deadline, "panel-network.query-device",
            [deviceManager, deviceId](const std::shared_ptr<std::atomic_bool>& cancellation) {
                return deviceManager->QueryDevice(deviceId, cancellation);
            });
        if (!queryCall) {
            done(SaveNetworkResult{.failure = SaveNetworkResult::Failure::Async, .asyncError = queryCall.Error()});
            co_return;
        }
        auto query = queryCall.TakeValue();
        if (!query) {
            co_return;
        }
        if (!query->has_value() && query->error() != px_console::ConsoleApiError::kDeviceNotFound) {
            done(SaveNetworkResult{.failure = SaveNetworkResult::Failure::Device, .deviceError = query->error()});
            co_return;
        }
        requestNewDevice = !query->has_value() || !query->value() || query->value()->device_id_.empty();
    }

    if (!requestNewDevice) {
        done({});
        co_return;
    }
    auto createCall = co_await AwaitGatedBlockingCall<Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>>(
        gate, request, poster, deadline, "panel-network.create-device",
        [deviceManager, defaultDeviceName](const std::shared_ptr<std::atomic_bool>& cancellation) {
            return deviceManager->RequestNewDevice(defaultDeviceName, "", cancellation);
        });
    if (!createCall) {
        done(SaveNetworkResult{.failure = SaveNetworkResult::Failure::Async, .asyncError = createCall.Error()});
        co_return;
    }
    auto created = createCall.TakeValue();
    if (!created) {
        co_return;
    }
    if (!created->has_value() || !created->value() || created->value()->device_id_.empty() || created->value()->gen_random_pwd_.empty()) {
        done(SaveNetworkResult{
            .failure = SaveNetworkResult::Failure::Device,
            .deviceError = created->has_value() ? px_console::ConsoleApiError::kInternalError : created->error(),
        });
        co_return;
    }
    done(SaveNetworkResult{.newDevice = created->value()});
}

} // namespace px
