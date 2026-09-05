#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "px_common/async_runtime.h"

namespace px {

class RenderExecutionContext final : public std::enable_shared_from_this<RenderExecutionContext> {
  public:
    static std::shared_ptr<RenderExecutionContext> Create(const std::shared_ptr<PxAsyncRuntime>& runtime, std::string owner_name);

    RenderExecutionContext(std::shared_ptr<PxAsyncRuntime> runtime, std::shared_ptr<PxAsyncScope> scope, std::string owner_name);
    ~RenderExecutionContext();

    RenderExecutionContext(const RenderExecutionContext&) = delete;
    RenderExecutionContext& operator=(const RenderExecutionContext&) = delete;

    [[nodiscard]] bool Post(std::function<void()> task);
    [[nodiscard]] bool PostDelayed(std::chrono::milliseconds delay, std::function<void()> task);
    void Stop();
    [[nodiscard]] bool IsStopping() const noexcept;

  private:
    std::shared_ptr<PxAsyncRuntime> runtime_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::string owner_name_;
    std::atomic_bool stopping_{false};
};

} // namespace px
