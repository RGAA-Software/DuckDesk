#ifndef PX_COMMON_NEW_ASIO_CLIENT_SHUTDOWN_H
#define PX_COMMON_NEW_ASIO_CLIENT_SHUTDOWN_H

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>

#include "async_delay.h"
#include "async_result.h"

namespace px {

template<typename Client>
PxResult<void> RequestAsioClientStop(const std::shared_ptr<Client>& client, const std::string& stage) {
    if (!client || client->is_stopped()) {
        return PxResult<void>::Success();
    }
    try {
        client->post([client] {
            client->set_auto_reconnect(false);
            client->stop_all_timers();
            client->stop();
        });
    }
    catch (const std::exception& error) {
        return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kProtocolError, stage, error.what(), true));
    }
    return PxResult<void>::Success();
}

template<typename Client>
bool IsAsioClientStopped(const std::shared_ptr<Client>& client) {
    return !client || client->is_stopped();
}

template<typename Client>
PxAwaitable<PxResult<void>> WaitForAsioClientStopped(
    const std::shared_ptr<Client>& client,
    const std::chrono::steady_clock::time_point deadline,
    std::string stage) {
    constexpr auto kPollInterval = std::chrono::milliseconds(5);
    while (!IsAsioClientStopped(client)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kTimeout, std::move(stage), "asio client stop deadline expired", true));
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto waited = co_await WaitForAsyncDelay(std::min(kPollInterval, remaining), stage);
        if (!waited) {
            co_return waited;
        }
    }
    co_return PxResult<void>::Success();
}

template<typename Client>
bool WaitForAsioClientStoppedBlocking(
    const std::shared_ptr<Client>& client,
    const std::chrono::steady_clock::time_point deadline) {
    constexpr auto kPollInterval = std::chrono::milliseconds(5);
    while (!IsAsioClientStopped(client)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return true;
}

} // namespace px

#endif // PX_COMMON_NEW_ASIO_CLIENT_SHUTDOWN_H
