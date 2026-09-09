#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "app_manager_win.h"
#include "ingress/application_text_service.h"
#include "px_common/async_runtime.h"
#include "game_text_write_permit.h"

namespace px {

class GameTextBackend final : public std::enable_shared_from_this<GameTextBackend> {
  public:
    using Send = std::function<bool(std::uint32_t, const CaptureTextCommand&, std::function<bool()>)>;
    GameTextBackend(std::shared_ptr<AppManagerWinImpl> manager, std::shared_ptr<PxAsyncRuntime> runtime, Send send);
    ~GameTextBackend();
    [[nodiscard]] ApplicationTextBackend Adapter();
    void HandleReply(std::uint32_t authenticated_pid, const CaptureTextReply& reply);
    void Stop();

  private:
    using Completion = std::function<void(CaptureTextReply)>;
    struct Pending final {
        std::uint32_t pid{};
        std::size_t text_bytes{};
        std::shared_ptr<UniqueWinHandle> process{};
        std::shared_ptr<asio::steady_timer> timer{};
        std::shared_ptr<GameTextWritePermit> permit{};
        Completion complete{};
    };
    void Request(
        OwnedGameTextTarget target, Completion complete, std::function<bool()> authorize = [] { return true; });
    void Finish(std::uint64_t request, CaptureTextReply reply);
    [[nodiscard]] static std::string TargetPrefix(const OwnedGameTextTarget& target);

    std::weak_ptr<AppManagerWinImpl> manager_{};
    std::shared_ptr<PxAsyncRuntime> runtime_{};
    Send send_{};
    std::mutex mutex_{};
    std::unordered_map<std::uint64_t, Pending> pending_{};
    std::uint64_t next_request_{};
    bool stopped_{};
};
} // namespace px
