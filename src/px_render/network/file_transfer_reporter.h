#ifndef PX_FILE_TRANSFER_REPORTER_H
#define PX_FILE_TRANSFER_REPORTER_H

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "network/file_transfer_report_delivery.h"
#include "px_common/async_result.h"
#include "px_common/async_runtime.h"

namespace px {

class PxAsyncRuntime;
class PxAsyncScope;
class RenderServiceClient;
class MsgFileTransferServiceResult;

namespace render {
struct FileTransferAuditBegin;
struct FileTransferAuditEnd;
struct FileTransferAuditProgress;
}  // namespace render

class FileTransferReporter final : public std::enable_shared_from_this<FileTransferReporter> {
public:
    static std::shared_ptr<FileTransferReporter> Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                        const std::shared_ptr<RenderServiceClient>& service_client);
    FileTransferReporter(std::shared_ptr<PxAsyncScope> scope, std::weak_ptr<RenderServiceClient> service_client);

    FileTransferReporter(const FileTransferReporter&) = delete;
    FileTransferReporter& operator=(const FileTransferReporter&) = delete;

    void Begin(const render::FileTransferAuditBegin& audit);
    void Progress(const render::FileTransferAuditProgress& audit);
    void End(const render::FileTransferAuditEnd& audit);
    void Stop();

private:
    struct Activity final {
        std::string transfer_request_id{};
        std::string logical_session_id{};
        std::string file_name{};
        std::string transfer_id{};
        int direction{};
        std::uint64_t total_bytes{};
        FileTransferReportDelivery delivery{};
    };

    static bool IsCanonicalUuid(const std::string& value);
    static bool IsTransientFailure(const PxResult<MsgFileTransferServiceResult>& result);
    static PxAwaitable<void> BeginAsync(std::weak_ptr<FileTransferReporter> reporter, std::shared_ptr<Activity> activity);
    static PxAwaitable<void> ReportLoopAsync(std::weak_ptr<FileTransferReporter> reporter, std::shared_ptr<Activity> activity);
    void RemoveIfCurrent(const std::shared_ptr<Activity>& activity);

    std::shared_ptr<PxAsyncScope> scope_{};
    std::weak_ptr<RenderServiceClient> service_client_{};
    std::mutex activities_mutex_{};
    std::unordered_map<std::string, std::shared_ptr<Activity>> activities_{};
    bool stopping_{};
};

}  // namespace px

#endif  // PX_FILE_TRANSFER_REPORTER_H
