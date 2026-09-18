#include "network/resource_channel_reporter.h"

#include <algorithm>
#include <cctype>

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
        if (activity->channel_id.empty() || activity->report_started) {
            return;
        }
        activity->report_started = true;
    }
    const auto weak_reporter = weak_from_this();
    if (!scope_ || !scope_->Spawn("resource-channel-close", [weak_reporter, activity]() { return ReportCloseAsync(weak_reporter, activity); })) {
        RemoveIfCurrent(activity);
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
    bool report_close = false;
    {
        std::scoped_lock lock(owner->activities_mutex_);
        const auto current = owner->activities_.find(activity->connection_key);
        if (owner->stopping_ || current == owner->activities_.end() || current->second != activity) {
            co_return;
        }
        activity->channel_id = result.Value().channel_id_;
        report_close = activity->close_requested && !activity->report_started;
        activity->report_started = report_close;
    }
    LOGI(
        "event=resource_channel.open component=render outcome=success "
        "session={} channel={}",
        activity->logical_session_id, activity->channel_id);
    if (report_close) {
        co_await ReportCloseAsync(reporter, activity);
    }
}

PxAwaitable<void> ResourceChannelReporter::ReportCloseAsync(std::weak_ptr<ResourceChannelReporter> reporter, std::shared_ptr<Activity> activity) {
    const auto owner = reporter.lock();
    const auto service_client = owner ? owner->service_client_.lock() : nullptr;
    if (!owner || !service_client) {
        co_return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - activity->started_at);
    auto result = co_await service_client->RequestResourceChannelReportAsync(
        GetUUID(), activity->channel_id, 1, 0, 0, static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(elapsed.count()))),
        activity->close_outcome, std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!result.HasValue() || !result.Value().accepted_) {
        LOGW(
            "event=resource_channel.close component=render outcome=failed "
            "channel={} code={}",
            activity->channel_id, result.HasValue() ? result.Value().error_code_ : result.Error().StableCode());
    } else {
        LOGI(
            "event=resource_channel.close component=render outcome=success "
            "channel={} state={}",
            activity->channel_id, result.Value().state_);
    }
    owner->RemoveIfCurrent(activity);
}

void ResourceChannelReporter::RemoveIfCurrent(const std::shared_ptr<Activity>& activity) {
    std::scoped_lock lock(activities_mutex_);
    const auto current = activities_.find(activity->connection_key);
    if (current != activities_.end() && current->second == activity) {
        activities_.erase(current);
    }
}

}  // namespace px
