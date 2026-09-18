#ifndef PX_RESOURCE_CHANNEL_REPORTER_H
#define PX_RESOURCE_CHANNEL_REPORTER_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "px_common/async_runtime.h"

namespace px {

class PxAsyncRuntime;
class PxAsyncScope;
class RenderServiceClient;

class ResourceChannelReporter final : public std::enable_shared_from_this<ResourceChannelReporter> {
public:
    static std::shared_ptr<ResourceChannelReporter> Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                           const std::shared_ptr<RenderServiceClient>& service_client);
    ResourceChannelReporter(std::shared_ptr<PxAsyncScope> scope, std::weak_ptr<RenderServiceClient> service_client);

    ResourceChannelReporter(const ResourceChannelReporter&) = delete;
    ResourceChannelReporter& operator=(const ResourceChannelReporter&) = delete;

    void Open(std::string connection_key, std::string logical_session_id, int channel_kind);
    void Close(const std::string& connection_key, int outcome);
    void Stop();

private:
    struct Activity final {
        std::string connection_key{};
        std::string source_id{};
        std::string logical_session_id{};
        std::string channel_id{};
        std::chrono::steady_clock::time_point started_at{};
        int channel_kind{};
        int close_outcome{};
        bool close_requested{};
        bool report_started{};
    };

    static bool IsCanonicalUuid(const std::string& value);
    static PxAwaitable<void> OpenAsync(std::weak_ptr<ResourceChannelReporter> reporter, std::shared_ptr<Activity> activity);
    static PxAwaitable<void> ReportCloseAsync(std::weak_ptr<ResourceChannelReporter> reporter, std::shared_ptr<Activity> activity);
    void RemoveIfCurrent(const std::shared_ptr<Activity>& activity);

    std::shared_ptr<PxAsyncScope> scope_{};
    std::weak_ptr<RenderServiceClient> service_client_{};
    std::mutex activities_mutex_{};
    std::unordered_map<std::string, std::shared_ptr<Activity>> activities_{};
    bool stopping_{};
};

}  // namespace px

#endif  // PX_RESOURCE_CHANNEL_REPORTER_H
