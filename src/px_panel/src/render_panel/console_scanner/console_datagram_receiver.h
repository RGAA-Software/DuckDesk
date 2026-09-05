#ifndef PX_CONSOLE_DATAGRAM_RECEIVER_H
#define PX_CONSOLE_DATAGRAM_RECEIVER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "px_common/async_runtime.h"

namespace px {

class ConsoleDatagramReceiver final {
public:
    using DatagramHandler = std::function<void(std::string)>;

    static std::shared_ptr<ConsoleDatagramReceiver> Create(
        const std::shared_ptr<PxAsyncRuntime>& runtime,
        std::chrono::milliseconds retry_delay = std::chrono::seconds(2));

    ConsoleDatagramReceiver(
        std::shared_ptr<PxAsyncScope> scope,
        std::chrono::milliseconds retry_delay);
    ~ConsoleDatagramReceiver();

    ConsoleDatagramReceiver(const ConsoleDatagramReceiver&) = delete;
    ConsoleDatagramReceiver& operator=(const ConsoleDatagramReceiver&) = delete;

    bool Start(std::uint16_t port, DatagramHandler handler);
    void Stop();

    [[nodiscard]] std::uint16_t BoundPort() const noexcept;
    [[nodiscard]] PxAsyncScopeStatistics Statistics() const;

private:
    class State;

    static PxAwaitable<void> Run(
        std::shared_ptr<State> state,
        std::uint16_t port,
        std::chrono::milliseconds retry_delay,
        DatagramHandler handler);

    std::shared_ptr<PxAsyncScope> scope_;
    std::shared_ptr<State> state_;
    std::chrono::milliseconds retry_delay_;
    std::atomic_bool started_ = false;
};

} // namespace px

#endif // PX_CONSOLE_DATAGRAM_RECEIVER_H
