#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "px_common/async_runtime.h"
#include "webrtc_transport_types.h"

namespace px {

class WebRtcExecutionContext final : public std::enable_shared_from_this<WebRtcExecutionContext> {
  public:
    [[nodiscard]] static std::shared_ptr<WebRtcExecutionContext> Create(const std::shared_ptr<PxAsyncRuntime>& runtime, std::string component_id);

    WebRtcExecutionContext(std::shared_ptr<PxAsyncRuntime> runtime, std::string component_id);
    ~WebRtcExecutionContext();

    WebRtcExecutionContext(const WebRtcExecutionContext&) = delete;
    WebRtcExecutionContext& operator=(const WebRtcExecutionContext&) = delete;

    void SetEventCallback(WebRtcEventCallback callback);
    void Publish(WebRtcEvent event, bool immediate = false);
    [[nodiscard]] bool PostWork(std::function<void()> task);
    [[nodiscard]] bool StartRepeatingTask(std::chrono::milliseconds interval, std::function<void()> task);
    void BeginStop();
    [[nodiscard]] bool StopAndWait(std::chrono::milliseconds timeout);
    [[nodiscard]] bool IsAccepting() const;

  private:
    [[nodiscard]] static PxAwaitable<void> RunWork(std::weak_ptr<WebRtcExecutionContext> weak_context, std::function<void()> task);
    [[nodiscard]] static PxAwaitable<void> RunRepeatingTask(std::weak_ptr<WebRtcExecutionContext> weak_context, std::chrono::milliseconds interval,
                                                            std::function<void()> task);
    void Deliver(const WebRtcEvent& event) const;

    std::shared_ptr<PxAsyncRuntime> runtime_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::string component_id_;
    mutable std::mutex callback_mutex_;
    WebRtcEventCallback callback_;
    std::atomic_bool accepting_{true};
};

} // namespace px
