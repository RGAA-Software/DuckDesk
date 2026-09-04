#ifndef PX_COMMON_NEW_WEBSOCKET_RECONNECT_ADAPTER_H
#define PX_COMMON_NEW_WEBSOCKET_RECONNECT_ADAPTER_H

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include <asio2/asio2.hpp>

#include "asio_client_shutdown.h"
#include "reconnect_supervisor.h"

namespace px {

inline PxReconnectSupervisorOptions MakeWebSocketReconnectOptions(std::string component) {
    return PxReconnectSupervisorOptions{
        .component = std::move(component),
        .connection_timeout = std::chrono::seconds(10),
        .adapter_stop_timeout = std::chrono::seconds(3),
        .backoff = PxReconnectBackoffOptions{
            .initial_delay = std::chrono::milliseconds(250),
            .maximum_delay = std::chrono::seconds(30),
            .multiplier = 2.0,
            .jitter_ratio = 0.2,
        },
    };
}

template<typename Client>
PxResult<void> StartWebSocketAdapter(
    const std::shared_ptr<Client>& client,
    const std::string& host,
    const int port,
    std::string stage) {
    if (!client) {
        return PxResult<void>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kServiceStopped, std::move(stage), "websocket adapter is unavailable"));
    }
    if (client->async_start(host, port)) {
        return PxResult<void>::Success();
    }
    return PxResult<void>::Failure(MakePxAsyncError(
        PxAsyncErrorCode::kServiceNotConnected, std::move(stage), asio2::last_error_msg(), true));
}

template<typename Client>
PxResult<void> StartWebSocketAdapter(
    const std::shared_ptr<Client>& client,
    const std::string& host,
    const int port,
    const std::string& path,
    std::string stage) {
    if (!client) {
        return PxResult<void>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kServiceStopped, std::move(stage), "websocket adapter is unavailable"));
    }
    if (client->async_start(host, port, path)) {
        return PxResult<void>::Success();
    }
    return PxResult<void>::Failure(MakePxAsyncError(
        PxAsyncErrorCode::kServiceNotConnected, std::move(stage), asio2::last_error_msg(), true));
}

template<typename Client>
PxAwaitable<PxResult<void>> StopWebSocketAdapter(
    const std::shared_ptr<Client>& client,
    const std::chrono::steady_clock::time_point deadline,
    std::string stage) {
    const auto requested = RequestAsioClientStop(client, stage);
    if (!requested) {
        co_return requested;
    }
    co_return co_await WaitForAsioClientStopped(client, deadline, std::move(stage));
}

} // namespace px

#endif // PX_COMMON_NEW_WEBSOCKET_RECONNECT_ADAPTER_H
