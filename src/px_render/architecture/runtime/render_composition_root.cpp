#include "runtime/render_composition_root.h"

#include <exception>
#include <utility>

#include "px_common_new/async_operation.h"
#include "px_common_new/log.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

RenderError MakeCompositionError(const RenderErrorCode code,
                                 std::string operation,
                                 std::string reason,
                                 const bool recoverable = false) {
    return RenderError{
        .code = code,
        .component = "render_composition_root",
        .operation = std::move(operation),
        .stage = "module_lifecycle",
        .reason = std::move(reason),
        .recoverable = recoverable,
    };
}

ModuleLifecycleResult ExceptionResult(const RenderErrorCode code,
                                      const std::string& operation,
                                      const std::string& module_id,
                                      const std::string& reason) {
    return std::unexpected(MakeCompositionError(
        code, operation, "module=" + module_id + " exception=" + reason));
}

void CompleteSafely(const CompositionCompletion& completion,
                    ModuleLifecycleResult result) {
    if (!completion) {
        return;
    }
    try {
        completion(std::move(result));
    }
    catch (const std::exception& error) {
        LOGE("event=module.completion component=render_composition_root "
             "code={} outcome=ignored reason={}",
             StableErrorCode(RenderErrorCode::kModuleCompletionException),
             error.what());
    }
    catch (...) {
        LOGE("event=module.completion component=render_composition_root "
             "code={} outcome=ignored reason=unknown_exception",
             StableErrorCode(RenderErrorCode::kModuleCompletionException));
    }
}

}  // namespace

std::shared_ptr<RenderCompositionRoot> RenderCompositionRoot::Create(
    const std::shared_ptr<PxAsyncRuntime>& runtime,
    const std::shared_ptr<BuiltinModuleCatalog>& catalog) {
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    if (!scope || !catalog) {
        return {};
    }
    return std::make_shared<RenderCompositionRoot>(scope, catalog);
}

RenderCompositionRoot::RenderCompositionRoot(
    std::shared_ptr<PxAsyncScope> lifecycle_scope,
    std::shared_ptr<BuiltinModuleCatalog> catalog)
    : lifecycle_scope_(std::move(lifecycle_scope)),
      catalog_(std::move(catalog)) {}

RenderCompositionRoot::~RenderCompositionRoot() {
    lifecycle_scope_->BeginStop();
}

ModuleLifecycleResult RenderCompositionRoot::Register(
    BuiltinModuleRegistration registration) {
    return catalog_->Register(std::move(registration));
}

ModuleLifecycleResult RenderCompositionRoot::SetEnabled(
    const std::string& module_id,
    const bool enabled) {
    auto result = catalog_->SetEnabled(module_id, enabled);
    if (result) {
        LOGI("event=module.enable component=render_composition_root "
             "module={} enabled={} outcome=success",
             module_id,
             enabled);
    }
    else {
        LOGE("event=module.enable component=render_composition_root "
             "module={} enabled={} code={} outcome=rejected reason={}",
             module_id,
             enabled,
             StableErrorCode(result.error().code),
             result.error().reason);
    }
    return result;
}

bool RenderCompositionRoot::RequestStart(CompositionCompletion completion) {
    stop_requested_.store(false, std::memory_order_release);
    const std::weak_ptr<RenderCompositionRoot> weak_owner = weak_from_this();
    const auto rejected_completion = completion;
    const auto accepted = lifecycle_scope_->Spawn(
        "render_composition_start",
        [weak_owner, completion = std::move(completion)]() mutable {
            return RunStart(weak_owner, std::move(completion));
        });
    if (!accepted) {
        CompleteSafely(rejected_completion, std::unexpected(MakeCompositionError(
            RenderErrorCode::kModuleLifecycleRejected,
            "start",
            "lifecycle scope rejected the start request",
            true)));
    }
    return accepted;
}

bool RenderCompositionRoot::RequestStop(CompositionCompletion completion) {
    stop_requested_.store(true, std::memory_order_release);
    const std::weak_ptr<RenderCompositionRoot> weak_owner = weak_from_this();
    const auto rejected_completion = completion;
    const auto accepted = lifecycle_scope_->Spawn(
        "render_composition_stop",
        [weak_owner, completion = std::move(completion)]() mutable {
            return RunStop(weak_owner, std::move(completion));
        });
    if (!accepted) {
        CompleteSafely(rejected_completion, std::unexpected(MakeCompositionError(
            RenderErrorCode::kModuleLifecycleRejected,
            "stop",
            "lifecycle scope rejected the stop request",
            true)));
    }
    return accepted;
}

std::vector<BuiltinModuleSnapshot>
RenderCompositionRoot::SnapshotModules() const {
    return catalog_->SnapshotAll();
}

PxAwaitable<void> RenderCompositionRoot::RunStart(
    std::weak_ptr<RenderCompositionRoot> weak_owner,
    CompositionCompletion completion) {
    const auto owner = weak_owner.lock();
    if (!owner) {
        CompleteSafely(completion, std::unexpected(MakeCompositionError(
            RenderErrorCode::kModuleLifecycleRejected,
            "start",
            "composition owner expired before start")));
        co_return;
    }
    if (owner->running_) {
        CompleteSafely(completion, {});
        co_return;
    }
    if (owner->starting_ || owner->stopping_) {
        CompleteSafely(completion, std::unexpected(MakeCompositionError(
            RenderErrorCode::kModuleLifecycleRejected,
            "start",
            "another lifecycle transition is active",
            true)));
        co_return;
    }
    if (owner->stop_requested_.load(std::memory_order_acquire)) {
        CompleteSafely(completion, std::unexpected(MakeCompositionError(
            RenderErrorCode::kModuleLifecycleRejected,
            "start",
            "stop was requested before startup began",
            true)));
        co_return;
    }

    owner->starting_ = true;
    owner->start_completion_ = PxAsyncOneShot<void>::Create(
        owner->lifecycle_scope_->Executor());
    auto plan = owner->catalog_->ResolveStartupPlan();
    if (!plan) {
        owner->starting_ = false;
        static_cast<void>(owner->start_completion_->TryComplete(
            PxResult<void>::Success()));
        CompleteSafely(completion, std::unexpected(plan.error()));
        co_return;
    }

    LOGI("event=composition.start component=render_composition_root "
         "module_count={} outcome=begin",
         plan->size());
    owner->active_modules_.clear();
    std::optional<RenderError> start_error;
    for (auto registration : *plan) {
        if (owner->stop_requested_.load(std::memory_order_acquire)) {
            start_error = MakeCompositionError(
                RenderErrorCode::kModuleLifecycleRejected,
                "start",
                "startup cancelled by stop request",
                true);
            break;
        }
        const auto module_id = registration.descriptor.id;
        static_cast<void>(owner->catalog_->SetRuntimeState(
            module_id, BuiltinModuleRuntimeState::kStarting));

        ModuleLifecycleResult result;
        try {
            result = co_await registration.start();
        }
        catch (const std::exception& error) {
            result = ExceptionResult(RenderErrorCode::kModuleStartFailed,
                                     "start",
                                     module_id,
                                     error.what());
        }
        catch (...) {
            result = ExceptionResult(RenderErrorCode::kModuleStartFailed,
                                     "start",
                                     module_id,
                                     "unknown_exception");
        }

        if (!result) {
            start_error = result.error();
            static_cast<void>(owner->catalog_->SetRuntimeState(
                module_id,
                BuiltinModuleRuntimeState::kFailed,
                start_error));
            LOGE("event=module.start component={} module={} code={} "
                 "outcome=failed recoverable={} reason={}",
                 start_error->component,
                 module_id,
                 StableErrorCode(start_error->code),
                 start_error->recoverable,
                 start_error->reason);
            break;
        }

        owner->active_modules_.push_back(std::move(registration));
        static_cast<void>(owner->catalog_->SetRuntimeState(
            module_id, BuiltinModuleRuntimeState::kRunning));
        LOGI("event=module.start component=render_composition_root "
             "module={} capability={} outcome=success",
             module_id,
             StableModuleCapability(
                 owner->active_modules_.back().descriptor.capability));
    }

    if (start_error ||
        owner->stop_requested_.load(std::memory_order_acquire)) {
        auto rollback_error = co_await StopRegistrations(
            owner, std::move(owner->active_modules_));
        owner->active_modules_.clear();
        owner->running_ = false;
        if (!start_error && rollback_error) {
            start_error = std::move(rollback_error);
        }
        if (!start_error) {
            start_error = MakeCompositionError(
                RenderErrorCode::kModuleLifecycleRejected,
                "start",
                "startup cancelled by stop request",
                true);
        }
    }
    else {
        owner->running_ = true;
    }

    owner->starting_ = false;
    static_cast<void>(owner->start_completion_->TryComplete(
        PxResult<void>::Success()));
    if (start_error) {
        LOGE("event=composition.start component=render_composition_root "
             "code={} outcome=rolled_back reason={}",
             StableErrorCode(start_error->code),
             start_error->reason);
        CompleteSafely(completion, std::unexpected(std::move(*start_error)));
    }
    else {
        LOGI("event=composition.start component=render_composition_root "
             "module_count={} outcome=success",
             owner->active_modules_.size());
        CompleteSafely(completion, {});
    }
}

PxAwaitable<void> RenderCompositionRoot::RunStop(
    std::weak_ptr<RenderCompositionRoot> weak_owner,
    CompositionCompletion completion) {
    const auto owner = weak_owner.lock();
    if (!owner) {
        CompleteSafely(completion, {});
        co_return;
    }
    if (owner->starting_ && owner->start_completion_) {
        const auto start_wait = co_await PxAsyncOneShot<void>::WaitUntil(
            owner->start_completion_, std::chrono::steady_clock::now() + 5s);
        if (!start_wait) {
            const auto error = MakeCompositionError(
                RenderErrorCode::kAsyncScopeDrainTimeout,
                "stop",
                "startup did not acknowledge cancellation before deadline");
            LOGE("event=composition.stop component=render_composition_root "
                 "code={} outcome=failed reason={}",
                 StableErrorCode(error.code),
                 error.reason);
            CompleteSafely(completion, std::unexpected(error));
            co_return;
        }
    }
    if (owner->stopping_) {
        CompleteSafely(completion, std::unexpected(MakeCompositionError(
            RenderErrorCode::kModuleLifecycleRejected,
            "stop",
            "another stop transition is active",
            true)));
        co_return;
    }
    if (!owner->running_ && owner->active_modules_.empty()) {
        CompleteSafely(completion, {});
        co_return;
    }

    owner->stopping_ = true;
    LOGI("event=composition.stop component=render_composition_root "
         "module_count={} outcome=begin",
         owner->active_modules_.size());
    auto stop_error = co_await StopRegistrations(
        owner, std::move(owner->active_modules_));
    owner->active_modules_.clear();
    owner->running_ = false;
    owner->stopping_ = false;

    if (stop_error) {
        LOGE("event=composition.stop component=render_composition_root "
             "code={} outcome=completed_with_error reason={}",
             StableErrorCode(stop_error->code),
             stop_error->reason);
        CompleteSafely(completion, std::unexpected(std::move(*stop_error)));
    }
    else {
        LOGI("event=composition.stop component=render_composition_root "
             "outcome=success");
        CompleteSafely(completion, {});
    }
}

PxAwaitable<std::optional<RenderError>>
RenderCompositionRoot::StopRegistrations(
    const std::shared_ptr<RenderCompositionRoot>& owner,
    std::vector<BuiltinModuleRegistration> registrations) {
    std::optional<RenderError> first_error;
    for (auto registration = registrations.rbegin();
         registration != registrations.rend();
         ++registration) {
        const auto module_id = registration->descriptor.id;
        static_cast<void>(owner->catalog_->SetRuntimeState(
            module_id, BuiltinModuleRuntimeState::kStopping));
        ModuleLifecycleResult result;
        try {
            result = co_await registration->stop();
        }
        catch (const std::exception& error) {
            result = ExceptionResult(RenderErrorCode::kModuleStopFailed,
                                     "stop",
                                     module_id,
                                     error.what());
        }
        catch (...) {
            result = ExceptionResult(RenderErrorCode::kModuleStopFailed,
                                     "stop",
                                     module_id,
                                     "unknown_exception");
        }

        if (!result) {
            if (!first_error) {
                first_error = result.error();
            }
            static_cast<void>(owner->catalog_->SetRuntimeState(
                module_id,
                BuiltinModuleRuntimeState::kFailed,
                result.error()));
            LOGE("event=module.stop component={} module={} code={} "
                 "outcome=failed recoverable={} reason={}",
                 result.error().component,
                 module_id,
                 StableErrorCode(result.error().code),
                 result.error().recoverable,
                 result.error().reason);
            continue;
        }
        static_cast<void>(owner->catalog_->SetRuntimeState(
            module_id, BuiltinModuleRuntimeState::kStopped));
        LOGI("event=module.stop component=render_composition_root "
             "module={} outcome=success",
             module_id);
    }
    co_return first_error;
}

}  // namespace px::render
