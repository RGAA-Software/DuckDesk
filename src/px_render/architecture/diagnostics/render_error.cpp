#include "diagnostics/render_error.h"

namespace px::render {

std::string_view StableErrorCode(const RenderErrorCode code) noexcept {
    switch (code) {
        case RenderErrorCode::kPipelineInvalidFrame:
            return "PIPELINE_INVALID_FRAME";
        case RenderErrorCode::kObserverQueueOverflow:
            return "OBSERVER_QUEUE_OVERFLOW";
        case RenderErrorCode::kModuleDependencyUnavailable:
            return "MODULE_DEPENDENCY_UNAVAILABLE";
        case RenderErrorCode::kWorkflowDeadlineExceeded:
            return "WORKFLOW_DEADLINE_EXCEEDED";
        case RenderErrorCode::kAsyncScopeDrainTimeout:
            return "ASYNC_SCOPE_DRAIN_TIMEOUT";
        case RenderErrorCode::kModuleInvalidDescriptor:
            return "MODULE_INVALID_DESCRIPTOR";
        case RenderErrorCode::kModuleAlreadyRegistered:
            return "MODULE_ALREADY_REGISTERED";
        case RenderErrorCode::kModuleNotFound:
            return "MODULE_NOT_FOUND";
        case RenderErrorCode::kModuleDependencyCycle:
            return "MODULE_DEPENDENCY_CYCLE";
        case RenderErrorCode::kModuleLifecycleRejected:
            return "MODULE_LIFECYCLE_REJECTED";
        case RenderErrorCode::kModuleStartFailed:
            return "MODULE_START_FAILED";
        case RenderErrorCode::kModuleStopFailed:
            return "MODULE_STOP_FAILED";
        case RenderErrorCode::kModuleCompletionException:
            return "MODULE_COMPLETION_EXCEPTION";
        case RenderErrorCode::kFrameDebuggerDirectoryFailed:
            return "FRAME_DEBUGGER_DIRECTORY_FAILED";
        case RenderErrorCode::kFrameDebuggerFileOpenFailed:
            return "FRAME_DEBUGGER_FILE_OPEN_FAILED";
        case RenderErrorCode::kFrameDebuggerFileWriteFailed:
            return "FRAME_DEBUGGER_FILE_WRITE_FAILED";
    }
    return "MODULE_DEPENDENCY_UNAVAILABLE";
}

bool RenderError::IsValid() const noexcept {
    return !component.empty() && !operation.empty() && !stage.empty() &&
           !reason.empty();
}

}  // namespace px::render
