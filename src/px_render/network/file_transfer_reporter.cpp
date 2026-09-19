#include "network/file_transfer_reporter.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "architecture/services/file_transfer_service.h"
#include "network/render_service_client.h"
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
    if (stopping_ || current == activities_.end() || current->second->terminal_requested) {
        return;
    }
    current->second->transferred_bytes = std::min(audit.transferred_bytes, current->second->total_bytes);
}

void FileTransferReporter::End(const render::FileTransferAuditEnd& audit) {
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(audit.transfer_request_id);
    if (stopping_ || current == activities_.end() || current->second->terminal_requested) {
        return;
    }
    auto& activity = *current->second;
    activity.transferred_bytes = std::min(audit.transferred_bytes, activity.total_bytes);
    activity.terminal_outcome = ProtocolOutcome(audit.console_outcome);
    activity.verified_sha256 = audit.verified_sha256;
    if (activity.terminal_outcome == ServiceFileTransferOutcome::kServiceFileTransferCompleted) {
        if (!activity.verified_sha256) {
            activity.terminal_outcome = ServiceFileTransferOutcome::kServiceFileTransferIoError;
        } else if (activity.transferred_bytes != activity.total_bytes) {
            activity.terminal_outcome = ServiceFileTransferOutcome::kServiceFileTransferSourceChanged;
            activity.verified_sha256.reset();
        }
    } else {
        activity.verified_sha256.reset();
    }
    activity.terminal_requested = true;
}

void FileTransferReporter::Stop() {
    std::shared_ptr<PxAsyncScope> scope{};
    {
        std::scoped_lock lock(activities_mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        activities_.clear();
        scope = scope_;
    }
    if (scope) {
        scope->BeginStop();
    }
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

PxAwaitable<void> FileTransferReporter::BeginAsync(std::weak_ptr<FileTransferReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto owner = reporter.lock();
    const auto service_client = owner ? owner->service_client_.lock() : nullptr;
    if (!owner || !service_client) {
        co_return;
    }
    auto result = co_await service_client->RequestFileTransferBeginAsync(GetUUID(), activity->transfer_request_id, activity->logical_session_id,
                                                                         activity->direction, activity->file_name, activity->total_bytes,
                                                                         std::nullopt, std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!result.HasValue() || !result.Value().accepted_ || !IsCanonicalUuid(result.Value().transfer_id_) || result.Value().state_ != "active") {
        LOGW("event=file_transfer.begin component=render outcome=failed session={} code={}", activity->logical_session_id,
             result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
        owner->RemoveIfCurrent(activity);
        co_return;
    }
    {
        std::scoped_lock lock(owner->activities_mutex_);
        const auto current = owner->activities_.find(activity->transfer_request_id);
        if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
            co_return;
        }
        activity->transfer_id = result.Value().transfer_id_;
    }
    LOGI("event=file_transfer.begin component=render outcome=success session={} transfer={} total_bytes={}", activity->logical_session_id,
         activity->transfer_id, activity->total_bytes);
    co_await ReportLoopAsync(reporter, activity);
}

PxAwaitable<void> FileTransferReporter::ReportLoopAsync(std::weak_ptr<FileTransferReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto executor = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(executor);
    for (;;) {
        bool terminal_before_wait{};
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
            terminal_before_wait = activity->terminal_requested;
        }
        if (!terminal_before_wait) {
            timer->expires_after(std::chrono::seconds(1));
            asio::error_code wait_error{};
            co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
            const auto cancellation = co_await asio::this_coro::cancellation_state;
            if (wait_error || cancellation.cancelled() != asio::cancellation_type::none) {
                co_return;
            }
        }

        bool terminal{};
        int outcome{ServiceFileTransferOutcome::kServiceFileTransferProgress};
        std::uint64_t sequence{};
        std::uint64_t transferred_bytes{};
        std::optional<std::array<std::uint8_t, 32>> verified_sha256{};
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
            terminal = activity->terminal_requested;
            outcome = terminal ? activity->terminal_outcome : outcome;
            sequence = ++activity->sequence;
            transferred_bytes = activity->transferred_bytes;
            verified_sha256 = activity->verified_sha256;
        }

        const auto owner = reporter.lock();
        const auto service_client = owner ? owner->service_client_.lock() : nullptr;
        if (!owner || !service_client) {
            co_return;
        }
        auto result =
            co_await service_client->RequestFileTransferReportAsync(GetUUID(), activity->transfer_id, sequence, transferred_bytes, outcome,
                                                                    verified_sha256, std::chrono::steady_clock::now() + std::chrono::seconds(12));
        if (!result.HasValue() || !result.Value().accepted_) {
            LOGW("event=file_transfer.report component=render outcome=failed transfer={} code={}", activity->transfer_id,
                 result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
        } else {
            LOGI("event=file_transfer.report component=render outcome=success transfer={} state={} transferred_bytes={}", activity->transfer_id,
                 result.Value().state_, transferred_bytes);
        }
        if (terminal) {
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
    }
}

}  // namespace px
