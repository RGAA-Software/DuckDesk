#include "media_recording_module.h"
#include "ct_const_def.h"
#include "px_client/modules/client_module_services.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_common/folder_util.h"
#include "px_common/log.h"
#include <QCoreApplication>
#include <QDir>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

namespace px {
namespace {
std::string ResolveRecordDir(const std::string& configured_path) {
    auto path = configured_path;
    if (path.empty())
        path = (std::filesystem::path(FolderUtil::GetProgramDataPath()) / "px_client_records").string();
    QDir directory{QString::fromStdString(path)};
    if (!directory.exists())
        directory.mkpath(".");
    return directory.exists() ? path : QCoreApplication::applicationDirPath().toStdString();
}
} // namespace

ClientMediaRecordingModule::ClientMediaRecordingModule(std::weak_ptr<ClientModuleServices> services) : services_(std::move(services)) {}

ClientMediaRecordingModule::~ClientMediaRecordingModule() {
    Stop();
}

bool ClientMediaRecordingModule::Start(const ClientModuleConfig& config) {
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_)
        return true;
    screen_recording_path_ = config.screen_recording_path_;
    stopped_ = false;
    return true;
}

void ClientMediaRecordingModule::Stop() {
    std::vector<std::shared_ptr<RecordingSession>> retiring{};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        retiring = std::move(finishing_);
        if (recording_)
            retiring.push_back(std::move(recording_));
    }
    for (const auto& run : retiring)
        run->Stop();
    for (const auto& run : retiring)
        static_cast<void>(run->WaitFor(std::chrono::seconds(5)));
    // Destruction joins outside the module lock; callbacks may notify the host.
}

void ClientMediaRecordingModule::HandleMessage(const std::shared_ptr<Message>& message) {
    std::shared_ptr<RecordingSession> run{};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_)
            return;
        run = recording_;
    }
    if (run)
        static_cast<void>(run->Submit(message));
}

void ClientMediaRecordingModule::UpdateSettings(const ClientModuleSettings&) {}

void ClientMediaRecordingModule::StartRecording() {
    std::string error{};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_)
            return;
        std::erase_if(finishing_, [](const auto& run) { return run->WaitFor(std::chrono::milliseconds::zero()); });
        if (recording_ && recording_->WaitFor(std::chrono::milliseconds::zero()))
            recording_.reset();
        if (recording_)
            return;
        if (finishing_.size() >= 4U) {
            error = "Previous recordings are still being finalized";
        } else {
            recording_ = RecordingSession::Create({.writer = {.dir = ResolveRecordDir(screen_recording_path_)}, .monitor_count = kMaxRenderViewCount},
                                                  {.finished = [weak_services = services_](const RecordingSessionResult& result) {
                                                      const auto services = weak_services.lock();
                                                      if (!services)
                                                          return;
                                                      if (!result.error.empty()) {
                                                          services->NotifyRecordingFailure(result.error);
                                                          return;
                                                      }
                                                      for (const auto& directory : result.directories)
                                                          services->NotifyRecordingComplete(directory);
                                                  }});
            if (!recording_ || !recording_->Start()) {
                recording_.reset();
                error = "Recording worker could not start";
            }
        }
    }
    if (!error.empty()) {
        if (const auto services = services_.lock())
            services->NotifyRecordingFailure(error);
    }
}

void ClientMediaRecordingModule::StopRecording() {
    std::shared_ptr<RecordingSession> retiring{};
    {
        std::lock_guard lock(lifecycle_mutex_);
        retiring = std::move(recording_);
        if (retiring)
            finishing_.push_back(retiring);
    }
    if (retiring)
        retiring->Stop();
}

std::string ClientMediaRecordingModule::GetScreenRecordingPath() const {
    std::lock_guard lock(lifecycle_mutex_);
    return screen_recording_path_;
}
} // namespace px
