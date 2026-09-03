#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace px::render {

enum class RenderErrorCode {
    kPipelineInvalidFrame,
    kObserverQueueOverflow,
    kModuleDependencyUnavailable,
    kWorkflowDeadlineExceeded,
    kAsyncScopeDrainTimeout,
    kModuleInvalidDescriptor,
    kModuleAlreadyRegistered,
    kModuleNotFound,
    kModuleDependencyCycle,
    kModuleLifecycleRejected,
    kModuleStartFailed,
    kModuleStopFailed,
    kModuleCompletionException,
    kFrameDebuggerDirectoryFailed,
    kFrameDebuggerFileOpenFailed,
    kFrameDebuggerFileWriteFailed,
};

[[nodiscard]] std::string_view StableErrorCode(RenderErrorCode code) noexcept;

struct RenderError final {
    RenderErrorCode code{RenderErrorCode::kModuleDependencyUnavailable};
    std::string component;
    std::string operation;
    std::string stage;
    std::string reason;
    bool recoverable{false};
    std::optional<std::int64_t> native_code;

    [[nodiscard]] bool IsValid() const noexcept;
};

}  // namespace px::render
