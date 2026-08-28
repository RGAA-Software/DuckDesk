#ifndef PX_FT_ENGINE_ASYNC_SESSION_H
#define PX_FT_ENGINE_ASYNC_SESSION_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "ft_engine.h"
#include "px_common_new/async_runtime.h"
#include "px_common_new/file_transfer_send_result.h"

namespace px::ft {

struct FtAsyncSessionStatistics {
    std::uint64_t accepted_messages = 0;
    std::uint64_t busy_retries = 0;
    std::uint64_t disconnected_retries = 0;
    std::uint64_t transport_errors = 0;
};

class FtAsyncSession final {
public:
    using Sender = std::function<FileTransferSendResult(
        const std::shared_ptr<const px::Message>&)>;
    using Configure = std::function<void(const std::shared_ptr<FtEngine>&)>;
    using Command = std::function<void(const std::shared_ptr<FtEngine>&)>;

    static std::shared_ptr<FtAsyncSession> Create(Sender sender,
                                                  Configure configure = {});
    static std::shared_ptr<FtAsyncSession> CreateOnRuntime(
        const std::shared_ptr<PxAsyncRuntime>& runtime,
        const std::shared_ptr<FtEngine>& engine,
        Sender sender,
        Configure configure = {},
        PxAsyncLane lane = PxAsyncLane::kWorker);

    FtAsyncSession(Sender sender, Configure configure);
    FtAsyncSession(std::shared_ptr<PxAsyncRuntime> runtime,
                   std::shared_ptr<FtEngine> engine,
                   Sender sender,
                   Configure configure,
                   bool owns_runtime,
                   PxAsyncLane lane);
    ~FtAsyncSession();

    FtAsyncSession(const FtAsyncSession&) = delete;
    FtAsyncSession& operator=(const FtAsyncSession&) = delete;

    bool Start();
    bool Post(std::string name, Command command);
    bool PostAndWait(std::string name,
                     Command command,
                     std::chrono::milliseconds timeout);
    bool StopAndWait(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    [[nodiscard]] bool HasJobs() const;
    [[nodiscard]] FtAsyncSessionStatistics GetStatistics() const;

private:
    class State;

    static PxAwaitable<void> Run(std::shared_ptr<State> state);
    static PxAwaitable<void> ExecuteCommand(std::shared_ptr<State> state,
                                            Command command);

    std::shared_ptr<PxAsyncRuntime> runtime_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::shared_ptr<State> state_;
    Configure configure_;
    bool owns_runtime_ = true;
    PxAsyncLane lane_ = PxAsyncLane::kWorker;
    std::atomic_bool started_{false};
};

} // namespace px::ft

#endif // PX_FT_ENGINE_ASYNC_SESSION_H
