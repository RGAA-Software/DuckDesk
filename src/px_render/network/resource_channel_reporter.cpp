#include "network/resource_channel_reporter.h"

#include <algorithm>
#include <cctype>
#include <limits>

#include "network/render_service_client.h"
#include "px_common/async_runtime.h"
#include "px_common/log.h"
#include "px_common/uuid.h"
#include "px_service_message.pb.h"

namespace px {

std::shared_ptr<ResourceChannelReporter> ResourceChannelReporter::Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                                         const std::shared_ptr<RenderServiceClient>& service_client) {
    if (!runtime || !service_client) {
        return {};
    }
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    if (!scope) {
        return {};
    }
    return std::make_shared<ResourceChannelReporter>(scope, service_client);
}

ResourceChannelReporter::ResourceChannelReporter(std::shared_ptr<PxAsyncScope> scope, std::weak_ptr<RenderServiceClient> service_client)
    : scope_(std::move(scope)), service_client_(std::move(service_client)) {}

void ResourceChannelReporter::Open(std::string connection_key, std::string logical_session_id, const int channel_kind) {
    if (connection_key.empty() || !IsCanonicalUuid(logical_session_id) || !ResourceChannelKind_IsValid(channel_kind)) {
        return;
    }
    const auto activity = std::make_shared<Activity>(Activity{
        .connection_key = std::move(connection_key),
        .source_id = GetUUID(),
        .logical_session_id = std::move(logical_session_id),
        .started_at = std::chrono::steady_clock::now(),
        .channel_kind = channel_kind,
    });
    {
        std::scoped_lock lock(activities_mutex_);
        if (stopping_ || activities_.contains(activity->connection_key)) {
            return;
        }
        activities_.emplace(activity->connection_key, activity);
    }
    const auto weak_reporter = weak_from_this();
    if (!scope_ || !scope_->Spawn("resource-channel-open", [weak_reporter, activity]() { return OpenAsync(weak_reporter, activity); })) {
        RemoveIfCurrent(activity);
    }
}

void ResourceChannelReporter::RecordTraffic(const std::string& connection_key, const std::uint64_t sent_bytes, const std::uint64_t received_bytes) {
    const auto saturating_add = [](const std::uint64_t current, const std::uint64_t increment) {
        return increment > std::numeric_limits<std::uint64_t>::max() - current ? std::numeric_limits<std::uint64_t>::max() : current + increment;
    };
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(connection_key);
    if (stopping_ || current == activities_.end() || current->second->close_requested) {
        return;
    }
    current->second->sent_bytes = saturating_add(current->second->sent_bytes, sent_bytes);
    current->second->received_bytes = saturating_add(current->second->received_bytes, received_bytes);
}

void ResourceChannelReporter::Close(const std::string& connection_key, const int outcome) {
    std::shared_ptr<Activity> activity;
    {
        std::scoped_lock lock(activities_mutex_);
        const auto current = activities_.find(connection_key);
        if (stopping_ || current == activities_.end()) {
            return;
        }
        activity = current->second;
        activity->close_requested = true;
        activity->close_outcome = outcome;
    }
}

void ResourceChannelReporter::Stop() {
    std::shared_ptr<PxAsyncScope> scope;
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

bool ResourceChannelReporter::IsCanonicalUuid(const std::string& value) {
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

PxAwaitable<void> ResourceChannelReporter::OpenAsync(std::weak_ptr<ResourceChannelReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto owner = reporter.lock();
    const auto service_client = owner ? owner->service_client_.lock() : nullptr;
    if (!owner || !service_client) {
        co_return;
    }
    auto result =
        co_await service_client->RequestResourceChannelOpenAsync(GetUUID(), activity->source_id, activity->logical_session_id, activity->channel_kind,
                                                                 std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!result.HasValue() || !result.Value().accepted_ || !IsCanonicalUuid(result.Value().channel_id_) || result.Value().state_ != "active") {
        LOGW(
            "event=resource_channel.open component=render outcome=failed "
            "session={} code={}",
            activity->logical_session_id, result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
        owner->RemoveIfCurrent(activity);
        co_return;
    }
    {
        std::scoped_lock lock(owner->activities_mutex_);
        const auto current = owner->activities_.find(activity->connection_key);
        if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
            co_return;
        }
        activity->channel_id = result.Value().channel_id_;
    }
    LOGI(
        "event=resource_channel.open component=render outcome=success "
        "session={} channel={}",
        activity->logical_session_id, activity->channel_id);
    co_await ReportLoopAsync(reporter, activity);
}

PxAwaitable<void> ResourceChannelReporter::ReportLoopAsync(std::weak_ptr<ResourceChannelReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto executor = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(executor);
    for (;;) {
        bool closing_before_wait{};
        {
            const auto owner = reporter.lock();
            if (!owner) {
                co_return;
            }
            std::scoped_lock lock(owner->activities_mutex_);
            const auto current = owner->activities_.find(activity->connection_key);
            if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
                co_return;
            }
            closing_before_wait = activity->close_requested;
        }
        if (!closing_before_wait) {
            timer->expires_after(std::chrono::seconds(5));
            asio::error_code wait_error;
            co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
            const auto cancellation = co_await asio::this_coro::cancellation_state;
            if (wait_error || cancellation.cancelled() != asio::cancellation_type::none) {
                co_return;
            }
        }

        bool closing{};
        int outcome{static_cast<int>(ResourceChannelOutcome::kResourceChannelProgress)};
        std::uint64_t sequence{};
        std::uint64_t sent_bytes{};
        std::uint64_t received_bytes{};
        std::uint64_t elapsed_ms{};
        {
            const auto owner = reporter.lock();
            if (!owner) {
                co_return;
            }
            std::scoped_lock lock(owner->activities_mutex_);
            const auto current = owner->activities_.find(activity->connection_key);
            if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
                co_return;
            }
            closing = activity->close_requested;
            if (closing) {
                outcome = activity->close_outcome;
            }
            sequence = ++activity->sequence;
            sent_bytes = activity->sent_bytes;
            received_bytes = activity->received_bytes;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - activity->started_at);
            elapsed_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(elapsed.count())));
        }

        const auto owner = reporter.lock();
        const auto service_client = owner ? owner->service_client_.lock() : nullptr;
        if (!owner || !service_client) {
            co_return;
        }
        auto result = co_await service_client->RequestResourceChannelReportAsync(GetUUID(), activity->channel_id, sequence, sent_bytes,
                                                                                 received_bytes, elapsed_ms, outcome,
                                                                                 std::chrono::steady_clock::now() + std::chrono::seconds(12));
        if (!result.HasValue() || !result.Value().accepted_) {
            LOGW("event=resource_channel.report component=render outcome=failed channel={} code={}", activity->channel_id,
                 result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
        } else {
            LOGI("event=resource_channel.report component=render outcome=success channel={} state={} sent_bytes={} received_bytes={}",
                 activity->channel_id, result.Value().state_, sent_bytes, received_bytes);
        }
        if (closing) {
            owner->RemoveIfCurrent(activity);
            co_return;
        }
    }
}

void ResourceChannelReporter::RemoveIfCurrent(const std::shared_ptr<Activity>& activity) {
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(activity->connection_key);
    if (current != activities_.end() && current->second == activity) {
        activities_.erase(current);
    }
}

}  // namespace px
