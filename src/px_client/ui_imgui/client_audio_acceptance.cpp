#include "client_audio_acceptance.h"

#include <iostream>
#include <utility>

#include "client_session.h"
#include "px_common/log.h"
#include "px_desktop_shell/desktop_shell.h"

namespace px::client::imgui {

ClientAudioAcceptance::ClientAudioAcceptance(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session)
    : shell_{shell}, session_{std::move(session)} {}

void ClientAudioAcceptance::Tick() {
    if (finished_) {
        return;
    }
    const auto snapshot = session_->Snapshot();
    if (snapshot.state == ClientConnectionState::Rejected || snapshot.state == ClientConnectionState::Disconnected) {
        Finish(6, "FAIL:SESSION_ENDED");
        return;
    }
    if (snapshot.frame && snapshot.decodedAudioFrames > 0U && snapshot.decodedAudioBytes > 0U) {
        const auto now = std::chrono::steady_clock::now();
        if (!successObservedAt_) {
            successObservedAt_ = now;
            return;
        }
        if (now - *successObservedAt_ < std::chrono::seconds{1}) {
            return;
        }
        LOGI("Audio acceptance received {} decoded frames and {} bytes", snapshot.decodedAudioFrames, snapshot.decodedAudioBytes);
        Finish(0, "PASS");
        return;
    }
    if (std::chrono::steady_clock::now() >= deadline_) {
        Finish(6, "FAIL:AUDIO_TIMEOUT");
    }
}

int ClientAudioAcceptance::ExitCode() const noexcept { return exitCode_; }

void ClientAudioAcceptance::Finish(const int exitCode, const std::string_view result) {
    exitCode_ = exitCode;
    finished_ = true;
    std::cout << "PIXELS_AUDIO_ACCEPTANCE=" << result << std::endl;
    shell_.get().RequestExit();
}

}  // namespace px::client::imgui
