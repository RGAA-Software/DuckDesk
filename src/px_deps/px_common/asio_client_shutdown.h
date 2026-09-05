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

template <typename Client> PxResult<void> RequestAsioClientStop(const std::shared_ptr<Client>& client, const std::string& stage) {
    if (!client) {
        return PxResult<void>::Success();
    }
    try {
        // asio2::basic_client::stop() owns the cross-thread handoff: from a non-I/O thread it waits for the stop chain and joins the
        // private I/O pool; from the I/O thread it schedules the chain without blocking. Posting stop() onto the client I/O thread here
        // leaves that pool alive and allows the adapter to be destroyed while its shutdown chain is still queued.
        client->set_auto_reconnect(false);
        client->stop();
    } catch (const std::exception& error) {
        return PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kProtocolError, stage, error.what(), true));
    }
    return PxResult<void>::Success();
}

template <typename Client> bool IsAsioObjectStopped(const std::shared_ptr<Client>& object) {
    return !object || object->is_stopped();
}

template <typename Client>
PxAwaitable<PxResult<void>> WaitForAsioObjectStopped(std::shared_ptr<Client> object, const std::chrono::steady_clock::time_point deadline,
                                                     std::string stage) {
    constexpr auto kPollInterval = std::chrono::milliseconds(5);
    while (!IsAsioObjectStopped(object)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return PxResult<void>::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kTimeout, std::move(stage), "asio client stop deadline expired", true));
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto waited = co_await WaitForAsyncDelay(std::min(kPollInterval, remaining), stage);
        if (!waited) {
            co_return waited;
        }
    }
    co_return PxResult<void>::Success();
}

template <typename Client>
bool WaitForAsioObjectStoppedBlocking(const std::shared_ptr<Client>& object, const std::chrono::steady_clock::time_point deadline) {
    constexpr auto kPollInterval = std::chrono::milliseconds(5);
    while (!IsAsioObjectStopped(object)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return true;
}

template <typename Client> bool IsAsioClientStopped(const std::shared_ptr<Client>& client) {
    return IsAsioObjectStopped(client);
}

template <typename Client>
PxAwaitable<PxResult<void>> WaitForAsioClientStopped(std::shared_ptr<Client> client, const std::chrono::steady_clock::time_point deadline,
                                                     std::string stage) {
    co_return co_await WaitForAsioObjectStopped(client, deadline, std::move(stage));
}

template <typename Client>
bool WaitForAsioClientStoppedBlocking(const std::shared_ptr<Client>& client, const std::chrono::steady_clock::time_point deadline) {
    return WaitForAsioObjectStoppedBlocking(client, deadline);
}

} // namespace px

#endif // PX_COMMON_NEW_ASIO_CLIENT_SHUTDOWN_H
