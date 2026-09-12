#include "client_toolbar.h"

#include "client_file_transfer_panel.h"
#include "client_session.h"
#include "client_text.h"

#include <imgui.h>

namespace px::client::imgui {

ClientToolbar::ClientToolbar(std::shared_ptr<ClientFileTransferPanel> fileTransfer) : fileTransfer_{std::move(fileTransfer)} {}

bool ClientToolbar::Visible() const noexcept {
    return visible_;
}

void ClientToolbar::Draw(const std::shared_ptr<ClientSession>& session, const bool english) {
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto snapshot = session->Snapshot();
    if (!visible_) {
        if (ImGui::SmallButton(text(ClientText::Controls))) visible_ = true;
        return;
    }
    const auto status = [&] {
        switch (snapshot.state) {
        case ClientConnectionState::Connecting: return ClientText::Connecting;
        case ClientConnectionState::Connected: return ClientText::Connected;
        case ClientConnectionState::MediaUnavailable: return ClientText::MediaUnavailable;
        case ClientConnectionState::Rejected: return ClientText::Rejected;
        case ClientConnectionState::Disconnected: return ClientText::Disconnected;
        }
        return ClientText::Disconnected;
    }();
    ImGui::TextUnformatted(text(status));
    if (!snapshot.monitorName.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0F);
        if (ImGui::BeginCombo("##monitor", snapshot.monitorName.c_str())) {
            for (const auto& monitor : snapshot.monitors) {
                if (ImGui::Selectable(monitor.c_str(), monitor == snapshot.monitorName)) static_cast<void>(session->SwitchMonitor(monitor));
            }
            ImGui::EndCombo();
        }
    }
    if (!snapshot.resolutions.empty()) {
        ImGui::SameLine();
        const auto label = std::to_string(resolutionWidth_) + "x" + std::to_string(resolutionHeight_);
        ImGui::SetNextItemWidth(150.0F);
        if (ImGui::BeginCombo("##resolution", resolutionWidth_ > 0 ? label.c_str() : text(ClientText::Resolution))) {
            for (const auto& resolution : snapshot.resolutions) {
                const auto value = std::to_string(resolution.width) + "x" + std::to_string(resolution.height);
                if (ImGui::Selectable(value.c_str(), resolution.width == resolutionWidth_ && resolution.height == resolutionHeight_)) {
                    resolutionWidth_ = resolution.width;
                    resolutionHeight_ = resolution.height;
                    static_cast<void>(session->ChangeResolution(resolution.width, resolution.height));
                }
            }
            ImGui::EndCombo();
        }
    }
    if (snapshot.virtualDisplayAvailable) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s %u/%u", text(ClientText::VirtualDisplays), snapshot.virtualDisplayCount, snapshot.virtualDisplayMaximum);
        ImGui::SameLine();
        ImGui::BeginDisabled(snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount >= snapshot.virtualDisplayMaximum);
        if (ImGui::SmallButton("+##virtual-display")) static_cast<void>(session->CreateVirtualDisplay());
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount == 0U);
        if (ImGui::SmallButton("-##virtual-display")) static_cast<void>(session->RemoveVirtualDisplay());
        ImGui::EndDisabled();
    }
    ImGui::NewLine();
    if (ImGui::Button(text(ClientText::Files)) && snapshot.fileTransferAvailable) fileTransfer_->Open();
    ImGui::SameLine();
    if (ImGui::Button(text(ClientText::SecureAttention))) static_cast<void>(session->SendSecureAttention());
    ImGui::SameLine();
    if (ImGui::Checkbox(text(ClientText::Audio), &audioEnabled_)) static_cast<void>(session->SetAudioEnabled(audioEnabled_));
    ImGui::SameLine();
    if (snapshot.recording) {
        if (ImGui::Button(text(ClientText::StopRecording))) static_cast<void>(session->StopRecording());
    } else if (ImGui::Button(text(ClientText::Record))) {
        static_cast<void>(session->StartRecording());
    }
    ImGui::SameLine();
    if (ImGui::Button(text(ClientText::Screenshot))) {
        screenshotStatus_ = session->SaveScreenshot().value_or(text(ClientText::ScreenshotFailed));
    }
    ImGui::SameLine();
    if (snapshot.voiceAvailable) {
        if (snapshot.voiceStatus == "Connected" || snapshot.voiceStatus == "Calling") {
            if (ImGui::Button(text(ClientText::HangUp))) static_cast<void>(session->StopVoiceCall());
        } else if (ImGui::Button(text(ClientText::Voice))) {
            static_cast<void>(session->StartVoiceCall());
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(text(ClientText::MuteMicrophone), &microphoneMuted_)) {
            static_cast<void>(session->SetVoiceMicrophoneMuted(microphoneMuted_));
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(text(ClientText::MuteSpeaker), &speakerMuted_)) {
            static_cast<void>(session->SetVoiceSpeakerMuted(speakerMuted_));
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0F);
    if (ImGui::SliderInt("FPS", &frameRate_, 15, 120)) static_cast<void>(session->SetFrameRate(frameRate_));
    ImGui::SameLine();
    ImGui::Checkbox(text(ClientText::Statistics), &showStatistics_);
    ImGui::SameLine();
    if (ImGui::SmallButton(text(ClientText::Hide))) visible_ = false;
    if (showStatistics_) {
        ImGui::TextDisabled("FPS %d  %d ms  %d Kbps  %s", snapshot.framesPerSecond, snapshot.latencyMilliseconds, snapshot.bitrateKbps,
                            snapshot.decoder.c_str());
    }
    if (!screenshotStatus_.empty()) ImGui::TextDisabled("%s", screenshotStatus_.c_str());
}

} // namespace px::client::imgui
