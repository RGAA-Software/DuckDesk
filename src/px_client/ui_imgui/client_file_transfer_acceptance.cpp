#include "client_file_transfer_acceptance.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <utility>

#include "client_session.h"
#include "px_common/log.h"
#include "px_desktop_shell/desktop_shell.h"

namespace px::client::imgui {

ClientFileTransferAcceptance::ClientFileTransferAcceptance(std::reference_wrapper<px::desktop::DesktopShell> shell,
                                                           std::shared_ptr<ClientSession> session, ClientFileTransferAcceptanceConfig config)
    : shell_{shell}, session_{std::move(session)}, config_{std::move(config)} {
    const auto fileName = std::filesystem::path{config_.localSourcePath}.filename().string();
    remotePath_ = config_.remoteDirectory;
    if (!remotePath_.empty() && !remotePath_.ends_with('/') && !remotePath_.ends_with('\\')) {
        remotePath_.push_back('\\');
    }
    remotePath_ += fileName;
    if (fileName.empty() || !std::filesystem::path{config_.localSourcePath}.is_absolute() ||
        !std::filesystem::path{config_.localDownloadDirectory}.is_absolute()) {
        Fail("INVALID_ACCEPTANCE_PATH");
    }
}

void ClientFileTransferAcceptance::Tick() {
    if (stage_ == Stage::Finished) {
        return;
    }
    if (std::chrono::steady_clock::now() >= deadline_) {
        Fail("FILE_TRANSFER_TIMEOUT");
        return;
    }
    const auto snapshot = session_->Snapshot();
    if (snapshot.state == ClientConnectionState::Rejected) {
        Fail("SESSION_REJECTED");
        return;
    }

    switch (stage_) {
        case Stage::WaitForConnection:
            if (snapshot.state != ClientConnectionState::Connected || !snapshot.fileTransferAvailable || !snapshot.frame) {
                return;
            }
            if (config_.exerciseHostRestart && !uploadStartNotBefore_) {
                uploadStartNotBefore_ = std::chrono::steady_clock::now() + std::chrono::seconds{5};
                return;
            }
            if (uploadStartNotBefore_ && std::chrono::steady_clock::now() < *uploadStartNotBefore_) {
                return;
            }
            if (config_.exerciseHostRestart && !session_->SetFileTransferRateLimitBytesPerSecond(256U * 1024U)) {
                Fail("UPLOAD_RATE_LIMIT_NOT_CONFIGURED");
                return;
            }
            uploadJobId_ = session_->StartUpload(config_.localSourcePath, config_.remoteDirectory);
            if (uploadJobId_ <= 0) {
                Fail("UPLOAD_NOT_STARTED");
                return;
            }
            LOGI("event=file_transfer.acceptance component=client operation=start_upload outcome=success job={}", uploadJobId_);
            if (config_.exerciseCancelRetry) {
                if (!session_->CancelTransfer(uploadJobId_)) {
                    Fail("UPLOAD_CANCEL_NOT_STARTED");
                    return;
                }
                stage_ = Stage::AwaitUploadCancellation;
                return;
            }
            stage_ = Stage::Upload;
            return;
        case Stage::AwaitUploadCancellation: {
            const auto cancelledJob = FindJob(uploadJobId_);
            if (!cancelledJob || cancelledJob->error.empty()) {
                return;
            }
            if (!session_->ResumeTransfer(uploadJobId_)) {
                Fail("UPLOAD_RETRY_NOT_STARTED");
                return;
            }
            const auto replacementJobId = FindReplacementUploadJob(uploadJobId_);
            if (!replacementJobId) {
                Fail("UPLOAD_RETRY_JOB_NOT_FOUND");
                return;
            }
            uploadJobId_ = *replacementJobId;
            stage_ = Stage::Upload;
            return;
        }
        case Stage::Upload: {
            const auto job = FindJob(uploadJobId_);
            if (!job) {
                return;
            }
            if (!job->error.empty()) {
                Fail("UPLOAD_FAILED:" + job->error);
                return;
            }
            if (!job->done) {
                return;
            }
            downloadJobId_ = session_->StartDownload(remotePath_, config_.localDownloadDirectory);
            if (downloadJobId_ <= 0) {
                Fail("DOWNLOAD_NOT_STARTED");
                return;
            }
            stage_ = Stage::Download;
            return;
        }
        case Stage::Download: {
            const auto job = FindJob(downloadJobId_);
            if (!job) {
                return;
            }
            if (!job->error.empty()) {
                Fail("DOWNLOAD_FAILED:" + job->error);
                return;
            }
            if (!job->done) {
                return;
            }
            if (!session_->RemoveRemoteEntry(remotePath_, false)) {
                Fail("REMOTE_CLEANUP_NOT_STARTED");
                return;
            }
            stage_ = Stage::Cleanup;
            return;
        }
        case Stage::Cleanup: {
            const auto cleanup = session_->TakeRemoteFileOperationResult();
            if (!cleanup) {
                return;
            }
            if (!cleanup->success) {
                Fail("REMOTE_CLEANUP_FAILED");
                return;
            }
            completionNotBefore_ = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            stage_ = Stage::CompletionDelay;
            return;
        }
        case Stage::CompletionDelay: {
            if (!completionNotBefore_ || std::chrono::steady_clock::now() < *completionNotBefore_) {
                return;
            }
            Complete();
            return;
        }
        case Stage::Finished:
            return;
    }
}

bool ClientFileTransferAcceptance::Finished() const noexcept { return stage_ == Stage::Finished; }

int ClientFileTransferAcceptance::ExitCode() const noexcept { return exitCode_; }

std::optional<ClientTransferJob> ClientFileTransferAcceptance::FindJob(const std::int32_t jobId) const {
    const auto jobs = session_->TransferJobs();
    const auto found = std::ranges::find(jobs, jobId, &ClientTransferJob::id);
    return found == jobs.end() ? std::nullopt : std::optional<ClientTransferJob>{*found};
}

std::optional<std::int32_t> ClientFileTransferAcceptance::FindReplacementUploadJob(const std::int32_t previousJobId) const {
    const auto jobs = session_->TransferJobs();
    const auto replacement = std::ranges::find_if(
        jobs, [previousJobId, localSourcePath = config_.localSourcePath, remoteDirectory = config_.remoteDirectory](const ClientTransferJob& job) {
            return job.id != previousJobId && !job.download && job.sourcePath == localSourcePath && job.destinationDirectory == remoteDirectory;
        });
    return replacement == jobs.end() ? std::nullopt : std::optional<std::int32_t>{replacement->id};
}

void ClientFileTransferAcceptance::Fail(std::string reason) {
    if (stage_ == Stage::Finished) {
        return;
    }
    LOGE("File-transfer acceptance failed: {}", reason);
    std::cout << "PIXELS_FILE_TRANSFER_ACCEPTANCE=FAIL:" << reason << std::endl;
    exitCode_ = 6;
    stage_ = Stage::Finished;
    shell_.get().RequestExit();
}

void ClientFileTransferAcceptance::Complete() {
    LOGI("File-transfer acceptance completed");
    if (config_.exerciseCancelRetry) {
        std::cout << "PIXELS_FILE_TRANSFER_CANCEL_RETRY=PASS" << std::endl;
    }
    std::cout << "PIXELS_FILE_TRANSFER_ACCEPTANCE=PASS" << std::endl;
    exitCode_ = 0;
    stage_ = Stage::Finished;
    shell_.get().RequestExit();
}

}  // namespace px::client::imgui
