#include "client_session.h"

#include "client_audio_output.h"
#include "ct_virtual_display_protocol.h"
#include "px_client_sdk/platform/windows/windows_decoder_factory.h"
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_connection_params.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_voice_call.h"
#include "px_client_sdk/platform/voice_audio_endpoint_port.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/data.h"
#include "px_common/md5.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include "px_message.pb.h"
#include "px_ft_engine/ft_async_session.h"
#include "px_ft_engine/ft_engine.h"
#include "px_rdp/rdp_client_endpoint.h"
#include "px_rdp/rdp_stream_packet.h"
#include "rdp/rdp_session.h"

#include <SDL3/SDL.h>
#include <freerdp/input.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

namespace px::client::imgui {

std::vector<ClientTransferJob> ClientSession::TransferJobs() const {
    const std::scoped_lock lock{mutex_};
    return transferJobs_;
}

std::vector<ClientRemoteEntry> ClientSession::RemoteEntries() const {
    const std::scoped_lock lock{mutex_};
    return remoteEntries_;
}

std::string ClientSession::RemotePath() const {
    const std::scoped_lock lock{mutex_};
    return remotePath_;
}

std::optional<ClientOverwriteRequest> ClientSession::PendingOverwrite() const {
    const std::scoped_lock lock{mutex_};
    return overwrite_;
}

bool ClientSession::ListRemoteDirectory(const std::string& path, const bool includeHidden) {
    const auto fileTransfer = FileTransfer();
    return fileTransfer && path.size() <= 4096U &&
           fileTransfer->Post("pixels-client-ft-list", [path, includeHidden](const auto& engine) { engine->ReadDir(path, includeHidden); });
}

std::int32_t ClientSession::StartUpload(const std::string& localPath, const std::string& remoteDirectory) {
    const auto fileTransfer = FileTransfer();
    if (!fileTransfer || localPath.empty())
        return 0;
    const std::string fileName{std::filesystem::path{localPath}.filename().string()};
    if (fileName.empty())
        return 0;
    std::error_code sizeError{};
    const bool regularFile{std::filesystem::is_regular_file(std::filesystem::path{localPath}, sizeError)};
    const std::uint64_t expectedBytes{regularFile ? std::filesystem::file_size(std::filesystem::path{localPath}, sizeError) : 0U};
    std::string remoteTarget{remoteDirectory};
    if (!remoteTarget.empty() && !remoteTarget.ends_with('/') && !remoteTarget.ends_with('\\'))
        remoteTarget.push_back('/');
    remoteTarget += fileName;
    const auto result = std::make_shared<std::atomic_int32_t>();
    const bool completed = fileTransfer->PostAndWait(
        "pixels-client-ft-upload",
        [localPath, remoteTarget = std::move(remoteTarget), streamId = config_.streamId, result](const auto& engine) {
            result->store(engine->SendFiles(localPath, false, remoteTarget, 0, false, streamId));
        },
        std::chrono::seconds{2});
    const std::int32_t id{completed ? result->load() : 0};
    if (id > 0) {
        const std::scoped_lock lock{mutex_};
        const auto found = std::ranges::find(transferJobs_, id, &ClientTransferJob::id);
        const ClientTransferJob request{.id = id,
                                        .totalBytes = sizeError ? 0U : expectedBytes,
                                        .fileCount = regularFile ? 1 : 0,
                                        .name = fileName,
                                        .sourcePath = localPath,
                                        .destinationDirectory = remoteDirectory,
                                        .download = false};
        if (found == transferJobs_.end())
            transferJobs_.push_back(request);
        else {
            found->name = request.name;
            found->sourcePath = request.sourcePath;
            found->destinationDirectory = request.destinationDirectory;
            if (found->totalBytes == 0U)
                found->totalBytes = request.totalBytes;
            if (found->fileCount == 0)
                found->fileCount = request.fileCount;
            if (found->done && found->totalBytes > 0U)
                found->completedBytes = found->totalBytes;
        }
    }
    return id;
}

std::int32_t ClientSession::StartDownload(const std::string& remotePath, const std::string& localDirectory) {
    const auto fileTransfer = FileTransfer();
    if (!fileTransfer || remotePath.empty() || localDirectory.empty())
        return 0;
    const auto separator = remotePath.find_last_of("/\\");
    const std::string fileName{separator == std::string::npos ? remotePath : remotePath.substr(separator + 1)};
    if (fileName.empty())
        return 0;
    std::uint64_t expectedBytes{};
    bool regularFile{};
    {
        const std::scoped_lock lock{mutex_};
        const auto source = std::ranges::find(remoteEntries_, remotePath, &ClientRemoteEntry::path);
        if (source != remoteEntries_.end()) {
            expectedBytes = source->size;
            regularFile = !source->directory;
        }
    }
    const std::string localTarget{(std::filesystem::path{localDirectory} / std::filesystem::path{fileName}).string()};
    const auto result = std::make_shared<std::atomic_int32_t>();
    const bool completed = fileTransfer->PostAndWait(
        "pixels-client-ft-download",
        [remotePath, localTarget, streamId = config_.streamId, result](const auto& engine) {
            result->store(engine->ReceiveFiles(remotePath, false, localTarget, 0, false, streamId));
        },
        std::chrono::seconds{2});
    const std::int32_t id{completed ? result->load() : 0};
    if (id > 0) {
        const std::scoped_lock lock{mutex_};
        const auto found = std::ranges::find(transferJobs_, id, &ClientTransferJob::id);
        const ClientTransferJob request{.id = id,
                                        .totalBytes = expectedBytes,
                                        .fileCount = regularFile ? 1 : 0,
                                        .name = fileName,
                                        .sourcePath = remotePath,
                                        .destinationDirectory = localDirectory,
                                        .download = true};
        if (found == transferJobs_.end())
            transferJobs_.push_back(request);
        else {
            found->name = request.name;
            found->sourcePath = request.sourcePath;
            found->destinationDirectory = request.destinationDirectory;
            if (found->totalBytes == 0U)
                found->totalBytes = request.totalBytes;
            if (found->fileCount == 0)
                found->fileCount = request.fileCount;
            if (found->done && found->totalBytes > 0U)
                found->completedBytes = found->totalBytes;
        }
    }
    return id;
}

bool ClientSession::CancelTransfer(const std::int32_t jobId) {
    const auto fileTransfer = FileTransfer();
    return fileTransfer && jobId > 0 && fileTransfer->Post("pixels-client-ft-cancel", [jobId](const auto& engine) { engine->CancelJob(jobId); });
}

bool ClientSession::ResumeTransfer(const std::int32_t jobId) {
    ClientTransferJob job{};
    {
        const std::scoped_lock lock{mutex_};
        const auto found = std::ranges::find(transferJobs_, jobId, &ClientTransferJob::id);
        if (found == transferJobs_.end() || found->sourcePath.empty() || found->destinationDirectory.empty() || found->error.empty())
            return false;
        job = *found;
    }
    const auto fileTransfer = FileTransfer();
    if (!fileTransfer)
        return false;
    std::string target{};
    if (job.download) {
        const auto separator = job.sourcePath.find_last_of("/\\");
        const std::string name{separator == std::string::npos ? job.sourcePath : job.sourcePath.substr(separator + 1U)};
        target = (std::filesystem::path{job.destinationDirectory} / std::filesystem::u8path(name)).string();
    } else {
        target = job.destinationDirectory;
        if (!target.empty() && !target.ends_with('/') && !target.ends_with('\\'))
            target.push_back('/');
        target += std::filesystem::path{job.sourcePath}.filename().string();
    }
    const auto result = std::make_shared<std::atomic_int32_t>();
    const bool completed = fileTransfer->PostAndWait(
        "pixels-client-ft-resume",
        [job, target = std::move(target), streamId = config_.streamId, result](const auto& engine) {
            result->store(job.download ? engine->ReceiveFiles(job.sourcePath, false, target, 0, true, streamId)
                                       : engine->SendFiles(job.sourcePath, false, target, 0, true, streamId));
        },
        std::chrono::seconds{2});
    if (!completed || result->load() <= 0)
        return false;
    const std::scoped_lock lock{mutex_};
    transferJobs_.erase(std::ranges::find(transferJobs_, jobId, &ClientTransferJob::id));
    job.id = result->load();
    job.done = false;
    job.error.clear();
    job.completedBytes = 0U;
    transferJobs_.push_back(std::move(job));
    return true;
}

void ClientSession::RemoveCompletedTransfers() {
    const std::scoped_lock lock{mutex_};
    std::erase_if(transferJobs_, [](const ClientTransferJob& job) { return job.done || !job.error.empty(); });
}

bool ClientSession::ConfirmOverwrite(const bool overwrite, const bool applyToAll) {
    ClientOverwriteRequest request{};
    {
        const std::scoped_lock lock{mutex_};
        if (!overwrite_)
            return false;
        request = *overwrite_;
        overwrite_.reset();
    }
    const auto fileTransfer = FileTransfer();
    return fileTransfer && fileTransfer->Post("pixels-client-ft-overwrite", [request, overwrite, applyToAll](const auto& engine) {
        if (applyToAll)
            engine->SetOverwriteStrategy(request.jobId, overwrite);
        engine->ConfirmFile(request.jobId, request.fileNumber, overwrite);
    });
}

bool ClientSession::CreateRemoteDirectory(const std::string& path) {
    const auto fileTransfer = FileTransfer();
    return fileTransfer && !path.empty() && path.size() <= 4096U &&
           fileTransfer->Post("pixels-client-ft-create-directory",
                              [path](const auto& engine) { engine->CreateDir(px::ft::FtEngine::NextJobId(), path); });
}

bool ClientSession::RemoveRemoteEntry(const std::string& path, const bool directory) {
    const auto fileTransfer = FileTransfer();
    return fileTransfer && !path.empty() && path.size() <= 4096U &&
           fileTransfer->Post("pixels-client-ft-remove-entry", [path, directory](const auto& engine) {
               const std::int32_t id{px::ft::FtEngine::NextJobId()};
               if (directory)
                   engine->RemoveDir(id, path, true);
               else
                   engine->RemoveFile(id, path);
           });
}

bool ClientSession::RemoveRemoteEntries(const std::vector<ClientRemoteEntry>& entries) {
    const auto fileTransfer = FileTransfer();
    if (!fileTransfer || entries.empty())
        return false;
    for (const auto& entry : entries) {
        if (entry.path.empty() || entry.path.size() > 4096U)
            return false;
    }
    return fileTransfer->Post("pixels-client-ft-remove-entries", [entries](const auto& engine) {
        for (const auto& entry : entries) {
            const std::int32_t id{px::ft::FtEngine::NextJobId()};
            if (entry.directory)
                engine->RemoveDir(id, entry.path, true);
            else
                engine->RemoveFile(id, entry.path);
        }
    });
}

bool ClientSession::RenameRemoteEntry(const std::string& path, const std::string& newName) {
    const auto fileTransfer = FileTransfer();
    return fileTransfer && !path.empty() && path.size() <= 4096U && !newName.empty() && newName.size() <= 255U &&
           fileTransfer->Post("pixels-client-ft-rename-entry",
                              [path, newName](const auto& engine) { engine->RenameFile(px::ft::FtEngine::NextJobId(), path, newName); });
}

std::optional<ClientFileOperationResult> ClientSession::TakeRemoteFileOperationResult() {
    const std::scoped_lock lock{mutex_};
    auto result = std::move(remoteFileOperationResult_);
    remoteFileOperationResult_.reset();
    return result;
}

bool ClientSession::StartRecording() {
    std::shared_ptr<px::RecordingSession> recording{};
    {
        const std::scoped_lock lock{mutex_};
        std::erase_if(finishingRecordings_, [](const auto& item) { return item->WaitFor(std::chrono::milliseconds::zero()); });
        if (recording_ || finishingRecordings_.size() >= 4U || !sdk_)
            return false;
        const std::filesystem::path directory =
            config_.recordingPath.empty() ? std::filesystem::current_path() / "recordings" : std::filesystem::u8path(config_.recordingPath);
        const std::string id{"recording-" + std::to_string(px::TimeUtil::GetCurrentTimestamp())};
        const std::weak_ptr<ClientSession> weakSelf{shared_from_this()};
        recording = px::RecordingSession::Create({.writer = {.dir = directory.string(),
                                                             .monitor_name = monitorName_,
                                                             .file_prefix = "pixels_",
                                                             .max_segment_bytes = 8LL * 1024 * 1024 * 1024,
                                                             .on_request_keyframe =
                                                                 [weakSdk = std::weak_ptr<px::ThunderSdk>(sdk_)] {
                                                                     if (const auto sdk = weakSdk.lock())
                                                                         sdk->RequestVideoKeyFrame();
                                                                 }}},
                                                 {.finished = [weakSelf, id](const px::RecordingSessionResult& result) {
                                                     if (const auto self = weakSelf.lock()) {
                                                         const std::scoped_lock stateLock{self->mutex_};
                                                         if (self->recordingId_ == id)
                                                             self->recordingId_.clear();
                                                         if (!result.error.empty())
                                                             self->status_ = "Recording failed: " + result.error;
                                                     }
                                                 }});
        if (!recording || !recording->Start())
            return false;
        recording_ = recording;
        recordingId_ = id;
    }
    sdk_->RequestVideoKeyFrame();
    return true;
}

bool ClientSession::StopRecording() {
    std::shared_ptr<px::RecordingSession> recording{};
    {
        const std::scoped_lock lock{mutex_};
        if (!recording_)
            return false;
        recording = std::move(recording_);
        finishingRecordings_.push_back(recording);
        recordingId_.clear();
    }
    recording->Stop();
    return true;
}

bool ClientSession::StartVoiceCall() {
    const auto voice = VoiceCall();
    return voice && voice->Start();
}

bool ClientSession::StopVoiceCall() {
    const auto voice = VoiceCall();
    if (!voice)
        return false;
    voice->Stop(true, "local_hangup");
    return true;
}

bool ClientSession::SetVoiceMicrophoneMuted(const bool muted) {
    const auto voice = VoiceCall();
    return voice && voice->SetMicrophoneMuted(muted);
}

bool ClientSession::SetVoiceSpeakerMuted(const bool muted) {
    const auto voice = VoiceCall();
    return voice && voice->SetSpeakerMuted(muted);
}

std::shared_ptr<px::ft::FtAsyncSession> ClientSession::FileTransfer() const {
    const std::scoped_lock lock{mutex_};
    return fileTransfer_;
}

std::shared_ptr<px::VoiceCallController> ClientSession::VoiceCall() const {
    const std::scoped_lock lock{mutex_};
    return voiceCall_;
}

void ClientSession::SetState(const ClientConnectionState state, std::string status, const ClientConnectionFailure failure) {
    const std::scoped_lock lock{mutex_};
    state_ = state;
    failure_ = failure;
    status_ = std::move(status);
}
} // namespace px::client::imgui
