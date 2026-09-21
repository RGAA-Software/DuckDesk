#include "network/file_transfer_reporter.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "architecture/services/file_transfer_service.h"
#include "network/render_service_client.h"
#include "px_common/async_delay.h"
#include "px_common/async_scope_drain.h"
#include "px_common/log.h"
#include "px_common/uuid.h"
#include "px_service_message.pb.h"

namespace px {
namespace {

int ProtocolDirection(const render::FileTransferAuditDirection direction) {
    return direction == render::FileTransferAuditDirection::kToNode ? ServiceFileTransferDirection::kServiceFileTransferToNode
                                                                    : ServiceFileTransferDirection::kServiceFileTransferFromNode;
}

int ProtocolOutcome(const render::FileTransferAuditOutcome outcome) {
    switch (outcome) {
        case render::FileTransferAuditOutcome::kCompleted:
            return ServiceFileTransferOutcome::kServiceFileTransferCompleted;
        case render::FileTransferAuditOutcome::kTransportLost:
            return ServiceFileTransferOutcome::kServiceFileTransferTransportLost;
        case render::FileTransferAuditOutcome::kHashMismatch:
            return ServiceFileTransferOutcome::kServiceFileTransferHashMismatch;
        case render::FileTransferAuditOutcome::kPolicyRevoked:
            return ServiceFileTransferOutcome::kServiceFileTransferPolicyRevoked;
        case render::FileTransferAuditOutcome::kIoError:
            return ServiceFileTransferOutcome::kServiceFileTransferIoError;
        case render::FileTransferAuditOutcome::kSourceChanged:
            return ServiceFileTransferOutcome::kServiceFileTransferSourceChanged;
        case render::FileTransferAuditOutcome::kCancelled:
            return ServiceFileTransferOutcome::kServiceFileTransferCancelled;
    }
    return ServiceFileTransferOutcome::kServiceFileTransferIoError;
}

}  // namespace

std::shared_ptr<FileTransferReporter> FileTransferReporter::Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                                   const std::shared_ptr<RenderServiceClient>& service_client) {
    if (!runtime || !service_client) {
        return {};
    }
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    return scope ? std::make_shared<FileTransferReporter>(scope, service_client) : std::shared_ptr<FileTransferReporter>{};
}

FileTransferReporter::FileTransferReporter(std::shared_ptr<PxAsyncScope> scope, std::weak_ptr<RenderServiceClient> service_client)
    : scope_(std::move(scope)), service_client_(std::move(service_client)) {}

void FileTransferReporter::Begin(const render::FileTransferAuditBegin& audit) {
    if (!IsCanonicalUuid(audit.transfer_request_id) || !IsCanonicalUuid(audit.logical_session_id) || audit.file_name.empty() ||
        audit.file_name == "." || audit.file_name == ".." || audit.file_name.contains('/') || audit.file_name.contains('\\')) {
        LOGW("event=file_transfer.begin component=render outcome=rejected code=INVALID_AUDIT_INPUT");
        return;
    }
    const auto activity = std::make_shared<Activity>(Activity{
        .transfer_request_id = audit.transfer_request_id,
        .logical_session_id = audit.logical_session_id,
        .file_name = audit.file_name,
        .direction = ProtocolDirection(audit.console_direction),
        .total_bytes = audit.total_bytes,
    });
    {
        std::scoped_lock lock(activities_mutex_);
        if (stopping_ || activities_.contains(activity->transfer_request_id)) {
            return;
        }
        activities_.emplace(activity->transfer_request_id, activity);
    }
    const auto weak_reporter = weak_from_this();
    if (!scope_ || !scope_->Spawn("file-transfer-begin", [weak_reporter, activity]() { return BeginAsync(weak_reporter, activity); })) {
        RemoveIfCurrent(activity);
    }
}

void FileTransferReporter::Progress(const render::FileTransferAuditProgress& audit) {
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(audit.transfer_request_id);
    if (stopping_ || current == activities_.end() || current->second->delivery.TerminalRequested()) {
        return;
    }
    current->second->delivery.RecordProgress(std::min(audit.transferred_bytes, current->second->total_bytes));
}

void FileTransferReporter::End(const render::FileTransferAuditEnd& audit) {
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(audit.transfer_request_id);
    if (stopping_ || current == activities_.end() || current->second->delivery.TerminalRequested()) {
        return;
    }
    auto& activity = *current->second;
    const auto transferred_bytes = std::min(audit.transferred_bytes, activity.total_bytes);
    auto terminal_outcome = ProtocolOutcome(audit.console_outcome);
    auto verified_sha256 = audit.verified_sha256;
    if (terminal_outcome == ServiceFileTransferOutcome::kServiceFileTransferCompleted) {
        if (!verified_sha256) {
            terminal_outcome = ServiceFileTransferOutcome::kServiceFileTransferIoError;
        } else if (transferred_bytes != activity.total_bytes) {
            terminal_outcome = ServiceFileTransferOutcome::kServiceFileTransferSourceChanged;
            verified_sha256.reset();
        }
    } else {
        verified_sha256.reset();
    }
    activity.delivery.RecordTerminal(transferred_bytes, terminal_outcome, std::move(verified_sha256));
}

void FileTransferReporter::Stop() { static_cast<void>(StopAndWait(std::chrono::steady_clock::now())); }

bool FileTransferReporter::StopAndWait(const std::chrono::steady_clock::time_point deadline) {
    std::shared_ptr<PxAsyncScope> scope{};
    bool drained{};
    {
        std::unique_lock lock(activities_mutex_);
        if (stopping_) {
            return activities_.empty();
        }
        drained = activities_changed_.wait_until(lock, deadline, [&activities = activities_] { return activities.empty(); });
        stopping_ = true;
        activities_.clear();
        scope = scope_;
    }
    if (scope) {
        scope->BeginStop();
        const auto remaining = std::max(std::chrono::milliseconds::zero(),
                                        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        drained = scope->WaitFor(remaining) && drained;
    }
    return drained;
}

PxAwaitable<PxResult<void>> FileTransferReporter::StopAsync(std::shared_ptr<FileTransferReporter> owner,
                                                            const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "file-transfer-reporter.stop", "reporter owner is missing"));
    }
    std::shared_ptr<PxAsyncScope> scope;
    for (;;) {
        {
            std::scoped_lock lock(owner->activities_mutex_);
            if (owner->activities_.empty()) {
                owner->stopping_ = true;
                scope = owner->scope_;
                break;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            {
                std::scoped_lock lock(owner->activities_mutex_);
                owner->stopping_ = true;
                owner->activities_.clear();
                scope = owner->scope_;
            }
            if (scope) {
                scope->BeginStop();
            }
            co_return PxResult<void>::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kTimeout, "file-transfer-reporter.stop", "file-transfer audit reports did not drain"));
        }
        const auto delay = std::min(std::chrono::milliseconds(5), std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
        const auto waited = co_await WaitForAsyncDelay(delay, "file-transfer-reporter.stop");
        if (!waited) {
            co_return waited;
        }
    }
    if (scope) {
        scope->BeginStop();
        const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "file-transfer-reporter.stop");
        if (!drained) {
            co_return PxResult<void>::Failure(drained.Error());
        }
    }
    co_return PxResult<void>::Success();
}

bool FileTransferReporter::IsCanonicalUuid(const std::string& value) {
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool separator = index == 8 || index == 13 || index == 18 || index == 23;
        if (separator ? value[index] != '-' : std::isxdigit(static_cast<unsigned char>(value[index])) == 0) {
            return false;
        }
    }
    return true;
}

bool FileTransferReporter::IsTransientFailure(const PxResult<MsgFileTransferServiceResult>& result) {
    if (!result.HasValue()) {
        return result.Error().retryable;
    }
    return !result.Value().accepted_ && result.Value().error_code_ == "NODE_CONTROL_UNAVAILABLE";
}

PxAwaitable<void> FileTransferReporter::BeginAsync(std::weak_ptr<FileTransferReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto executor = co_await asio::this_coro::executor;
    auto retry_timer = std::make_shared<asio::steady_timer>(executor);
    for (;;) {
        const auto owner = reporter.lock();
        const auto service_client = owner ? owner->service_client_.lock() : nullptr;
        if (!owner || !service_client) {
            co_return;
        }
        {
            std::scoped_lock lock(owner->activities_mutex_);
            const auto current = owner->activities_.find(activity->transfer_request_id);
            if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
                co_return;
            }
        }
        auto result = co_await service_client->RequestFileTransferBeginAsync(
            GetUUID(), activity->transfer_request_id, activity->logical_session_id, activity->direction, activity->file_name, activity->total_bytes,
            std::nullopt, std::chrono::steady_clock::now() + std::chrono::seconds(12));
        if (result.HasValue() && result.Value().accepted_ && IsCanonicalUuid(result.Value().transfer_id_) && result.Value().state_ == "active") {
            std::scoped_lock lock(owner->activities_mutex_);
            const auto current = owner->activities_.find(activity->transfer_request_id);
            if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
                co_return;
            }
            activity->transfer_id = result.Value().transfer_id_;
            break;
        }
        LOGW("event=file_transfer.begin component=render outcome=failed session={} code={}", activity->logical_session_id,
             result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
        if (!IsTransientFailure(result)) {
            owner->RemoveIfCurrent(activity);
            co_return;
        }
        retry_timer->expires_after(std::chrono::seconds(1));
        asio::error_code wait_error{};
        co_await retry_timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
        const auto cancellation = co_await asio::this_coro::cancellation_state;
        if (wait_error || cancellation.cancelled() != asio::cancellation_type::none) {
            co_return;
        }
    }
    LOGI("event=file_transfer.begin component=render outcome=success session={} transfer={} total_bytes={}", activity->logical_session_id,
         activity->transfer_id, activity->total_bytes);
    co_await ReportLoopAsync(reporter, activity);
}

PxAwaitable<void> FileTransferReporter::ReportLoopAsync(std::weak_ptr<FileTransferReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto executor = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(executor);
    for (;;) {
        bool wait_before_report{};
        {
            const auto owner = reporter.lock();
            if (!owner) {
                co_return;
            }
            std::scoped_lock lock(owner->activities_mutex_);
            const auto current = owner->activities_.find(activity->transfer_request_id);
            if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
                co_return;
            }
            wait_before_report = !activity->delivery.TerminalRequested() && !activity->delivery.HasPendingSnapshot();
        }
        if (wait_before_report) {
            timer->expires_after(std::chrono::seconds(1));
            asio::error_code wait_error{};
            co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
            const auto cancellation = co_await asio::this_coro::cancellation_state;
            if (wait_error || cancellation.cancelled() != asio::cancellation_type::none) {
                co_return;
            }
        }

        std::optional<FileTransferReportSnapshot> report_snapshot{};
        {
            const auto owner = reporter.lock();
            if (!owner) {
                co_return;
            }
            std::scoped_lock lock(owner->activities_mutex_);
            const auto current = owner->activities_.find(activity->transfer_request_id);
            if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
                co_return;
            }
            report_snapshot = activity->delivery.PrepareSnapshot(ServiceFileTransferOutcome::kServiceFileTransferProgress);
        }
        if (!report_snapshot) {
            LOGE("event=file_transfer.report component=render outcome=failed transfer={} code=REPORT_SEQUENCE_EXHAUSTED", activity->transfer_id);
            if (const auto owner = reporter.lock()) {
                owner->RemoveIfCurrent(activity);
            }
            co_return;
        }

        const auto owner = reporter.lock();
        const auto service_client = owner ? owner->service_client_.lock() : nullptr;
        if (!owner || !service_client) {
            co_return;
        }
        auto result = co_await service_client->RequestFileTransferReportAsync(
            GetUUID(), activity->transfer_id, report_snapshot->sequence, report_snapshot->transferred_bytes, report_snapshot->outcome,
            report_snapshot->verified_sha256, std::chrono::steady_clock::now() + std::chrono::seconds(12));
        const bool accepted = result.HasValue() && result.Value().accepted_ && result.Value().transfer_id_ == activity->transfer_id &&
                              result.Value().sequence_ == report_snapshot->sequence;
        if (!accepted) {
            LOGW("event=file_transfer.report component=render outcome=failed transfer={} code={}", activity->transfer_id,
                 result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
            if (!IsTransientFailure(result)) {
                owner->RemoveIfCurrent(activity);
                co_return;
            }
            timer->expires_after(std::chrono::seconds(1));
            asio::error_code wait_error{};
            co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
            const auto cancellation = co_await asio::this_coro::cancellation_state;
            if (wait_error || cancellation.cancelled() != asio::cancellation_type::none) {
                co_return;
            }
        } else {
            {
                std::scoped_lock lock(owner->activities_mutex_);
                const auto current = owner->activities_.find(activity->transfer_request_id);
                if (owner->stopping_ || current == owner->activities_.end() || current->second != activity ||
                    !activity->delivery.Accept(report_snapshot->sequence)) {
                    co_return;
                }
            }
            LOGI("event=file_transfer.report component=render outcome=success transfer={} state={} transferred_bytes={}", activity->transfer_id,
                 result.Value().state_, report_snapshot->transferred_bytes);
        }
        if (accepted && report_snapshot->terminal) {
            owner->RemoveIfCurrent(activity);
            co_return;
        }
    }
}

void FileTransferReporter::RemoveIfCurrent(const std::shared_ptr<Activity>& activity) {
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(activity->transfer_request_id);
    if (current != activities_.end() && current->second == activity) {
        activities_.erase(current);
        activities_changed_.notify_all();
    }
}

}  // namespace px
